"""
device/mqtt_client.py
──────────────────────
MQTT Client bất đồng bộ — kết nối Backend với ESP32 qua Broker.

Kiến trúc:
  - Chạy như một background asyncio task suốt vòng đời của ứng dụng
  - Tự động kết nối lại khi mất kết nối (exponential backoff)
  - Subscribe nhận mọi event từ ESP32
  - Cung cấp hàm publish() để các service gửi lệnh xuống ESP32

Lifecycle:
  - Khởi động: await mqtt_client.start()   (gọi trong lifespan startup)
  - Tắt:       await mqtt_client.stop()    (gọi trong lifespan shutdown)

Callback hooks (sẽ được gắn vào từ Task 06 và Task 07):
  - on_slot_reached   — Dispensing service xử lý queue
  - on_home_reached   — Dispensing service kết thúc phiên
  - on_taken_button   — Dispensing service xác nhận
  - on_device_online  — WebSocket broadcast trạng thái
  - on_device_offline — WebSocket broadcast trạng thái
"""

import asyncio
import json
import logging
from datetime import datetime, timezone
from typing import Callable, Awaitable

import aiomqtt

from config import settings
from device.device_state import device_state
from device.protocol import (
    topic_events,
    topic_state,
    topic_lwt,
    build_online_event,
    build_offline_lwt,
    parse_event,
)
from ws_manager import (
    ws_manager,
    WS_DEVICE_ONLINE, WS_DEVICE_OFFLINE, WS_DEVICE_STATUS,
    WS_TELEMETRY, WS_SLOT_REACHED, WS_HOME_REACHED,
    WS_JAMMED, WS_ERROR, WS_SCHEDULE_UPDATED,
)

logger = logging.getLogger(__name__)

# Type alias cho async callback hook
AsyncCallback = Callable[[dict], Awaitable[None]]


