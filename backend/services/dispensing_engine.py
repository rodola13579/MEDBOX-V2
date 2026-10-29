"""
services/dispensing_engine.py
──────────────────────────────
FSM (Finite State Machine) điều phối toàn bộ quy trình cấp thuốc.

Lifecycle một phiên cấp thuốc:
  1. dispense_now()      → Tạo dose_log PENDING, gửi MQTT DISPENSE_NOW
  2. on_slot_reached()   → Callback từ MQTT, cập nhật → DISPENSED
  3. on_taken_button()   → Callback từ MQTT (hoặc confirm_manual()), → TAKEN + trừ tồn kho
  4. on_home_reached()   → Callback từ MQTT, xóa session, FSM về SAFE_HOME_IDLE
     --- HOẶC ---
  3. _watchdog()         → Sau DISPENSE_TIMEOUT_SECONDS → MISSED, gửi home
  3. return_home_now()   → API khẩn cấp → MISSED, gửi home

QUAN TRỌNG:
  - Chỉ 1 phiên hoạt động tại 1 thời điểm (singleton session)
  - Tồn kho CHỈ trừ khi TAKEN (KHÔNG trừ khi MISSED)
  - Callbacks được gắn vào mqtt_client trong main.py (không import vòng)
"""

import asyncio
import logging
from dataclasses import dataclass, field
from datetime import datetime, timezone

from fastapi import HTTPException

from config import settings
from database.connection import get_db_context
from database.repositories import dose_log_repo, slot_repo
from device.device_state import device_state
from device.protocol import (
    topic_cmd,
    build_dispense_cmd,
    build_home_cmd,
    build_calibrate_cmd,
)
from ws_manager import (
    ws_manager,
    WS_DISPENSE_START, WS_SLOT_REACHED,
    WS_TAKEN, WS_MISSED, WS_JAMMED,
)

logger = logging.getLogger(__name__)

DEVICE_ID = settings.DEVICE_ID


@dataclass
class ActiveSession:
    """Trạng thái phiên cấp thuốc đang diễn ra — lưu trong RAM."""
    dose_log_id: int
    slot_number: int
    schedule_id: int | None
    trigger_source: str
    status: str          # PENDING → DISPENSED
    started_at: datetime = field(default_factory=lambda: datetime.now(timezone.utc))
    timeout_seconds: int = field(default_factory=lambda: settings.DISPENSE_TIMEOUT_SECONDS)

    def seconds_remaining(self) -> int:
        elapsed = (datetime.now(timezone.utc) - self.started_at).total_seconds()
        return max(0, int(self.timeout_seconds - elapsed))

    def to_dict(self) -> dict:
        return {
            "is_active": True,
            "dose_log_id": self.dose_log_id,
            "slot_number": self.slot_number,
            "status": self.status,
            "trigger_source": self.trigger_source,
            "started_at": self.started_at.isoformat(),
            "timeout_seconds": self.timeout_seconds,
            "seconds_remaining": self.seconds_remaining(),
        }


