"""
services/scheduler_engine.py
──────────────────────────────
Scheduler Engine — Quét lịch uống thuốc mỗi phút và kích hoạt cấp thuốc tự động.

Hai nhiệm vụ chính:
  1. MINUTE SCANNER — Chạy mỗi 60 giây, quét bảng schedules
     tìm các cữ trùng với giờ hiện tại, đưa vào hàng chờ.

  2. QUEUE CONSUMER — Xử lý hàng chờ tuần tự, chờ dispensing_engine
     rảnh rồi mới dispatch lệnh tiếp theo.

Bảo vệ chống trùng:
  - exists_today(): mỗi schedule chỉ được cấp 1 lần/ngày
  - Nếu trùng thời gian nhiều slot: xếp hàng chờ, xử lý lần lượt

Startup Recovery:
  - _recover_orphans(): khi server khởi động, tìm dose_log còn PENDING/DISPENSED
    → đánh dấu MISSED (do server crash trước đó)

QUAN TRỌNG:
  - Sử dụng giờ LOCAL (Việt Nam UTC+7) để so với lịch HH:MM của người dùng
  - Không cần install thêm thư viện (chỉ dùng asyncio + stdlib)
"""

import asyncio
import logging
from datetime import datetime
from zoneinfo import ZoneInfo

from config import settings
from database.connection import get_db_context
from database.repositories import dose_log_repo, schedule_repo
from services.dispensing_engine import dispensing_engine
from ws_manager import ws_manager, WS_MISSED
from device.mqtt_client import mqtt_client
from device.protocol import topic_cmd
from device.device_state import device_state

logger = logging.getLogger(__name__)

DEVICE_ID = settings.DEVICE_ID

# Múi giờ Việt Nam (UTC+7) — dùng để so sánh time_of_day HH:MM
_VN_TZ = ZoneInfo("Asia/Ho_Chi_Minh")

# Giới hạn tối đa item trong queue (tránh tràn bộ nhớ nếu scheduler chạy lâu)
_QUEUE_MAX = 20


