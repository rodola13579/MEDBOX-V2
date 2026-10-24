"""
services/schedule_service.py
─────────────────────────────
Business logic cho Schedule.

Chức năng cốt lõi:
  1. get_session_label()       — Tự động tính nhãn Buổi từ time_of_day
  2. sync_schedules_to_esp32() — Phát MQTT đồng bộ bảng lịch xuống ESP32
  3. create_schedule()         — Validate + tạo lịch mới đơn lẻ (409 nếu trùng slot+giờ)
  4. create_batch_schedules()  — Đặt lịch đồng bộ theo mốc giờ cho nhiều ngăn (upsert + sync ESP32)
  5. toggle_schedule()         — Bật/tắt lịch (404 nếu không tìm thấy) + sync ESP32
  6. delete_schedule()         — Xóa lịch (404 nếu không tìm thấy) + sync ESP32

NHÃN BUỔI (theo SYSTEM_SPECIFICATION.md):
  "05:00" – "10:59"  →  "Buổi Sáng"
  "11:00" – "13:59"  →  "Buổi Trưa"
  "14:00" – "17:59"  →  "Buổi Chiều"
  "18:00" – "04:59"  →  "Buổi Tối"
"""

import logging

import aiosqlite
from fastapi import HTTPException

from config import settings
from database.repositories import schedule_repo
from schemas.schedule import ScheduleCreate, ScheduleBatchCreate
from ws_manager import ws_manager, WS_SCHEDULE_UPDATED

logger = logging.getLogger(__name__)

DEVICE_ID = settings.DEVICE_ID


def get_session_label(time_str: str) -> str:
    """
    Tính nhãn Buổi từ chuỗi giờ 'HH:MM'.

    Ranh giới:
      05:00 – 10:59  →  Buổi Sáng
      11:00 – 13:59  →  Buổi Trưa
      14:00 – 17:59  →  Buổi Chiều
      18:00 – 23:59  →  Buổi Tối
      00:00 – 04:59  →  Buổi Tối  (qua đêm)
    """
    hour, minute = map(int, time_str.split(":"))
    total_minutes = hour * 60 + minute

    # Sáng:  05:00 (300 phút) → 10:59 (659 phút)
    if 300 <= total_minutes <= 659:
        return "Buổi Sáng"
    # Trưa:  11:00 (660 phút) → 13:59 (839 phút)
    elif 660 <= total_minutes <= 839:
        return "Buổi Trưa"
    # Chiều: 14:00 (840 phút) → 17:59 (1079 phút)
    elif 840 <= total_minutes <= 1079:
        return "Buổi Chiều"
    # Tối:   18:00 (1080 phút) → 23:59 (1439 phút) hoặc 00:00 → 04:59
    else:
        return "Buổi Tối"


async def sync_schedules_to_esp32(db: aiosqlite.Connection) -> bool:
    """
    Đồng bộ toàn bộ bảng lịch hiện tại xuống phần cứng ESP32 qua MQTT.
    ESP32 sẽ nhận mảng {h, m, mask} và lưu vĩnh viễn vào Flash NVS.
    """
    try:
        from device.mqtt_client import mqtt_client
        all_schedules = await schedule_repo.get_all(db, DEVICE_ID)
        ok = await mqtt_client.publish_sync_schedules(all_schedules)
        if ok:
            logger.info(f"Đã phát lệnh MQTT sync {len(all_schedules)} cữ uống xuống ESP32")
        return ok
    except Exception as e:
        logger.warning(f"Không thể sync lịch sang ESP32 qua MQTT: {e}")
        return False


async def list_schedules(db: aiosqlite.Connection) -> list[dict]:
    """Trả về tất cả lịch uống thuốc, sắp xếp theo giờ tăng dần."""
    return await schedule_repo.get_all(db, DEVICE_ID)


async def get_schedule_or_404(db: aiosqlite.Connection, schedule_id: int) -> dict:
    """
    Lấy 1 lịch theo ID.
    Raise 404 nếu không tìm thấy.
    """
    schedule = await schedule_repo.get_by_id(db, schedule_id)
    if not schedule:
        raise HTTPException(
            status_code=404,
            detail=f"Lịch id={schedule_id} không tồn tại."
        )
    return schedule


