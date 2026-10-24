"""
database/repositories/dose_log_repo.py
───────────────────────────────────────
SQL thuần cho bảng dose_logs.

QUY TẮC BẤT BIẾN (từ schema.sql):
  - KHÔNG BAO GIỜ DELETE bản ghi — chỉ INSERT và UPDATE status
  - Status lifecycle: PENDING → DISPENSED → TAKEN | MISSED | JAMMED
  - Tồn kho chỉ bị trừ khi TAKEN (việc này do dispensing_engine xử lý, không ở đây)

Hàm bổ sung cho Task 09 (Scheduler):
  - exists_today()   — Kiểm tra tránh cấp thuốc trùng lần trong ngày
  - get_orphaned()   — Tìm log bị "mồ côi" (server crash, cần recovery)
"""

import logging
from datetime import datetime, timezone

import aiosqlite

logger = logging.getLogger(__name__)


async def create_pending(
    db: aiosqlite.Connection,
    device_id: str,
    slot_number: int,
    scheduled_time: str,          # ISO datetime string — thời điểm lịch dự kiến
    trigger_source: str,           # AUTO_SCHEDULE | EARLY_MANUAL | LOCAL_BUTTON
    schedule_id: int | None = None,
    notes: str | None = None,
) -> dict:
    """
    Tạo bản ghi PENDING khi bắt đầu phiên cấp thuốc.
    Trả về dict của bản ghi vừa tạo.
    """
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    cursor = await db.execute(
        """
        INSERT INTO dose_logs
            (schedule_id, device_id, slot_number, scheduled_time,
             status, trigger_source, notes, created_at)
        VALUES (?, ?, ?, ?, 'PENDING', ?, ?, ?)
        """,
        (schedule_id, device_id, slot_number, scheduled_time,
         trigger_source, notes, now)
    )
    await db.commit()
    log_id = cursor.lastrowid
    logger.debug(f"dose_log created: id={log_id}, slot={slot_number}, trigger={trigger_source}")
    return await get_by_id(db, log_id)


async def get_by_id(db: aiosqlite.Connection, log_id: int) -> dict | None:
    """Lấy 1 bản ghi theo ID."""
    cursor = await db.execute(
        "SELECT * FROM dose_logs WHERE id = ?",
        (log_id,)
    )
    row = await cursor.fetchone()
    return dict(row) if row else None


async def get_active(db: aiosqlite.Connection, device_id: str) -> dict | None:
    """
    Lấy phiên đang hoạt động (status PENDING hoặc DISPENSED).
    Trả về None nếu không có phiên nào đang chạy.
    """
    cursor = await db.execute(
        """
        SELECT * FROM dose_logs
        WHERE device_id = ? AND status IN ('PENDING', 'DISPENSED')
        ORDER BY created_at DESC
        LIMIT 1
        """,
        (device_id,)
    )
    row = await cursor.fetchone()
    return dict(row) if row else None


async def mark_dispensed(
    db: aiosqlite.Connection,
    log_id: int,
    dispensed_time: str | None = None,
) -> dict | None:
    """
    Cập nhật status → DISPENSED khi ESP32 báo SLOT_REACHED.
    Ghi nhận thời điểm khay đến đích.
    """
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    if dispensed_time is None:
        dispensed_time = now

    cursor = await db.execute(
        """
        UPDATE dose_logs
           SET status = 'DISPENSED', dispensed_time = ?
         WHERE id = ? AND status = 'PENDING'
        """,
        (dispensed_time, log_id)
    )
    await db.commit()
    if cursor.rowcount == 0:
        logger.warning(f"mark_dispensed: log_id={log_id} không ở trạng thái PENDING")
        return await get_by_id(db, log_id)
    logger.debug(f"dose_log id={log_id} → DISPENSED")
    return await get_by_id(db, log_id)