class SchedulerEngine:
    """Singleton quét lịch mỗi phút và điều phối phiên cấp thuốc tự động."""

    def __init__(self):
        self._running: bool = False
        self._scanner_task: asyncio.Task | None = None
        self._consumer_task: asyncio.Task | None = None
        self._queue: asyncio.Queue = asyncio.Queue(maxsize=_QUEUE_MAX)

    # ── Public API ─────────────────────────────────────────────────────────────
    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def queue_size(self) -> int:
        return self._queue.qsize()

    async def start(self) -> None:
        """
        Khởi động Scheduler Engine.
        Gọi từ main.py lifespan STARTUP.
        """
        logger.info("[SCHEDULER] Đang khởi động...")

        # 1. Recovery: đánh dấu MISSED các log còn treo từ lần chạy trước
        await self._recover_orphans()

        # 2. Bắt đầu vòng quét mỗi phút
        self._scanner_task = asyncio.create_task(
            self._minute_scanner(),
            name="scheduler-scanner"
        )

        # 3. Bắt đầu consumer xử lý hàng chờ tuần tự
        self._consumer_task = asyncio.create_task(
            self._queue_consumer(),
            name="scheduler-consumer"
        )

        self._running = True
        logger.info("[SCHEDULER] Đã khởi động. Đang quét lịch mỗi phút.")

    async def stop(self) -> None:
        """
        Tắt Scheduler Engine.
        Gọi từ main.py lifespan SHUTDOWN.
        """
        self._running = False

        for task in (self._scanner_task, self._consumer_task):
            if task and not task.done():
                task.cancel()
                try:
                    await task
                except asyncio.CancelledError:
                    pass

        logger.info("[SCHEDULER] Đã dừng.")

    # ── Minute Scanner ─────────────────────────────────────────────────────────
    async def _minute_scanner(self) -> None:
        """
        Vòng lặp chính — chạy mỗi 60 giây.

        Thuật toán mỗi vòng:
          1. Lấy giờ hiện tại dạng HH:MM (múi giờ VN)
          2. Query schedules có time_of_day = HH:MM VÀ is_active = 1
          3. Với mỗi schedule: kiểm tra exists_today() → nếu chưa → enqueue
        """
        # Chờ đến đầu phút tiếp theo để scan đúng lúc
        await self._sleep_until_next_minute()

        while True:
            try:
                now_vn = datetime.now(_VN_TZ)
                current_time = now_vn.strftime("%H:%M")
                today_date  = now_vn.strftime("%Y-%m-%d")

                logger.debug(f"[SCHEDULER] Quét lịch lúc {current_time} ({today_date})")

                async with get_db_context() as db:
                    due_list = await schedule_repo.get_active_by_time(
                        db, DEVICE_ID, current_time
                    )
                    logger.debug(f"[SCHEDULER] Tìm thấy {len(due_list)} lịch tại {current_time}")

                    valid_schedules = []
                    for schedule in due_list:
                        already = await dose_log_repo.exists_today(
                            db, schedule["id"], today_date
                        )
                        if already:
                            logger.debug(
                                f"[SCHEDULER] Schedule id={schedule['id']} "
                                f"(slot {schedule['slot_number']}) đã cấp hôm nay → bỏ qua."
                            )
                            continue
                        valid_schedules.append(schedule)

                    if valid_schedules:
                        combined_mask = 0
                        slots = []
                        sched_ids = []
                        labels = []
                        med_names = []
                        for s in valid_schedules:
                            s_num = int(s["slot_number"])
                            combined_mask |= (1 << (s_num - 1))
                            slots.append(s_num)
                            sched_ids.append(s["id"])
                            if s.get("label"):
                                labels.append(s["label"])
                            if s.get("medicine_name"):
                                med_names.append(f"N{s_num}:{s['medicine_name']}")

                        scheduled_time = f"{today_date} {current_time}:00"
                        job = {
                            "slot_number": slots[0],
                            "slots": slots,
                            "mask": combined_mask,
                            "schedule_id": sched_ids[0],
                            "schedule_ids": sched_ids,
                            "scheduled_time": scheduled_time,
                            "trigger_source": "AUTO_SCHEDULE",
                            "schedule_label": ", ".join(dict.fromkeys(labels)),
                            "medicine_name": ", ".join(med_names),
                        }

                        try:
                            self._queue.put_nowait(job)
                            logger.info(
                                f"[SCHEDULER] Đã xếp hàng cữ thuốc gộp: mask=0x{combined_mask:02X}, "
                                f"slots={slots}, schedules={sched_ids}"
                            )
                        except asyncio.QueueFull:
                            logger.warning(
                                f"[SCHEDULER] Hàng chờ đầy ({_QUEUE_MAX} items) "
                                f"— bỏ qua mốc giờ {current_time}"
                            )

            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"[SCHEDULER] Lỗi vòng quét: {e}", exc_info=True)

            # Chờ đến đầu phút tiếp theo
            await self._sleep_until_next_minute()

    # ── Queue Consumer ─────────────────────────────────────────────────────────
    async def _queue_consumer(self) -> None:
        """
        Xử lý hàng chờ dispatch tuần tự.
        Chờ dispensing_engine rảnh trước khi gửi lệnh tiếp theo.
        """
        while True:
            try:
                # Lấy job tiếp theo từ hàng chờ (block đến khi có)
                job = await self._queue.get()

                # Chờ cho đến khi engine rảnh (tối đa ~5 phút)
                wait_seconds = 0
                max_wait = 300  # 5 phút
                while dispensing_engine.has_active_session:
                    if wait_seconds >= max_wait:
                        logger.warning(
                            f"[SCHEDULER] Chờ quá {max_wait}s — bỏ job slot={job['slot_number']}"
                        )
                        self._queue.task_done()
                        break
                    await asyncio.sleep(5)
                    wait_seconds += 5
                else:
                    # Engine rảnh → dispatch
                    await self._dispatch_job(job)
                    self._queue.task_done()

            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"[SCHEDULER] Lỗi queue consumer: {e}", exc_info=True)

    async def _dispatch_job(self, job: dict) -> None:
        """
        Gửi lệnh cấp thuốc cho 1 job trong hàng chờ.
        Xử lý mọi HTTPException để tránh crash consumer.
        """
        mask = job.get("mask") or (1 << (job["slot_number"] - 1))
        slot = job.get("slot_number", 1)
        slots = job.get("slots", [slot])
        schedule_ids = job.get("schedule_ids", [job.get("schedule_id")])
        label = job.get("schedule_label", "")
        medicine = job.get("medicine_name", "")

        logger.info(
            f"[SCHEDULER] Dispatch cữ thuốc gộp: mask=0x{mask:02X}, slots={slots}, "
            f"schedule_ids={schedule_ids}, label={label!r}, thuốc={medicine!r}"
        )

        try:
            # Gửi lệnh báo cữ thuốc (DUE_ALERT) với mask gộp đầy đủ cho tất cả các ngăn trong cữ
            await mqtt_client.publish(
                topic_cmd(DEVICE_ID, "due_alert"),
                {
                    "command": "DUE_ALERT",
                    "slot": slot,
                    "target_slot": slot,
                    "slots": slots,
                    "mask": mask,
                    "schedule_ids": schedule_ids,
                    "trigger_source": "AUTO_SCHEDULE"
                }
            )
            device_state.update_fsm("ALERTING_DUE")
            await ws_manager.broadcast("dose_due", {
                "slot_number": slot,
                "slots": slots,
                "mask": mask,
                "schedule_ids": schedule_ids,
                "label": label,
                "medicine_name": medicine,
                "timeout_seconds": 900,
                **device_state.to_dict()
            })
            logger.info(
                f"[SCHEDULER] Đã kích hoạt báo cữ thuốc (15 phút chờ xác nhận): "
                f"mask=0x{mask:02X}, slots={slots}"
            )
        except Exception as e:
            detail = getattr(e, "detail", str(e))
            logger.warning(
                f"[SCHEDULER] Không thể phát cữ thuốc mask=0x{mask:02X}: {detail}"
            )

    # ── Orphan Recovery ────────────────────────────────────────────────────────
    async def _recover_orphans(self) -> None:
        """
        Khi server khởi động: tìm dose_logs còn PENDING/DISPENSED
        (do server crash trong phiên trước) → đánh dấu MISSED.

        Không gửi lệnh home vì không chắc ESP32 đang ở đâu.
        ESP32 sẽ tự homing khi kết nối lại (qua LWT/STATUS event).
        """
        async with get_db_context() as db:
            orphans = await dose_log_repo.get_orphaned(db, DEVICE_ID)

            if not orphans:
                logger.info("[SCHEDULER] Orphan recovery: không có log bị treo.")
                return

            logger.warning(
                f"[SCHEDULER] Orphan recovery: tìm thấy {len(orphans)} "
                f"log bị treo — đánh dấu MISSED."
            )

            for log in orphans:
                await dose_log_repo.mark_missed(
                    db,
                    log["id"],
                    notes="Orphan recovery — server restart/crash"
                )
                await ws_manager.broadcast(WS_MISSED, {
                    "dose_log_id": log["id"],
                    "slot_number": log["slot_number"],
                    "reason": "Server restart — orphan recovery",
                })
                logger.info(
                    f"  → dose_log id={log['id']} "
                    f"(slot={log['slot_number']}, status trước={log['status']}) → MISSED"
                )

    # ── Helpers ────────────────────────────────────────────────────────────────
    @staticmethod
    async def _sleep_until_next_minute() -> None:
        """
        Ngủ đến đầu phút tiếp theo (xx:yy:00).
        Giúp scanner luôn chạy đúng vào giây đầu tiên của mỗi phút.
        """
        now = datetime.now(_VN_TZ)
        seconds_remaining = 60 - now.second
        await asyncio.sleep(seconds_remaining)


# ── Singleton toàn cục ────────────────────────────────────────────────────────
scheduler_engine = SchedulerEngine()
