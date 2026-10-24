"""
database/repositories/schedule_repo.py
───────────────────────────────────────
SQL thuần cho bảng schedules.

Nguyên tắc thiết kế:
  - Không có business logic ở đây — chỉ thuần SQL
  - get_all() JOIN với slots + medicines để trả về tên thuốc trong từng lịch
  - get_active_by_time() được dùng bởi scheduler_engine (Task 09)
  - toggle() lật cờ is_active (0→1 hoặc 1→0)
"""

import logging
from datetime import datetime, timezone

import aiosqlite

logger = logging.getLogger(__name__)

# JOIN schedules → slots → medicines để lấy tên thuốc kèm theo
_SCHEDULE_QUERY = """
    SELECT
        sc.id,
        sc.device_id,
        sc.slot_number,
        sc.time_of_day,
        sc.label,
        sc.dose_quantity,
        sc.is_active,
        sc.created_at,
        sc.updated_at,
        m.name        AS medicine_name,
        m.dosage_unit AS medicine_dosage_unit
    FROM schedules sc
    LEFT JOIN slots sl
           ON sc.device_id = sl.device_id
          AND sc.slot_number = sl.slot_number
    LEFT JOIN medicines m ON sl.medicine_id = m.id
"""


async def get_all(db: aiosqlite.Connection, device_id: str) -> list[dict]:
    """
    Lấy tất cả lịch uống thuốc của thiết bị.
    Sắp xếp theo giờ tăng dần (hiển thị theo dòng thời gian).
    """
    cursor = await db.execute(
        _SCHEDULE_QUERY + " WHERE sc.device_id = ? ORDER BY sc.time_of_day ASC",
        (device_id,)
    )
    rows = await cursor.fetchall()
    return [dict(row) for row in rows]


async def get_by_id(db: aiosqlite.Connection, schedule_id: int) -> dict | None:
    """Lấy 1 lịch theo ID. Trả về None nếu không tồn tại."""
    cursor = await db.execute(
        _SCHEDULE_QUERY + " WHERE sc.id = ?",
        (schedule_id,)
    )
    row = await cursor.fetchone()
    return dict(row) if row else None


async def get_active_by_time(
    db: aiosqlite.Connection,
    device_id: str,
    time_str: str
) -> list[dict]:
    """
    Lấy tất cả lịch ĐANG KÍCH HOẠT tại mốc giờ cụ thể (định dạng 'HH:MM').
    Được gọi bởi scheduler_engine mỗi phút để tìm cữ cần cấp thuốc.
    """
    cursor = await db.execute(
        _SCHEDULE_QUERY +
        " WHERE sc.device_id = ? AND sc.is_active = 1 AND sc.time_of_day = ?",
        (device_id, time_str)
    )
    rows = await cursor.fetchall()
    return [dict(row) for row in rows]


async def check_duplicate(
    db: aiosqlite.Connection,
    device_id: str,
    slot_number: int,
    time_of_day: str,
    exclude_id: int | None = None
) -> bool:
    """
    Kiểm tra đã tồn tại lịch trùng (cùng slot + cùng giờ) chưa.
    exclude_id: bỏ qua bản ghi có ID này (dùng khi update).
    """
    query = """
        SELECT COUNT(*) FROM schedules
        WHERE device_id = ? AND slot_number = ? AND time_of_day = ?
    """
    params: list = [device_id, slot_number, time_of_day]

    if exclude_id is not None:
        query += " AND id != ?"
        params.append(exclude_id)

    cursor = await db.execute(query, params)
    row = await cursor.fetchone()
    return (row[0] > 0) if row else False


async def create(
    db: aiosqlite.Connection,
    device_id: str,
    slot_number: int,
    time_of_day: str,
    label: str,
    dose_quantity: int
) -> dict:
    """
    Tạo lịch mới. Trả về dict lịch vừa tạo (kèm tên thuốc từ JOIN).
    Caller đã kiểm tra trùng trước khi gọi hàm này.
    """
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    cursor = await db.execute(
        """
        INSERT INTO schedules
            (device_id, slot_number, time_of_day, label, dose_quantity, is_active, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, 1, ?, ?)
        """,
        (device_id, slot_number, time_of_day, label, dose_quantity, now, now)
    )
    await db.commit()
    new_id = cursor.lastrowid
    logger.debug(f"Schedule created: id={new_id}, slot={slot_number}, time={time_of_day}, label={label!r}")
    return await get_by_id(db, new_id)