async def create_schedule(db: aiosqlite.Connection, data: ScheduleCreate) -> dict:
    """
    Tạo lịch uống thuốc mới (đơn lẻ).

    Kiểm tra:
      1. slot_number phải từ 1..7 (Slot 0 = Safe Home)
      2. Không trùng: cùng slot + cùng giờ → 409 Conflict
      3. Tự động tính label từ time_of_day
      4. Tự động đồng bộ xuống ESP32 và broadcast WebSocket
    """
    if data.slot_number == 0:
        raise HTTPException(
            status_code=400,
            detail="Không thể lập lịch cho Slot 0 (Safe Home)."
        )

    # Kiểm tra trùng: cùng slot + cùng giờ
    is_duplicate = await schedule_repo.check_duplicate(
        db, DEVICE_ID, data.slot_number, data.time_of_day
    )
    if is_duplicate:
        raise HTTPException(
            status_code=409,
            detail=(
                f"Ngăn {data.slot_number} đã có lịch uống lúc {data.time_of_day}. "
                f"Mỗi ngăn chỉ được có 1 lịch tại cùng mốc giờ."
            )
        )

    # Kiểm tra giới hạn tối đa 4 cữ uống cho mỗi ngăn
    current_count = await schedule_repo.count_by_slot(db, DEVICE_ID, data.slot_number)
    if current_count >= 4:
        raise HTTPException(
            status_code=400,
            detail=f"Ngăn {data.slot_number} đã đạt tối đa 4 cữ uống trong ngày."
        )

    # Tự động tính nhãn Buổi
    label = get_session_label(data.time_of_day)

    created = await schedule_repo.create(
        db,
        device_id=DEVICE_ID,
        slot_number=data.slot_number,
        time_of_day=data.time_of_day,
        label=label,
        dose_quantity=data.dose_quantity
    )

    logger.info(
        f"Schedule created: slot={data.slot_number}, time={data.time_of_day}, "
        f"label={label!r}, qty={data.dose_quantity}"
    )

    # Đồng bộ phần cứng ESP32 & Web clients
    await sync_schedules_to_esp32(db)
    await ws_manager.broadcast(WS_SCHEDULE_UPDATED, created)

    return created


async def create_batch_schedules(db: aiosqlite.Connection, data: ScheduleBatchCreate) -> dict:
    """
    Tạo/cập nhật hàng loạt lịch uống cho một mốc giờ và danh sách các ngăn tương ứng.
    Tương thích hoàn toàn với quy trình trên phần cứng (Set giờ -> Chọn các ngăn).
    """
    time_val = data.time_of_day
    if not time_val:
        raise HTTPException(status_code=400, detail="Thiếu mốc giờ uống thuốc.")

    if not data.slots:
        raise HTTPException(status_code=400, detail="Vui lòng chọn ít nhất một ngăn thuốc.")

    label = get_session_label(time_val)
    created_list = []

    for item in data.slots:
        slot = item.slot_number
        if slot < 1 or slot > 7:
            raise HTTPException(
                status_code=400,
                detail=f"Ngăn {slot} không hợp lệ. Chỉ được phép lập lịch cho ngăn 1 đến 7."
            )

        # Kiểm tra nếu chưa có mốc này và ngăn đã đạt tối đa 4 cữ
        is_dup = await schedule_repo.check_duplicate(db, DEVICE_ID, slot, time_val)
        if not is_dup:
            cnt = await schedule_repo.count_by_slot(db, DEVICE_ID, slot)
            if cnt >= 4:
                raise HTTPException(
                    status_code=400,
                    detail=f"Ngăn {slot} đã đạt tối đa 4 cữ uống trong ngày."
                )

        qty = item.dose_quantity or 1
        saved = await schedule_repo.upsert(
            db,
            device_id=DEVICE_ID,
            slot_number=slot,
            time_of_day=time_val,
            label=label,
            dose_quantity=qty
        )
        created_list.append(saved)

    logger.info(
        f"Batch schedules saved: time={time_val}, label={label}, count={len(created_list)}"
    )

    # Tự động đồng bộ mảng struct Sched {h, m, mask} xuống ESP32
    await sync_schedules_to_esp32(db)

    # Phát thông báo WebSocket
    for sch in created_list:
        await ws_manager.broadcast(WS_SCHEDULE_UPDATED, sch)

    return {
        "time_of_day": time_val,
        "label": label,
        "created_count": len(created_list),
        "schedules": created_list,
    }


async def toggle_schedule(db: aiosqlite.Connection, schedule_id: int) -> dict:
    """
    Bật/tắt lịch (flip is_active).
    Raise 404 nếu lịch không tồn tại.
    """
    result = await schedule_repo.toggle(db, schedule_id)
    if result is None:
        raise HTTPException(
            status_code=404,
            detail=f"Lịch id={schedule_id} không tồn tại."
        )

    await sync_schedules_to_esp32(db)
    await ws_manager.broadcast(WS_SCHEDULE_UPDATED, result)

    return result


async def delete_schedule(db: aiosqlite.Connection, schedule_id: int) -> None:
    """
    Xóa lịch uống thuốc.
    Raise 404 nếu lịch không tồn tại.
    """
    await get_schedule_or_404(db, schedule_id)

    deleted = await schedule_repo.delete(db, schedule_id)
    if not deleted:
        raise HTTPException(
            status_code=404,
            detail=f"Lịch id={schedule_id} không tồn tại."
        )

    logger.info(f"Schedule deleted: id={schedule_id}")
    await sync_schedules_to_esp32(db)
    await ws_manager.broadcast(WS_SCHEDULE_UPDATED, {"id": schedule_id, "deleted": True})