class DispensingEngine:
    """Singleton điều phối toàn bộ quy trình cấp thuốc."""

    def __init__(self):
        self._lock = asyncio.Lock()
        self._session: ActiveSession | None = None
        self._watchdog_task: asyncio.Task | None = None

    # ── Public Properties ─────────────────────────────────────────────────────
    @property
    def has_active_session(self) -> bool:
        return self._session is not None

    def get_session_dict(self) -> dict:
        """Trả về trạng thái phiên hiện tại (dùng cho API /active và WebSocket)."""
        if self._session:
            return self._session.to_dict()
        return {"is_active": False}

    # ── Core: Kích hoạt cấp thuốc ────────────────────────────────────────────
    async def dispense_now(
        self,
        slot_number: int,
        trigger_source: str = "EARLY_MANUAL",
        schedule_id: int | None = None,
        scheduled_time: str | None = None,
    ) -> dict:
        """
        Kích hoạt phiên cấp thuốc mới.

        Kiểm tra tiền điều kiện:
          1. Không có phiên nào đang hoạt động
          2. ESP32 online và ở trạng thái SAFE_HOME_IDLE
          3. Slot hợp lệ, có thuốc, còn tồn kho

        Trả về dict của dose_log vừa tạo.
        """
        async with self._lock:
            # ── Kiểm tra 1: Không có phiên đang chạy ──────────────────────
            if self._session is not None:
                raise HTTPException(
                    status_code=409,
                    detail=(
                        f"Đang có phiên cấp thuốc khác (slot={self._session.slot_number}, "
                        f"log_id={self._session.dose_log_id}). "
                        f"Chờ hoàn tất hoặc hồi vị trước."
                    )
                )

            # ── Kiểm tra 2: ESP32 phải online ─────────────────────────────
            if not device_state.is_online:
                raise HTTPException(
                    status_code=503,
                    detail="Thiết bị ESP32 chưa kết nối. Vui lòng kiểm tra nguồn và Wi-Fi."
                )

            # ── Kiểm tra 3: ESP32 phải ở trạng thái rảnh ─────────────────
            allowed_states = ("SAFE_HOME_IDLE",)
            if device_state.fsm_state not in allowed_states:
                raise HTTPException(
                    status_code=409,
                    detail=f"Thiết bị đang bận: {device_state.fsm_state}. Chờ về Slot 0 trước."
                )

            # ── Kiểm tra 4: Slot hợp lệ ───────────────────────────────────
            async with get_db_context() as db:
                slot = await slot_repo.get_slot(db, DEVICE_ID, slot_number)

            if not slot:
                raise HTTPException(status_code=404, detail=f"Slot {slot_number} không tồn tại.")
            if not slot.get("medicine_id"):
                raise HTTPException(
                    status_code=422,
                    detail=f"Slot {slot_number} chưa có thuốc. Gán thuốc trước khi cấp."
                )
            if slot.get("current_quantity", 0) <= 0:
                raise HTTPException(
                    status_code=422,
                    detail=f"Slot {slot_number} đã hết thuốc. Vui lòng nạp lại."
                )

            # ── Tạo dose_log PENDING ───────────────────────────────────────
            if scheduled_time is None:
                scheduled_time = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")

            async with get_db_context() as db:
                log = await dose_log_repo.create_pending(
                    db,
                    device_id=DEVICE_ID,
                    slot_number=slot_number,
                    scheduled_time=scheduled_time,
                    trigger_source=trigger_source,
                    schedule_id=schedule_id,
                    notes=None,
                )

            # ── Tạo session trong RAM ──────────────────────────────────────
            self._session = ActiveSession(
                dose_log_id=log["id"],
                slot_number=slot_number,
                schedule_id=schedule_id,
                trigger_source=trigger_source,
                status="PENDING",
            )

            # ── Gửi lệnh MQTT xuống ESP32 ─────────────────────────────────
            from device.mqtt_client import mqtt_client
            cmd = build_dispense_cmd(slot_number, settings.DISPENSE_TIMEOUT_SECONDS, trigger_source)
            await mqtt_client.publish(
                topic_cmd(DEVICE_ID, "dispense"),
                cmd
            )

            # ── Khởi động watchdog ─────────────────────────────────────────
            self._start_watchdog(log["id"])

            # ── Phát sóng WebSocket ────────────────────────────────────────
            await ws_manager.broadcast(WS_DISPENSE_START, {
                **log,
                "timeout_seconds": settings.DISPENSE_TIMEOUT_SECONDS,
            })

            logger.info(
                f"[DISPENSE] Phiên bắt đầu: log_id={log['id']}, "
                f"slot={slot_number}, trigger={trigger_source}"
            )
            return log

    # ── MQTT Callbacks (đăng ký từ main.py) ──────────────────────────────────
    async def on_slot_reached(self, data: dict) -> None:
        """
        Callback: ESP32 đã xoay khay đến slot đích (SLOT_REACHED event).
        Cập nhật dose_log PENDING → DISPENSED.
        """
        async with self._lock:
            if not self._session:
                logger.debug("on_slot_reached: không có session đang chạy, bỏ qua.")
                return

            slot = data.get("slot")
            if slot != self._session.slot_number:
                logger.warning(
                    f"on_slot_reached: slot={slot} nhưng session đang ở slot={self._session.slot_number}"
                )
                return

            self._session.status = "DISPENSED"
            log_id = self._session.dose_log_id

        # Cập nhật DB ngoài lock để tránh deadlock với get_db_context
        async with get_db_context() as db:
            await dose_log_repo.mark_dispensed(db, log_id)

        # Phát sóng WebSocket
        await ws_manager.broadcast(WS_SLOT_REACHED, {
            "dose_log_id": log_id,
            "slot_number": slot,
            "status": "DISPENSED",
            **device_state.to_dict()
        })

        logger.info(f"[DISPENSE] Slot {slot} đã sẵn sàng — chờ người dùng lấy thuốc (log_id={log_id})")

    async def on_taken_button(self, data: dict) -> None:
        """
        Callback: Người dùng bấm nút vật lý TAKEN trên hộp thuốc.
        Cập nhật → TAKEN, trừ tồn kho (cả khi có session từ Web lẫn khi chạy offline), gửi lệnh về nhà.
        """
        session = await self._clear_session_and_cancel_watchdog()
        slot = int(data.get("slot") or device_state.current_slot or 0)
        is_early = (
            data.get("event") == "EARLY_TAKEN"
            or data.get("trigger") == "EARLY_MANUAL"
            or (session and session.trigger_source == "EARLY_MANUAL")
        )
        trigger_src = "EARLY_MANUAL" if is_early else "LOCAL_BUTTON"
        status_val = "EARLY_TAKEN" if is_early else "TAKEN"

        async with get_db_context() as db:
            if session:
                # Cữ uống phát sinh từ Web / Scheduler có session
                log = await dose_log_repo.mark_taken(
                    db, session.dose_log_id, trigger_source=trigger_src, status=status_val
                )
                await self._decrement_stock(db, session.slot_number)
                target_slot = session.slot_number
            elif slot >= 1 and slot <= 7:
                # Cữ uống phát sinh độc lập từ lịch NVS trên ESP32
                now_str = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
                sched_time_str = data.get("sched_time")
                if sched_time_str and ":" in sched_time_str:
                    now_date = datetime.now(timezone.utc).strftime("%Y-%m-%d")
                    sched_full = f"{now_date} {sched_time_str}:00"
                else:
                    sched_full = now_str

                note_str = "Lấy thuốc sớm từ ESP32" if is_early else "Uống thuốc theo cữ từ ESP32"
                pending = await dose_log_repo.create_pending(
                    db,
                    device_id=DEVICE_ID,
                    slot_number=slot,
                    scheduled_time=sched_full,
                    trigger_source=trigger_src,
                    schedule_id=None,
                    notes=note_str,
                )
                log = await dose_log_repo.mark_taken(
                    db, pending["id"], trigger_source=trigger_src, status=status_val
                )
                await self._decrement_stock(db, slot)
                target_slot = slot
                logger.info(f"[DISPENSE] {status_val} ({trigger_src}) từ ESP32: Slot {slot}, đã trừ tồn kho Database.")
            else:
                log = None
                target_slot = slot

        # Phát sóng WebSocket
        await ws_manager.broadcast(WS_TAKEN, {**(log or {}), **device_state.to_dict()})

        # Lưu ý: Không tự ý gửi cmd/home tại đây để ESP32 tự động xoay ngăn tiếp theo
        # nếu cữ thuốc còn ngăn; ESP32 sẽ tự động hồi vị về Slot 0 khi hết cữ.
        logger.info(
            f"[DISPENSE] TAKEN (nút vật lý): slot={target_slot} — đã cập nhật DB."
        )

    async def on_missed(self, data: dict) -> None:
        """
        Callback: Nhận cảnh báo bỏ lỡ cữ thuốc từ ESP32 (quá 15 phút không uống).
        Ghi nhận trạng thái MISSED vào dose_logs.
        """
        session = await self._clear_session_and_cancel_watchdog()
        slot = int(data.get("slot") or (session.slot_number if session else 0))
        now_str = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")

        sched_time_str = data.get("sched_time")
        if sched_time_str and ":" in sched_time_str:
            now_date = datetime.now(timezone.utc).strftime("%Y-%m-%d")
            sched_full = f"{now_date} {sched_time_str}:00"
        else:
            sched_full = now_str

        async with get_db_context() as db:
            if session:
                await dose_log_repo.mark_missed(
                    db, session.dose_log_id,
                    notes="Quá 15 phút không uống (Timeout từ ESP32)"
                )
                target_slot = session.slot_number
            elif 1 <= slot <= 7:
                pending = await dose_log_repo.create_pending(
                    db,
                    device_id=DEVICE_ID,
                    slot_number=slot,
                    scheduled_time=sched_full,
                    trigger_source="SCHEDULED",
                    notes="Quá 15 phút không lấy thuốc (Timeout ESP32)",
                )
                await dose_log_repo.mark_missed(
                    db, pending["id"],
                    notes="Quá 15 phút không lấy thuốc (Timeout ESP32)"
                )
                target_slot = slot
            else:
                target_slot = 0

        await ws_manager.broadcast(WS_MISSED, {
            "slot_number": target_slot,
            "status": "MISSED",
            "reason": "Quá 15 phút không uống thuốc (Timeout ESP32)",
        })
        logger.warning(f"[DISPENSE] Đã ghi nhận MISSED cho Ngăn {target_slot} từ ESP32")

    async def on_home_reached(self, data: dict) -> None:
        """
        Callback: ESP32 đã về Slot 0 (HOME_REACHED event).
        Phiên đã hoàn tất, FSM về SAFE_HOME_IDLE.
        """
        # Device state đã được cập nhật bởi mqtt_client._handle_home_reached()
        # Không cần làm gì thêm — phiên đã được xóa trước khi gửi home_cmd
        logger.info("[DISPENSE] Khay đã về Slot 0 — sẵn sàng cho lần tiếp theo.")

    async def on_error(self, data: dict) -> None:
        """
        Callback: ESP32 báo lỗi (ERROR event).
        Nếu có phiên → đánh dấu JAMMED.
        """
        session = await self._clear_session_and_cancel_watchdog()
        if not session:
            return

        code = data.get("code", "UNKNOWN")
        async with get_db_context() as db:
            await dose_log_repo.mark_jammed(
                db, session.dose_log_id,
                notes=f"ESP32 error: {code} — {data.get('message', '')}"
            )

        await ws_manager.broadcast(WS_JAMMED, {"dose_log_id": session.dose_log_id, **data})
        logger.error(f"[DISPENSE] JAMMED: log_id={session.dose_log_id}, code={code}")

    # ── API-triggered actions ─────────────────────────────────────────────────
    async def confirm_taken_manual(self) -> dict:
        """
        Xác nhận qua Web UI (thay thế nút vật lý).
        Dùng khi người dùng đã lấy thuốc nhưng không bấm nút.
        """
        session = await self._clear_session_and_cancel_watchdog()
        if not session:
            if device_state.fsm_state in ("WAITING_CONFIRM", "WAITING_TAKEN"):
                from device.mqtt_client import mqtt_client
                await mqtt_client.publish(
                    topic_cmd(DEVICE_ID, "confirm_taken"),
                    {"command": "CONFIRM_TAKEN"}
                )
                return {"message": "Đã gửi xác nhận lấy thuốc xuống ESP32"}
            raise HTTPException(
                status_code=404,
                detail="Không có phiên cấp thuốc nào đang hoạt động."
            )

        async with get_db_context() as db:
            log = await dose_log_repo.mark_taken(
                db, session.dose_log_id,
                trigger_source="EARLY_MANUAL",
                notes="Xác nhận qua Web UI"
            )
            await self._decrement_stock(db, session.slot_number)

        await ws_manager.broadcast(WS_TAKEN, {**(log or {}), **device_state.to_dict()})

        from device.mqtt_client import mqtt_client
        await mqtt_client.publish(
            topic_cmd(DEVICE_ID, "confirm_taken"),
            {"command": "CONFIRM_TAKEN"}
        )

        logger.info(f"[DISPENSE] TAKEN (Web confirm): log_id={session.dose_log_id}")
        return log

    async def return_home_now(self) -> None:
        """
        Hồi vị khẩn cấp từ Web UI.
        Nếu có phiên đang chạy → đánh dấu MISSED (tồn kho KHÔNG thay đổi).
        """
        session = await self._clear_session_and_cancel_watchdog()

        if session and session.status in ("PENDING", "DISPENSED"):
            async with get_db_context() as db:
                await dose_log_repo.mark_missed(
                    db, session.dose_log_id,
                    notes="Hồi vị khẩn cấp từ Web UI (EMERGENCY_HOME)"
                )
            logger.warning(
                f"[DISPENSE] MISSED (emergency home): log_id={session.dose_log_id}"
            )

        from device.mqtt_client import mqtt_client
        await mqtt_client.publish(topic_cmd(DEVICE_ID, "home"), build_home_cmd())
        logger.info("[DISPENSE] Lệnh RETURN_HOME_ZERO đã gửi.")

    async def start_calibration(self) -> None:
        """
        Kích hoạt Auto-Homing (TCRT5000) từ Web UI hoặc khi khởi động.
        ESP32 sẽ rà tìm vạch phản xạ và về Slot 0.
        """
        if not device_state.is_online:
            raise HTTPException(status_code=503, detail="ESP32 chưa kết nối.")

        from device.mqtt_client import mqtt_client
        cmd = build_calibrate_cmd()
        await mqtt_client.publish(topic_cmd(DEVICE_ID, "calibrate"), cmd)
        logger.info("[DISPENSE] Lệnh CALIBRATE (Auto-Homing) đã gửi.")

    # ── Watchdog ──────────────────────────────────────────────────────────────
    def _start_watchdog(self, log_id: int) -> None:
        """Bắt đầu đếm ngược timeout — tự động MISSED nếu hết giờ."""
        self._cancel_watchdog()
        self._watchdog_task = asyncio.create_task(
            self._watchdog_timeout(log_id),
            name=f"watchdog-{log_id}"
        )

    def _cancel_watchdog(self) -> None:
        if self._watchdog_task and not self._watchdog_task.done():
            self._watchdog_task.cancel()
        self._watchdog_task = None

    async def _watchdog_timeout(self, log_id: int) -> None:
        """Chạy sau DISPENSE_TIMEOUT_SECONDS — tự động kết thúc phiên là MISSED."""
        try:
            await asyncio.sleep(settings.DISPENSE_TIMEOUT_SECONDS)
        except asyncio.CancelledError:
            return

        async with self._lock:
            if not self._session or self._session.dose_log_id != log_id:
                return
            session = self._session
            self._session = None
        # Watchdog đã cancel chính nó nên không cần cancel lại

        async with get_db_context() as db:
            await dose_log_repo.mark_missed(
                db, log_id,
                notes=f"Watchdog timeout {settings.DISPENSE_TIMEOUT_SECONDS}s"
            )

        await ws_manager.broadcast(WS_MISSED, {
            "dose_log_id": log_id,
            "slot_number": session.slot_number,
            "reason": f"Watchdog timeout {settings.DISPENSE_TIMEOUT_SECONDS}s",
        })

        from device.mqtt_client import mqtt_client
        await mqtt_client.publish(topic_cmd(DEVICE_ID, "home"), build_home_cmd())

        logger.warning(
            f"[WATCHDOG] Phiên log_id={log_id} MISSED — timeout "
            f"{settings.DISPENSE_TIMEOUT_SECONDS}s, slot={session.slot_number}"
        )

    # ── Internal helpers ──────────────────────────────────────────────────────
    async def _clear_session_and_cancel_watchdog(self) -> ActiveSession | None:
        """Thread-safe: xóa session và hủy watchdog. Trả về session cũ."""
        async with self._lock:
            session = self._session
            self._session = None
        self._cancel_watchdog()
        return session

    @staticmethod
    async def _decrement_stock(db, slot_number: int) -> None:
        """
        Trừ 1 viên tồn kho khỏi slot sau khi TAKEN.
        Đảm bảo current_quantity không xuống dưới 0.
        """
        slot = await slot_repo.get_slot(db, DEVICE_ID, slot_number)
        if not slot:
            logger.warning(f"_decrement_stock: slot {slot_number} không tồn tại")
            return
        current = slot.get("current_quantity", 0)
        new_qty = max(0, current - 1)
        await slot_repo.update_slot(db, DEVICE_ID, slot_number, {"current_quantity": new_qty})
        logger.info(f"Tồn kho slot {slot_number}: {current} → {new_qty}")


# ── Singleton toàn cục ────────────────────────────────────────────────────────
dispensing_engine = DispensingEngine()