async def mark_taken(
    db: aiosqlite.Connection,
    log_id: int,
    trigger_source: str = "LOCAL_BUTTON",   # LOCAL_BUTTON hoặc EARLY_MANUAL
    notes: str | None = None,
) -> dict | None:
    """
    Cập nhật status → TAKEN khi người dùng xác nhận đã lấy thuốc.
    Ghi nhận thời điểm xác nhận.
    QUAN TRỌNG: Hàm này KHÔNG trừ tồn kho — dispensing_engine xử lý riêng.
    """
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    cursor = await db.execute(
        """
        UPDATE dose_logs
           SET status = 'TAKEN', confirmed_time = ?, trigger_source = ?,
               notes = COALESCE(?, notes)
         WHERE id = ? AND status IN ('PENDING', 'DISPENSED')
        """,
        (now, trigger_source, notes, log_id)
    )
    await db.commit()
    if cursor.rowcount == 0:
        logger.warning(f"mark_taken: log_id={log_id} không ở trạng thái phù hợp")
    logger.info(f"dose_log id={log_id} → TAKEN (trigger={trigger_source})")
    return await get_by_id(db, log_id)


async def mark_missed(
    db: aiosqlite.Connection,
    log_id: int,
    notes: str | None = None,
) -> dict | None:
    """
    Cập nhật status → MISSED khi timeout hết hoặc hồi vị khẩn cấp.
    Tồn kho KHÔNG thay đổi.
    """
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    await db.execute(
        """
        UPDATE dose_logs
           SET status = 'MISSED', notes = COALESCE(?, notes)
         WHERE id = ? AND status IN ('PENDING', 'DISPENSED')
        """,
        (notes, log_id)
    )
    await db.commit()
    logger.info(f"dose_log id={log_id} → MISSED (notes={notes!r})")
    return await get_by_id(db, log_id)


async def mark_jammed(
    db: aiosqlite.Connection,
    log_id: int,
    notes: str | None = None,
) -> dict | None:
    """Cập nhật status → JAMMED khi ESP32 báo lỗi kẹt cơ."""
    await db.execute(
        """
        UPDATE dose_logs
           SET status = 'JAMMED', notes = COALESCE(?, notes)
         WHERE id = ?
        """,
        (notes, log_id)
    )
    await db.commit()
    logger.warning(f"dose_log id={log_id} → JAMMED")
    return await get_by_id(db, log_id)


async def get_history(
    db: aiosqlite.Connection,
    device_id: str,
    limit: int = 50,
    status_filter: str | None = None,
) -> list[dict]:
    """
    Lấy lịch sử nhật ký, mới nhất trước.
    status_filter: nếu truyền vào, chỉ lấy các bản ghi có status tương ứng.
    """
    if status_filter:
        cursor = await db.execute(
            """
            SELECT * FROM dose_logs
             WHERE device_id = ? AND status = ?
             ORDER BY created_at DESC
             LIMIT ?
            """,
            (device_id, status_filter, limit)
        )
    else:
        cursor = await db.execute(
            """
            SELECT * FROM dose_logs
             WHERE device_id = ?
             ORDER BY created_at DESC
             LIMIT ?
            """,
            (device_id, limit)
        )
    rows = await cursor.fetchall()
    return [dict(row) for row in rows]


async def exists_today(
    db: aiosqlite.Connection,
    schedule_id: int,
    today_date: str,          # Format 'YYYY-MM-DD'
) -> bool:
    """
    Kiểm tra xem schedule này đã được cấp thuốc HÔM NAY chưa.
    Bỏ qua các log có status = 'MISSED' (đã bỏ lỡ → vẫn cần cấp lại).

    Dùng bởi scheduler_engine để tránh tạo trùng dose_log cho cùng 1 lịch trong ngày.
    """
    cursor = await db.execute(
        """
        SELECT COUNT(*) FROM dose_logs
         WHERE schedule_id = ?
           AND status NOT IN ('MISSED')
           AND DATE(scheduled_time) = ?
        """,
        (schedule_id, today_date)
    )
    row = await cursor.fetchone()
    return (row[0] > 0) if row else False


async def get_orphaned(
    db: aiosqlite.Connection,
    device_id: str,
) -> list[dict]:
    """
    Lấy tất cả dose_logs bị "mồ côi" — còn ở trạng thái PENDING hoặc DISPENSED.

    Những log này xuất hiện khi server bị crash hoặc restart đột ngột
    trong khi đang có phiên cấp thuốc. Được xử lý bởi scheduler_engine
    khi khởi động (orphan recovery).
    """
    cursor = await db.execute(
        """
        SELECT * FROM dose_logs
         WHERE device_id = ? AND status IN ('PENDING', 'DISPENSED')
         ORDER BY created_at ASC
        """,
        (device_id,)
    )
    rows = await cursor.fetchall()
    return [dict(row) for row in rows]
