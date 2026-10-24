"""
database/repositories/medicine_repo.py
───────────────────────────────────────
SQL thuần cho bảng medicines.

Nguyên tắc thiết kế:
  - Không có business logic ở đây — chỉ thuần SQL
  - Mọi hàm nhận aiosqlite.Connection làm tham số đầu tiên
  - Trả về dict hoặc list[dict] để service layer dễ dùng
  - get_by_id() trả về None nếu không tìm thấy (không raise exception)
"""

import logging
from datetime import datetime, timezone

import aiosqlite

logger = logging.getLogger(__name__)


async def get_all(db: aiosqlite.Connection) -> list[dict]:
    """Lấy toàn bộ danh sách thuốc, sắp xếp theo tên A-Z."""
    cursor = await db.execute(
        "SELECT id, name, dosage_unit, instructions, created_at, updated_at "
        "FROM medicines ORDER BY name ASC"
    )
    rows = await cursor.fetchall()
    return [dict(row) for row in rows]


async def get_by_id(db: aiosqlite.Connection, medicine_id: int) -> dict | None:
    """Lấy 1 thuốc theo ID. Trả về None nếu không tồn tại."""
    cursor = await db.execute(
        "SELECT id, name, dosage_unit, instructions, created_at, updated_at "
        "FROM medicines WHERE id = ?",
        (medicine_id,)
    )
    row = await cursor.fetchone()
    return dict(row) if row else None


async def get_by_name(db: aiosqlite.Connection, name: str) -> dict | None:
    """Tìm thuốc theo tên (case-insensitive). Dùng để kiểm tra trùng tên."""
    cursor = await db.execute(
        "SELECT id FROM medicines WHERE LOWER(name) = LOWER(?)",
        (name,)
    )
    row = await cursor.fetchone()
    return dict(row) if row else None


async def create(db: aiosqlite.Connection, name: str, dosage_unit: str = "viên", instructions: str = "Không phụ thuộc") -> dict:
    """
    Tạo thuốc mới. Trả về dict của thuốc vừa tạo.
    Gọi db.commit() bên trong hàm — caller không cần commit thêm.
    """
    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    cursor = await db.execute(
        "INSERT INTO medicines (name, dosage_unit, instructions, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?)",
        (name, dosage_unit, instructions, now, now)
    )
    await db.commit()
    new_id = cursor.lastrowid
    logger.debug(f"Medicine created: id={new_id}, name={name!r}")
    return await get_by_id(db, new_id)


async def update(db: aiosqlite.Connection, medicine_id: int, fields: dict) -> dict | None:
    """
    Cập nhật các field được truyền vào (partial update).

    fields: dict với các key là tên cột cần cập nhật.
    Ví dụ: {"name": "Aspirin", "instructions": "Sau ăn"}

    Trả về thuốc sau khi cập nhật, hoặc None nếu không tìm thấy.
    """
    if not fields:
        return await get_by_id(db, medicine_id)

    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    fields["updated_at"] = now

    set_clause = ", ".join(f"{col} = ?" for col in fields.keys())
    values = list(fields.values()) + [medicine_id]

    await db.execute(
        f"UPDATE medicines SET {set_clause} WHERE id = ?",
        values
    )
    await db.commit()
    logger.debug(f"Medicine updated: id={medicine_id}, fields={list(fields.keys())}")
    return await get_by_id(db, medicine_id)


async def delete(db: aiosqlite.Connection, medicine_id: int) -> bool:
    """
    Xóa thuốc theo ID.
    Trả về True nếu xóa thành công, False nếu không tìm thấy.

    LƯU Ý: Caller phải kiểm tra thuốc có đang gán cho slot không trước khi gọi hàm này.
    Nếu có slot đang dùng → schema.sql ON DELETE SET NULL sẽ tự xử lý.
    """
    cursor = await db.execute(
        "DELETE FROM medicines WHERE id = ?",
        (medicine_id,)
    )
    await db.commit()
    deleted = cursor.rowcount > 0
    if deleted:
        logger.debug(f"Medicine deleted: id={medicine_id}")
    return deleted


async def is_used_by_slot(db: aiosqlite.Connection, medicine_id: int) -> bool:
    """
    Kiểm tra thuốc có đang được gán vào ngăn nào không.
    Dùng để ngăn xóa thuốc đang hoạt động (409 Conflict).
    """
    cursor = await db.execute(
        "SELECT COUNT(*) FROM slots WHERE medicine_id = ?",
        (medicine_id,)
    )
    row = await cursor.fetchone()
    return (row[0] > 0) if row else False
