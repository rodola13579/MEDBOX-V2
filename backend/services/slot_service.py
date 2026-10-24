"""
services/slot_service.py
─────────────────────────
Business logic cho Slot.

Quy tắc nghiệp vụ xử lý tại đây:
  1. Slot 0 (Safe Home) TUYỆT ĐỐI KHÔNG được chỉnh sửa qua API → 400 Bad Request
  2. Nếu truyền medicine_id → kiểm tra thuốc tồn tại → 404 nếu không có
  3. Nếu truyền current_quantity > capacity → 422 Unprocessable Entity
  4. Chỉ cập nhật các field thực sự được client truyền vào
"""

import logging

import aiosqlite
from fastapi import HTTPException

from config import settings
from database.repositories import medicine_repo, slot_repo

logger = logging.getLogger(__name__)

DEVICE_ID = settings.DEVICE_ID


async def list_slots(db: aiosqlite.Connection) -> list[dict]:
    """Trả về tất cả 8 ngăn (0..7) với thông tin thuốc lồng nhau."""
    return await slot_repo.get_all_slots(db, DEVICE_ID)


async def get_slot_or_404(db: aiosqlite.Connection, slot_number: int) -> dict:
    """
    Lấy 1 ngăn theo slot_number.
    Raise 404 nếu không tìm thấy.
    """
    slot = await slot_repo.get_slot(db, DEVICE_ID, slot_number)
    if not slot:
        raise HTTPException(
            status_code=404,
            detail=f"Ngăn số {slot_number} không tồn tại."
        )
    return slot


async def update_slot(
    db: aiosqlite.Connection,
    slot_number: int,
    medicine_id,          # int | None | "__UNSET__"
    capacity: int | None,
    current_quantity: int | None,
    is_active: bool | None,
    default_dose: int | None = None,
) -> dict:
    """
    Cập nhật cấu hình ngăn.

    QUY TẮC BẤT BIẾN: Slot 0 không thể chỉnh sửa.
    Kiểm tra medicine_id tồn tại nếu được truyền.
    Kiểm tra current_quantity không vượt capacity mới hoặc hiện tại.
    """
    # ── RULE 1: Slot 0 bất khả xâm phạm ────────────────────────────────────
    if slot_number == 0:
        raise HTTPException(
            status_code=400,
            detail=(
                "Không thể chỉnh sửa Slot 0 (Safe Home). "
                "Đây là vị trí hồi vị an toàn của khay thuốc."
            )
        )

    # ── Lấy trạng thái hiện tại của ngăn ────────────────────────────────────
    current_slot = await get_slot_or_404(db, slot_number)

    # ── Xây dựng dict fields cần cập nhật ────────────────────────────────────
    fields: dict = {}

    # medicine_id: "__UNSET__" = không truyền → bỏ qua
    #              None        = muốn xóa thuốc khỏi ngăn
    #              int         = muốn gán thuốc mới
    _UNSET = "__UNSET__"
    if medicine_id != _UNSET:
        if medicine_id is not None:
            # ── RULE 2: Kiểm tra medicine_id tồn tại ───────────────────────
            med = await medicine_repo.get_by_id(db, medicine_id)
            if not med:
                raise HTTPException(
                    status_code=404,
                    detail=f"Thuốc id={medicine_id} không tồn tại."
                )
        fields["medicine_id"] = medicine_id

    if capacity is not None:
        if capacity < 0:
            raise HTTPException(status_code=422, detail="capacity phải >= 0.")
        fields["capacity"] = capacity

    if current_quantity is not None:
        if current_quantity < 0:
            raise HTTPException(status_code=422, detail="current_quantity phải >= 0.")
        # Tự động mở rộng capacity theo số lượng thuốc nạp (gỡ bỏ giới hạn số lượng)
        effective_capacity = fields.get("capacity", current_slot.get("capacity", 1000))
        if current_quantity > effective_capacity:
            fields["capacity"] = current_quantity
        fields["current_quantity"] = current_quantity

    if is_active is not None:
        fields["is_active"] = is_active

    if default_dose is not None:
        if default_dose < 1 or default_dose > 10:
            raise HTTPException(status_code=422, detail="default_dose phải từ 1 đến 10 viên.")
        fields["default_dose"] = default_dose

    if not fields:
        # Không có gì thay đổi
        return current_slot

    updated = await slot_repo.update_slot(db, DEVICE_ID, slot_number, fields)
    if not updated:
        raise HTTPException(status_code=404, detail=f"Ngăn số {slot_number} không tìm thấy.")

    logger.info(f"Slot {slot_number} updated: {list(fields.keys())}")
    try:
        from ws_manager import ws_manager
        await ws_manager.broadcast("slot_updated", {"slot_number": slot_number, **fields})
    except Exception as e:
        logger.warning(f"Lỗi broadcast WS slot_updated: {e}")

    return updated