async def upsert(
    db: aiosqlite.Connection,
    device_id: str,
    slot_number: int,
    time_of_day: str,
    label: str,
    dose_quantity: int
) -> dict:
    """
    Tạo mới hoặc cập nhật nếu đã tồn tại lịch (device_id, time_of_day, slot_number).
    Nếu đã tồn tại, cập nhật dose_quantity, nhãn label và kích hoạt lại is_active=1.
    """
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    await db.execute(
        """
        INSERT INTO schedules
            (device_id, slot_number, time_of_day, label, dose_quantity, is_active, created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, 1, ?, ?)
        ON CONFLICT(device_id, time_of_day, slot_number)
        DO UPDATE SET
            dose_quantity = excluded.dose_quantity,
            label = excluded.label,
            is_active = 1,
            updated_at = excluded.updated_at
        """,
        (device_id, slot_number, time_of_day, label, dose_quantity, now, now)
    )
    await db.commit()
    cursor = await db.execute(
        _SCHEDULE_QUERY + " WHERE sc.device_id = ? AND sc.slot_number = ? AND sc.time_of_day = ?",
        (device_id, slot_number, time_of_day)
    )
    row = await cursor.fetchone()
    logger.debug(f"Schedule upserted: slot={slot_number}, time={time_of_day}, label={label!r}")
    return dict(row)


async def toggle(db: aiosqlite.Connection, schedule_id: int) -> dict | None:
    """
    Lật cờ is_active: 0→1 hoặc 1→0.
    Trả về lịch sau khi cập nhật, hoặc None nếu không tìm thấy.
    """
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    cursor = await db.execute(
        "UPDATE schedules SET is_active = NOT is_active, updated_at = ? WHERE id = ?",
        (now, schedule_id)
    )
    await db.commit()

    if cursor.rowcount == 0:
        return None

    result = await get_by_id(db, schedule_id)
    state = "ACTIVE" if result and result["is_active"] else "INACTIVE"
    logger.debug(f"Schedule id={schedule_id} toggled → {state}")
    return result


async def set_active(
    db: aiosqlite.Connection,
    device_id: str,
    time_of_day: str,
    slot_number: int,
    is_active: bool
) -> bool:
    """Cập nhật trạng thái bật/tắt (is_active) cho một ngăn tại mốc giờ cụ thể."""
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    cursor = await db.execute(
        "UPDATE schedules SET is_active = ?, updated_at = ? WHERE device_id = ? AND time_of_day = ? AND slot_number = ?",
        (1 if is_active else 0, now, device_id, time_of_day, slot_number)
    )
    await db.commit()
    return cursor.rowcount > 0


async def delete(db: aiosqlite.Connection, schedule_id: int) -> bool:
    """
    Xóa lịch theo ID.
    Trả về True nếu xóa thành công, False nếu không tìm thấy.
    """
    cursor = await db.execute(
        "DELETE FROM schedules WHERE id = ?",
        (schedule_id,)
    )
    await db.commit()
    deleted = cursor.rowcount > 0
    if deleted:
        logger.debug(f"Schedule deleted: id={schedule_id}")
    return deleted


async def get_all_by_time(
    db: aiosqlite.Connection,
    device_id: str,
    time_str: str
) -> list[dict]:
    """Lấy tất cả lịch (cả active lẫn inactive) tại mốc giờ cụ thể của thiết bị."""
    cursor = await db.execute(
        _SCHEDULE_QUERY +
        " WHERE sc.device_id = ? AND sc.time_of_day = ?",
        (device_id, time_str)
    )
    rows = await cursor.fetchall()
    return [dict(row) for row in rows]


async def delete_by_time(
    db: aiosqlite.Connection,
    device_id: str,
    time_str: str
) -> int:
    """Xóa tất cả lịch tại mốc giờ cụ thể của thiết bị (dùng khi đồng bộ xóa từ ESP32)."""
    cursor = await db.execute(
        "DELETE FROM schedules WHERE device_id = ? AND time_of_day = ?",
        (device_id, time_str)
    )
    await db.commit()
    count = cursor.rowcount
    if count > 0:
        logger.debug(f"Đã xóa {count} lịch lúc {time_str} của thiết bị {device_id}")
    return count


async def count_by_slot(db: aiosqlite.Connection, device_id: str, slot_number: int) -> int:
    """Đếm số lịch uống hiện có của một slot (tối đa 4 cữ/ngăn)."""
    cursor = await db.execute(
        "SELECT COUNT(*) FROM schedules WHERE device_id = ? AND slot_number = ?",
        (device_id, slot_number)
    )
    row = await cursor.fetchone()
    return row[0] if row else 0


async def delete_by_slot(
    db: aiosqlite.Connection,
    device_id: str,
    slot_number: int
) -> int:
    """Xóa tất cả lịch liên quan đến slot_number của thiết bị."""
    cursor = await db.execute(
        "DELETE FROM schedules WHERE device_id = ? AND slot_number = ?",
        (device_id, slot_number)
    )
    await db.commit()
    count = cursor.rowcount
    if count > 0:
        logger.info(f"Đã xóa {count} lịch của Ngăn {slot_number} (thiết bị {device_id})")
    return count