class MQTTClient:
    """
    MQTT Client singleton — quản lý kết nối và giao tiếp với ESP32.
    """

    def __init__(self):
        self._client: aiomqtt.Client | None = None
        self._running: bool = False
        self._task: asyncio.Task | None = None

        # ── Callback hooks — các service sẽ gắn hàm vào đây (Task 06, 07) ──
        self.on_slot_reached:   AsyncCallback | None = None
        self.on_home_reached:   AsyncCallback | None = None
        self.on_taken_button:   AsyncCallback | None = None
        self.on_device_online:  AsyncCallback | None = None
        self.on_device_offline: AsyncCallback | None = None
        self.on_error:          AsyncCallback | None = None

    # ── Lifecycle ─────────────────────────────────────────────────────────────
    async def start(self) -> None:
        """Khởi động MQTT background task. Gọi trong lifespan startup."""
        self._running = True
        self._task = asyncio.create_task(self._run(), name="mqtt-client")
        logger.info(f"MQTT client task started (broker: {settings.MQTT_BROKER}:{settings.MQTT_PORT})")

    async def stop(self) -> None:
        """Dừng MQTT background task. Gọi trong lifespan shutdown."""
        self._running = False
        if self._task and not self._task.done():
            self._task.cancel()
            try:
                await self._task
            except asyncio.CancelledError:
                pass
        logger.info("MQTT client stopped.")

    # ── Gửi lệnh xuống ESP32 ─────────────────────────────────────────────────
    async def publish(self, topic: str, payload: dict | str, qos: int = 1) -> bool:
        """
        Gửi message MQTT tới ESP32.

        Trả về True nếu gửi thành công, False nếu chưa kết nối.
        Caller nên kiểm tra device_state.is_online trước khi gọi.
        """
        if self._client is None:
            logger.warning(f"Cannot publish to '{topic}' — MQTT not connected.")
            return False
        try:
            if isinstance(payload, dict):
                payload = json.dumps(payload, ensure_ascii=False)
            await self._client.publish(topic, payload, qos=qos)
            logger.debug(f"Published → {topic}: {payload[:120]}")
            return True
        except Exception as e:
            logger.error(f"Publish failed [{topic}]: {e}")
            return False

    async def publish_sync_schedules(self, schedules: list[dict]) -> bool:
        """
        Đồng bộ toàn bộ bảng lịch cữ xuống phần cứng ESP32 qua MQTT.
        Gom nhóm theo mốc giờ 'HH:MM' thành danh sách {h, m, mask} chuẩn cấu trúc C++ Sched.
        Gói tin được tinh giản siêu gọn (loại bỏ trường text dài) chống tràn buffer ESP32.
        """
        time_groups: dict[str, int] = {}
        time_active_groups: dict[str, int] = {}
        details = []

        for s in schedules:
            t = s.get("time_of_day") or s.get("time")
            if not t:
                continue
            slot = int(s.get("slot_number", 0) or s.get("slot", 0))
            if slot < 1 or slot > 7:
                continue

            # Bitmask: bit 0 = Slot 1, bit 1 = Slot 2, ..., bit 6 = Slot 7
            bit = (1 << (slot - 1))
            time_groups[t] = time_groups.get(t, 0) | bit
            if s.get("is_active", True):
                time_active_groups[t] = time_active_groups.get(t, 0) | bit
            details.append({
                "slot": slot,
                "time": t,
                "dose": int(s.get("dose_quantity", 1) or s.get("dose", 1)),
                "is_active": bool(s.get("is_active", True))
            })

        items = []
        for t_str, mask in sorted(time_groups.items()):
            parts = t_str.split(":")
            h, m = int(parts[0]), int(parts[1])
            act_mask = time_active_groups.get(t_str, 0)
            items.append({"h": h, "m": m, "mask": mask, "active_mask": act_mask})

        payload = {
            "command": "SYNC_SCHEDULES",
            "device_id": settings.DEVICE_ID,
            "items": items,          # Cấu trúc C++ Sched {h, m, mask} trên ESP32
            "schedules": details,    # Chi tiết {slot, time, dose, is_active}
            "timestamp": int(datetime.now(timezone.utc).timestamp()),
        }
        topic = f"pillbox/{settings.DEVICE_ID}/cmd/sync_schedules"
        logger.info(f"[MQTT] Gửi đồng bộ {len(items)} mốc giờ (tổng {len(details)} cữ) xuống ESP32...")
        return await self.publish(topic, payload, qos=1)

    async def publish_sync_time(self) -> bool:
        """Gửi lệnh đồng bộ thời gian thực từ Backend xuống ESP32."""
        now = datetime.now()
        payload = {
            "cmd": "SYNC_TIME",
            "hour": now.hour,
            "minute": now.minute,
            "second": now.second,
            "wday": now.weekday(),  # 0=Thứ 2 .. 6=Chủ nhật
            "epoch": int(now.timestamp()),
        }
        topic = f"pillbox/{settings.DEVICE_ID}/cmd/sync_time"
        logger.info(f"[MQTT] Gửi đồng bộ thời gian ({now.strftime('%H:%M:%S')}) xuống ESP32...")
        return await self.publish(topic, payload, qos=1)

    # ── Background task chính ────────────────────────────────────────────────
    async def _run(self) -> None:
        """
        Vòng lặp kết nối + lắng nghe message. Tự động kết nối lại khi lỗi.
        """
        reconnect_delay = 2  # giây, tăng dần theo exponential backoff

        while self._running:
            try:
                logger.info(f"Đang kết nối MQTT Broker {settings.MQTT_BROKER}:{settings.MQTT_PORT}...")

                # LWT — tin nhắn tự động gửi khi backend mất kết nối đột ngột
                will = aiomqtt.Will(
                    topic=topic_lwt(settings.DEVICE_ID),
                    payload=build_offline_lwt(),
                    qos=1,
                    retain=True,
                )

                async with aiomqtt.Client(
                    hostname=settings.MQTT_BROKER,
                    port=settings.MQTT_PORT,
                    identifier=settings.MQTT_CLIENT_ID,
                    username=settings.MQTT_USERNAME or None,
                    password=settings.MQTT_PASSWORD or None,
                    will=will,
                ) as client:
                    self._client = client
                    reconnect_delay = 2  # reset khi kết nối thành công

                    # Subscribe nhận tất cả event và state từ thiết bị
                    await client.subscribe(topic_events(settings.DEVICE_ID), qos=1)
                    await client.subscribe(topic_state(settings.DEVICE_ID), qos=1)
                    # Subscribe LWT topic (phát hiện ESP32 offline)
                    await client.subscribe(topic_lwt(settings.DEVICE_ID), qos=1)

                    logger.info("MQTT kết nối thành công. Đang lắng nghe events...")

                    # Lắng nghe message từ Broker
                    async for message in client.messages:
                        if not self._running:
                            break
                        topic_str = str(message.topic)
                        await self._dispatch(topic_str, message.payload)

            except aiomqtt.MqttError as e:
                self._client = None
                device_state.set_offline()
                logger.warning(f"MQTT lỗi kết nối: {e}. Thử lại sau {reconnect_delay}s...")

                # Broadcast offline qua WebSocket (nếu ws_manager đã sẵn sàng)
                await self._fire(self.on_device_offline, {"status": "OFFLINE"})

                await asyncio.sleep(reconnect_delay)
                reconnect_delay = min(reconnect_delay * 2, 60)  # tối đa 60s

            except asyncio.CancelledError:
                logger.info("MQTT task bị hủy.")
                break
            except Exception as e:
                self._client = None
                logger.error(f"MQTT unexpected error: {e}", exc_info=True)
                await asyncio.sleep(reconnect_delay)

        self._client = None
        logger.info("MQTT background task đã kết thúc.")

    # ── Điều phối message nhận được ───────────────────────────────────────────
    async def _dispatch(self, topic: str, raw_payload: bytes) -> None:
        """
        Phân tích topic và payload, gọi handler tương ứng.
        """
        # LWT — ESP32 mất kết nối đột ngột
        if topic == topic_lwt(settings.DEVICE_ID):
            data = parse_event(raw_payload) or {}
            await self._handle_lwt(data)
            return

        # Events từ ESP32 — topic dạng: pillbox/{id}/events/{event_type}
        data = parse_event(raw_payload)
        if data is None:
            logger.warning(f"Payload JSON không hợp lệ từ {topic}: {raw_payload[:80]}")
            return

        event_type = data.get("event", "").upper()
        if not event_type:
            if topic.endswith("/telemetry"):
                event_type = "TELEMETRY"
            elif topic.endswith("/status"):
                event_type = "STATUS"
            elif topic.endswith("/schedule") or topic.endswith("/schedule_create"):
                event_type = "SCHEDULE_CREATE"
            elif topic.endswith("/taken"):
                event_type = "TAKEN_BUTTON_PRESSED"
            elif topic.endswith("/dispense"):
                event_type = "SLOT_REACHED"
            elif topic.endswith("/alert"):
                event_type = "ERROR"
            elif topic.endswith("/home") or topic.endswith("/home_reached"):
                event_type = "HOME_REACHED"
            elif topic.endswith("/due_alert"):
                event_type = "DUE_ALERT"
            elif topic.endswith("/missed"):
                event_type = "MISSED"
            elif topic.endswith("/refill"):
                event_type = "REFILL_READY"
            elif topic.endswith("/slot_cleared"):
                event_type = "SLOT_CLEARED"
            elif topic.endswith("/sync_slots"):
                event_type = "SYNC_SLOTS"

        logger.debug(f"Event nhận được [{topic}]: {event_type}")

        if event_type == "SLOT_REACHED":
            await self._handle_slot_reached(data)
        elif event_type == "HOME_REACHED":
            await self._handle_home_reached(data)
        elif event_type in ("TAKEN_BUTTON_PRESSED", "TAKEN"):
            await self._handle_taken_button(data)
        elif event_type == "STATUS":
            await self._handle_status(data)
        elif event_type == "TELEMETRY":
            await self._handle_telemetry(data)
        elif event_type in ("SCHEDULE_CREATE", "SCHEDULE_CREATED", "SCHEDULE_SAVED", "SYNC_SCHEDULES") or topic.endswith("/events/schedule") or topic.endswith("/events/sync_schedules"):
            await self._handle_schedule_create(data)
        elif event_type in ("ERROR", "ALERT"):
            await self._handle_error(data)
        elif event_type == "REFILL_READY":
            slot = int(data.get("slot", 0))
            device_state.update_slot(slot)
            device_state.update_fsm("REFILL_MODE")
            logger.info(f"REFILL_READY: Khay sẵn sàng nạp tại Slot {slot}")
            await ws_manager.broadcast("refill_ready", {**data, **device_state.to_dict()})
        elif event_type == "SLOT_CLEARED":
            slot = int(data.get("slot", 0))
            logger.info(f"SLOT_CLEARED từ ESP32: Ngăn {slot} đã được lấy hết thuốc và xóa toàn bộ lịch!")
            try:
                from database.connection import get_db_context
                from database.repositories import slot_repo, schedule_repo
                from config import DEVICE_ID
                async with get_db_context() as db:
                    if slot > 0:
                        # 1. Xóa thông tin thuốc và tồn kho của ngăn
                        await slot_repo.update_slot(db, DEVICE_ID, slot, {"medicine_id": None, "current_quantity": 0})
                        # 2. Xóa toàn bộ các lịch liên quan đến ngăn này
                        del_cnt = await schedule_repo.delete_by_slot(db, DEVICE_ID, slot)
                        logger.info(f"Đã đồng bộ xóa {del_cnt} lịch của Ngăn {slot} trong database.")
            except Exception as e:
                logger.error(f"Lỗi cập nhật DB khi SLOT_CLEARED: {e}")
            device_state.update_slot(0)
            device_state.update_fsm("SAFE_HOME_IDLE")
            await ws_manager.broadcast("slot_cleared", {"slot_number": slot, **device_state.to_dict()})
            await ws_manager.broadcast("schedule_updated", {"slot_number": slot, "deleted": True})
            await ws_manager.broadcast("home_reached", {"slot": 0, **device_state.to_dict()})
        elif event_type == "REFILL_DONE" or (topic.endswith("/refill") and "qty" in data):
            slot = int(data.get("slot", 0))
            qty = data.get("qty")
            added = data.get("added", 0)
            logger.info(f"REFILL_DONE từ ESP32: Ngăn {slot} đã nạp +{added} viên (Tổng mới: {qty} viên)")
            try:
                from database.connection import get_db_context
                from database.repositories import slot_repo
                from config import DEVICE_ID
                async with get_db_context() as db:
                    if qty is not None and slot > 0:
                        cur_slot = await slot_repo.get_slot(db, DEVICE_ID, slot)
                        cur_cap = cur_slot.get("capacity", 1000) if cur_slot else 1000
                        update_payload = {"current_quantity": int(qty)}
                        if int(qty) > cur_cap:
                            update_payload["capacity"] = int(qty)
                        await slot_repo.update_slot(db, DEVICE_ID, slot, update_payload)
            except Exception as e:
                logger.error(f"Lỗi cập nhật DB khi REFILL_DONE: {e}")
            device_state.update_slot(0)
            device_state.update_fsm("SAFE_HOME_IDLE")
            await ws_manager.broadcast("refill_done", {**data, **device_state.to_dict()})
            await ws_manager.broadcast("slot_updated", {"slot_number": slot, "current_quantity": int(qty) if qty is not None else 0, "added": added})
            await ws_manager.broadcast("home_reached", {"slot": 0, **device_state.to_dict()})
        elif event_type == "SYNC_SLOTS" or topic.endswith("/events/sync_slots"):
            await self._handle_sync_slots(data)
        elif event_type == "MISSED":
            logger.warning(f"MISSED DOSE nhận từ ESP32: {data}")
            from services.dispensing_engine import dispensing_engine
            await dispensing_engine.on_missed(data)
        elif event_type == "DUE_ALERT":
            logger.info(f"DUE_ALERT nhận từ ESP32: {data}")
            await ws_manager.broadcast("dose_due", {**data, **device_state.to_dict()})
        else:
            logger.debug(f"Event không xác định: {event_type}")

    # ── Event handlers ────────────────────────────────────────────────────────
    async def _handle_sync_slots(self, data: dict) -> None:
        """Đồng bộ toàn bộ danh sách ngăn thuốc từ ESP32 lên Backend sau khi kết nối lại."""
        slots_list = data.get("slots", [])
        if not slots_list:
            return
        logger.info(f"[MQTT SYNC] Nhận dữ liệu đồng bộ tồn kho {len(slots_list)} ngăn từ ESP32...")
        try:
            from database.connection import get_db_context
            from database.repositories import slot_repo, medicine_repo
            from config import DEVICE_ID
            async with get_db_context() as db:
                all_meds = await medicine_repo.get_all(db)
                for s in slots_list:
                    slot_num = int(s.get("slot", 0))
                    if slot_num < 1 or slot_num > 7:
                        continue
                    name = str(s.get("name", "")).strip()
                    qty = int(s.get("qty", 0))
                    dose = int(s.get("dose", 1))

                    med_id = None
                    if name:
                        found = next((m for m in all_meds if m["name"].lower() == name.lower()), None)
                        if found:
                            med_id = found["id"]
                        else:
                            new_m = await medicine_repo.create(db, name=name, dosage_unit="viên")
                            med_id = new_m["id"]
                            all_meds.append(new_m)

                    cur_slot = await slot_repo.get_slot(db, DEVICE_ID, slot_num)
                    cur_cap = cur_slot.get("capacity", 1000) if cur_slot else 1000
                    update_payload = {
                        "current_quantity": qty,
                        "default_dose": dose,
                        "capacity": max(cur_cap, qty, 1000),
                    }
                    if med_id is not None:
                        update_payload["medicine_id"] = med_id
                    elif not name and qty == 0:
                        update_payload["medicine_id"] = None

                    await slot_repo.update_slot(db, DEVICE_ID, slot_num, update_payload)
            await ws_manager.broadcast("slot_updated", {"synced": True, "count": len(slots_list)})
            logger.info(f"[MQTT SYNC] Đã cập nhật xong tồn kho {len(slots_list)} ngăn từ ESP32 vào Database!")
        except Exception as e:
            logger.error(f"Lỗi khi xử lý SYNC_SLOTS từ ESP32: {e}")

    async def _handle_lwt(self, data: dict) -> None:
        """ESP32 mất kết nối (Last Will Testament)."""
        status = data.get("status", "OFFLINE")
        if status == "OFFLINE":
            device_state.set_offline()
            logger.warning("ESP32 mất kết nối (LWT OFFLINE).")
            await ws_manager.broadcast(WS_DEVICE_OFFLINE, device_state.to_dict())
            await self._fire(self.on_device_offline, {"status": "OFFLINE", "fsm_state": "OFFLINE"})
        elif status == "ONLINE":
            device_state.set_online()
            logger.info("ESP32 kết nối (LWT ONLINE).")
            await ws_manager.broadcast(WS_DEVICE_ONLINE, device_state.to_dict())
            await self._fire(self.on_device_online, device_state.to_dict())

    async def _handle_slot_reached(self, data: dict) -> None:
        """Khay đã đến vị trí slot đích — sẵn sàng để lấy thuốc."""
        slot = int(data.get("slot") if data.get("slot") is not None else data.get("target_slot", 0))
        device_state.update_slot(slot)
        device_state.update_fsm("WAITING_CONFIRM")
        logger.info(f"SLOT_REACHED: khay tại Slot {slot}")
        await ws_manager.broadcast(WS_SLOT_REACHED, {**data, **device_state.to_dict()})
        await self._fire(self.on_slot_reached, {**data, **device_state.to_dict()})

    async def _handle_home_reached(self, data: dict) -> None:
        """Khay đã về Slot 0 (Safe Home) thành công."""
        device_state.update_slot(0)
        device_state.update_fsm("SAFE_HOME_IDLE")
        logger.info("HOME_REACHED: khay đã về Slot 0.")
        await ws_manager.broadcast(WS_HOME_REACHED, {**data, **device_state.to_dict()})
        await self._fire(self.on_home_reached, {**data, **device_state.to_dict()})

    async def _handle_taken_button(self, data: dict) -> None:
        """Người dùng bấm nút vật lý để xác nhận đã lấy thuốc."""
        slot = data.get("slot", device_state.current_slot)
        logger.info(f"TAKEN_BUTTON_PRESSED: người dùng lấy thuốc tại Slot {slot}")
        await self._fire(self.on_taken_button, data)

    async def _handle_status(self, data: dict) -> None:
        """ESP32 báo cáo trạng thái tổng quát (STATUS event)."""
        status = data.get("status", "")
        state = data.get("state", "")
        slot = data.get("current_slot", device_state.current_slot)

        if status == "ONLINE":
            device_state.set_online()
            logger.info(f"ESP32 ONLINE — FSM: {state}, Slot: {slot}")
            await ws_manager.broadcast(WS_DEVICE_ONLINE, device_state.to_dict())
            await self._fire(self.on_device_online, device_state.to_dict())

            # Chỉ gửi đồng bộ thời gian thực xuống ESP32, KHÔNG gửi lịch xuống khi kết nối lại
            await self.publish_sync_time()
        elif status == "OFFLINE":
            device_state.set_offline()
            await ws_manager.broadcast(WS_DEVICE_OFFLINE, device_state.to_dict())
            await self._fire(self.on_device_offline, device_state.to_dict())
        else:
            if state:
                device_state.update_fsm(state)
            if slot is not None:
                device_state.update_slot(slot)

    async def _handle_telemetry(self, data: dict) -> None:
        """Cập nhật thông số kỹ thuật từ ESP32 (RSSI, RAM, firmware, nhiệt độ, độ ẩm...)."""
        if not device_state.is_online:
            device_state.set_online()
            await ws_manager.broadcast(WS_DEVICE_ONLINE, device_state.to_dict())

        raw_temp = data.get("temperature") if data.get("temperature") is not None else data.get("temp")
        raw_hum = data.get("humidity") if data.get("humidity") is not None else data.get("hum")
        temp_val = round(float(raw_temp)) if raw_temp is not None else None
        hum_val = round(float(raw_hum)) if raw_hum is not None else None

        heap_val = data.get("free_heap") if data.get("free_heap") is not None else data.get("heap")
        up_val = data.get("uptime") if data.get("uptime") is not None else data.get("up")
        cur_slot = data.get("current_slot") if data.get("current_slot") is not None else data.get("slot")
        cur_fsm = data.get("fsm_state") or data.get("fsm")

        device_state.update_telemetry(
            rssi=data.get("rssi"),
            free_heap=heap_val,
            firmware=data.get("firmware"),
            ip_address=data.get("ip") or data.get("ip_address"),
            uptime=up_val,
            temperature=temp_val,
            humidity=hum_val,
        )
        if cur_slot is not None:
            device_state.update_slot(int(cur_slot))
        if cur_fsm:
            device_state.update_fsm(str(cur_fsm))

        logger.debug(
            f"Telemetry updated: RSSI={data.get('rssi')}dBm, Heap={heap_val}B, "
            f"Temp={temp_val}°C, Hum={hum_val}%, Slot={cur_slot}, FSM={cur_fsm}"
        )
        await ws_manager.broadcast(WS_TELEMETRY, device_state.to_dict())

    async def _handle_schedule_create(self, data: dict) -> None:
        """
        Xử lý khi ESP32 gửi lịch lên Backend (khi khởi động kết nối mạng hoặc khi cài đặt qua EC11).
        ESP32 là nguồn dữ liệu chuẩn khi kết nối: Backend nhận lịch từ ESP32 gửi lên
        và cập nhật vào Database SQLite, sau đó hiển thị lên Web Dashboard.
        KHÔNG gửi lịch ngược lại xuống ESP32 tại thời điểm này.
        """
        try:
            from database.connection import get_db_context
            from database.repositories import schedule_repo
            from services.schedule_service import get_session_label

            # TRƯỜNG HỢP 1: Full Sync danh sách lịch từ ESP32 gửi lên
            if "schedules" in data and isinstance(data["schedules"], list):
                schedules_list = data["schedules"]
                logger.info(f"[MQTT SYNC] ESP32 gửi lên {len(schedules_list)} cữ lịch để đồng bộ vào Web Database...")

                async with get_db_context() as db:
                    # Cập nhật Database theo đúng danh sách lịch ESP32 gửi lên
                    await db.execute("DELETE FROM schedules WHERE device_id = ?", (settings.DEVICE_ID,))
                    for s in schedules_list:
                        slot = int(s.get("slot") or s.get("slot_number") or 1)
                        if slot < 1 or slot > 7:
                            continue
                        t_str = str(s.get("time") or s.get("time_of_day") or "08:00")
                        d_qty = int(s.get("dose") or s.get("dose_quantity") or 1)
                        is_act = bool(s.get("is_active", True))
                        lbl = get_session_label(t_str)
                        await schedule_repo.create(
                            db,
                            device_id=settings.DEVICE_ID,
                            slot_number=slot,
                            time_of_day=t_str,
                            label=lbl,
                            dose_quantity=d_qty,
                        )
                        if not is_act:
                            await schedule_repo.set_active(db, settings.DEVICE_ID, t_str, slot, False)
                    await db.commit()

                logger.info(f"[MQTT SYNC] Đã cập nhật xong {len(schedules_list)} cữ lịch từ ESP32 vào Database.")
                await ws_manager.broadcast(WS_SCHEDULE_UPDATED, {"synced": True, "count": len(schedules_list)})
                return

            # TRƯỜNG HỢP 2: Cập nhật theo mốc giờ (SCHEDULE_SAVED / SCHEDULE_CREATE)
            time_str = data.get("time_of_day") or data.get("time")
            if not time_str and "h" in data and "m" in data:
                time_str = f"{int(data['h']):02d}:{int(data['m']):02d}"

            if not time_str:
                logger.warning(f"[MQTT] Lệnh schedule từ ESP32 thiếu thông tin giờ: {data}")
                return

            parts = str(time_str).strip().split(":")
            if len(parts) == 2:
                time_str = f"{int(parts[0]):02d}:{int(parts[1]):02d}"

            mask = data.get("mask")
            dose = int(data.get("dose_quantity") or data.get("dose") or 1)

            async with get_db_context() as db:
                if mask is not None:
                    int_mask = int(mask)
                    existing = await schedule_repo.get_all_by_time(db, settings.DEVICE_ID, time_str)
                    existing_slots = {item["slot_number"]: item["id"] for item in existing}

                    if int_mask == 0:
                        # Người dùng xóa bỏ toàn bộ mốc giờ này trên phần cứng ESP32
                        del_cnt = await schedule_repo.delete_by_time(db, settings.DEVICE_ID, time_str)
                        logger.info(f"[MQTT] Đã xóa {del_cnt} cữ uống tại mốc {time_str} theo lệnh từ ESP32.")
                        await ws_manager.broadcast(WS_SCHEDULE_UPDATED, {"time_of_day": time_str, "deleted": True})
                    else:
                        # 1. Xóa các ngăn đã bị bỏ chọn khỏi mốc giờ này trên ESP32
                        for s_num, s_id in existing_slots.items():
                            if not ((int_mask >> (s_num - 1)) & 1):
                                await schedule_repo.delete(db, s_id)

                        # 2. Upsert các ngăn được chọn trong bitmask trực tiếp
                        label = get_session_label(time_str)
                        act_mask = int(data["active_mask"]) if "active_mask" in data else int_mask
                        count_saved = 0
                        for s in range(7):
                            if (int_mask >> s) & 1:
                                slot_num = s + 1
                                is_act = bool((act_mask >> s) & 1)
                                await schedule_repo.upsert(
                                    db,
                                    device_id=settings.DEVICE_ID,
                                    slot_number=slot_num,
                                    time_of_day=time_str,
                                    label=label,
                                    dose_quantity=dose
                                )
                                if not is_act:
                                    await schedule_repo.set_active(db, settings.DEVICE_ID, time_str, slot_num, False)
                                count_saved += 1

                        logger.info(
                            f"[MQTT] Đã lưu {count_saved} lịch lúc {time_str} (Mask=0x{int_mask:02X}) từ ESP32."
                        )
                        await ws_manager.broadcast(WS_SCHEDULE_UPDATED, {
                            "time_of_day": time_str,
                            "mask": int_mask,
                            "active_mask": act_mask,
                            "count": count_saved
                        })
                else:
                    slot = data.get("slot_number") if data.get("slot_number") is not None else data.get("slot")
                    if slot is not None and 1 <= int(slot) <= 7:
                        label = get_session_label(time_str)
                        saved = await schedule_repo.upsert(
                            db,
                            device_id=settings.DEVICE_ID,
                            slot_number=int(slot),
                            time_of_day=time_str,
                            label=label,
                            dose_quantity=dose
                        )
                        logger.info(f"[MQTT] Đã tạo lịch đơn lẻ từ ESP32: Slot {slot} lúc {time_str}")
                        await ws_manager.broadcast(WS_SCHEDULE_UPDATED, saved)
        except Exception as e:
            logger.error(f"Lỗi khi lưu lịch từ ESP32: {e}", exc_info=True)

    async def _handle_error(self, data: dict) -> None:
        """Xử lý lỗi từ ESP32 (kẹt cơ, lỗi motor...)."""
        code = data.get("code", "UNKNOWN")
        message = data.get("message", "")
        logger.error(f"ESP32 ERROR: {code} — {message}")
        if code == "JAMMED":
            device_state.set_jammed()
            await ws_manager.broadcast(WS_JAMMED, {**data, **device_state.to_dict()})
        else:
            await ws_manager.broadcast(WS_ERROR, {**data, **device_state.to_dict()})
        await self._fire(self.on_error, {**data, **device_state.to_dict()})

    # ── Helper ────────────────────────────────────────────────────────────────
    @staticmethod
    async def _fire(callback: AsyncCallback | None, data: dict) -> None:
        """Gọi callback async nếu đã được gắn vào (không crash nếu None)."""
        if callback is not None:
            try:
                await callback(data)
            except Exception as e:
                logger.error(f"MQTT callback error: {e}", exc_info=True)


# ── Singleton toàn cục ────────────────────────────────────────────────────────
# Import và dùng trực tiếp:
#   from device.mqtt_client import mqtt_client
#   await mqtt_client.publish(topic, payload)
mqtt_client = MQTTClient()
#   from device.mqtt_client import mqtt_client
#   await mqtt_client.publish(topic, payload)
mqtt_client = MQTTClient()
