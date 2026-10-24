"""
database/repositories/slot_repo.py
────────────────────────────────────
SQL thuần cho bảng slots.

Nguyên tắc thiết kế:
  - Không có business logic ở đây — chỉ thuần SQL
  - get_all_slots() JOIN với medicines để trả về thông tin thuốc lồng nhau
  - Hàm update_slot() KHÔNG tự bảo vệ Slot 0 — trách nhiệm đó thuộc slot_service.py
  - Trả về dict hoặc list[dict]
"""

import logging
from datetime import datetime, timezone

import aiosqlite

logger = logging.getLogger(__name__)

# Query JOIN slots + medicines — dùng chung cho get_all và get_slot
_SLOT_QUERY = """
    SELECT
        s.id,
        s.device_id,
        s.slot_number,
        s.medicine_id,
        s.capacity,
        s.current_quantity,
        s.default_dose,
        s.is_active,
        s.last_refilled_at,
        s.updated_at,
        m.name        AS medicine_name,
        m.dosage_unit AS medicine_dosage_unit,
        m.instructions AS medicine_instructions
    FROM slots s
    LEFT JOIN medicines m ON s.medicine_id = m.id
"""


def _row_to_dict(row) -> dict:
    """
    Chuyển aiosqlite.Row thành dict có cấu trúc lồng nhau.
    Nếu slot có thuốc → đặt thông tin vào key 'medicine' (nested dict).
    Nếu slot trống   → 'medicine' = None.
    """
    d = dict(row)
    # Tách thông tin thuốc ra nested dict
    if d.get("medicine_id") is not None:
        d["medicine"] = {
            "id": d["medicine_id"],
            "name": d.pop("medicine_name"),
            "dosage_unit": d.pop("medicine_dosage_unit"),
            "instructions": d.pop("medicine_instructions"),
        }
    else:
        d.pop("medicine_name", None)
        d.pop("medicine_dosage_unit", None)
        d.pop("medicine_instructions", None)
        d["medicine"] = None
    return d


async def get_all_slots(db: aiosqlite.Connection, device_id: str) -> list[dict]:
    """Lấy tất cả 8 ngăn của thiết bị, kèm thông tin thuốc, sắp xếp theo slot_number."""
    cursor = await db.execute(
        _SLOT_QUERY + " WHERE s.device_id = ? ORDER BY s.slot_number ASC",
        (device_id,)
    )
    rows = await cursor.fetchall()
    return [_row_to_dict(row) for row in rows]


async def get_slot(db: aiosqlite.Connection, device_id: str, slot_number: int) -> dict | None:
    """Lấy 1 ngăn theo slot_number. Trả về None nếu không tìm thấy."""
    cursor = await db.execute(
        _SLOT_QUERY + " WHERE s.device_id = ? AND s.slot_number = ?",
        (device_id, slot_number)
    )
    row = await cursor.fetchone()
    return _row_to_dict(row) if row else None


async def update_slot(
    db: aiosqlite.Connection,
    device_id: str,
    slot_number: int,
    fields: dict
) -> dict | None:
    """
    Cập nhật các field được truyền vào cho 1 ngăn cụ thể.

    fields: dict chứa các cột cần cập nhật.
    Ví dụ: {"medicine_id": 2, "current_quantity": 10}

    Khi current_quantity được cập nhật (nạp thuốc) → cũng cập nhật last_refilled_at.
    Trả về ngăn sau khi cập nhật, hoặc None nếu không tìm thấy.
    """
    if not fields:
        return await get_slot(db, device_id, slot_number)

    now = datetime.now(timezone.utc).strftime("%Y-%m-%d %H:%M:%S")
    fields["updated_at"] = now

    # Nếu có nạp số viên → ghi thời điểm nạp thuốc và đảm bảo capacity >= current_quantity
    if "current_quantity" in fields and slot_number > 0:
        fields["last_refilled_at"] = now
        q = fields["current_quantity"]
        if "capacity" not in fields:
            cur_slot = await get_slot(db, device_id, slot_number)
            cur_cap = cur_slot.get("capacity", 1000) if cur_slot else 1000
            if q > cur_cap:
                fields["capacity"] = max(q, 1000)
        elif fields["capacity"] < q:
            fields["capacity"] = q

    set_clause = ", ".join(f"{col} = ?" for col in fields.keys())
    values = list(fields.values()) + [device_id, slot_number]

    cursor = await db.execute(
        f"UPDATE slots SET {set_clause} WHERE device_id = ? AND slot_number = ?",
        values
    )
    await db.commit()

    if cursor.rowcount == 0:
        return None

    logger.debug(f"Slot updated: device={device_id}, slot={slot_number}, fields={list(fields.keys())}")
    return await get_slot(db, device_id, slot_number)

# Cho phép cả `from database.repositories import slot_repo` và `from database.repositories.slot_repo import slot_repo`
import sys
slot_repo = sys.modules[__name__]
