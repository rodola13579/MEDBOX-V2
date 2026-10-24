"""
services/medicine_service.py
─────────────────────────────
Business logic cho Medicine.

Quy tắc nghiệp vụ xử lý tại đây:
  1. Xóa thuốc đang gán cho ngăn → 409 Conflict
  2. Không cho phép tên thuốc rỗng sau khi trim

Repo layer chỉ làm SQL. Service layer quyết định ĐƯỢC LÀM HAY KHÔNG.
"""

import logging

import aiosqlite
from fastapi import HTTPException

from database.repositories import medicine_repo
from schemas.medicine import MedicineCreate, MedicineUpdate

logger = logging.getLogger(__name__)


async def list_medicines(db: aiosqlite.Connection) -> list[dict]:
    """Trả về toàn bộ danh sách thuốc."""
    return await medicine_repo.get_all(db)


async def get_medicine_or_404(db: aiosqlite.Connection, medicine_id: int) -> dict:
    """
    Lấy thuốc theo ID.
    Raise 404 nếu không tìm thấy.
    """
    medicine = await medicine_repo.get_by_id(db, medicine_id)
    if not medicine:
        raise HTTPException(
            status_code=404,
            detail=f"Thuốc id={medicine_id} không tồn tại."
        )
    return medicine


async def create_medicine(db: aiosqlite.Connection, data: MedicineCreate) -> dict:
    """
    Tạo thuốc mới.
    Không cho phép tên trùng (case-insensitive).
    """
    name = data.name.strip()
    if not name:
        raise HTTPException(status_code=422, detail="Tên thuốc không được để trống.")

    existing = await medicine_repo.get_by_name(db, name)
    if existing:
        raise HTTPException(
            status_code=409,
            detail=f"Tên thuốc '{name}' đã tồn tại (id={existing['id']})."
        )

    return await medicine_repo.create(
        db,
        name=name,
        dosage_unit=data.dosage_unit.strip(),
        instructions=getattr(data, "instructions", None) or "Không phụ thuộc"
    )


async def update_medicine(
    db: aiosqlite.Connection,
    medicine_id: int,
    data: MedicineUpdate
) -> dict:
    """
    Cập nhật thuốc (partial update — chỉ field nào được truyền vào mới thay đổi).
    Raise 404 nếu không tìm thấy.
    """
    # Xác nhận thuốc tồn tại
    await get_medicine_or_404(db, medicine_id)

    # Lấy các field thực sự được truyền vào (exclude_unset loại bỏ field không có trong payload)
    fields = data.model_dump(exclude_unset=True)

    # Trim tên nếu có
    if "name" in fields:
        fields["name"] = fields["name"].strip()
        if not fields["name"]:
            raise HTTPException(status_code=422, detail="Tên thuốc không được để trống.")

    if not fields:
        # Không có gì thay đổi — trả về nguyên bản
        return await medicine_repo.get_by_id(db, medicine_id)

    return await medicine_repo.update(db, medicine_id, fields)


async def delete_medicine(db: aiosqlite.Connection, medicine_id: int) -> None:
    """
    Xóa thuốc.

    NGHIỆP VỤ QUAN TRỌNG: Không cho phép xóa thuốc đang được gán cho ngăn.
    → Trả về 409 Conflict với thông báo rõ ràng.

    Raise 404 nếu thuốc không tồn tại.
    """
    # Xác nhận tồn tại
    await get_medicine_or_404(db, medicine_id)

    # Kiểm tra đang dùng
    in_use = await medicine_repo.is_used_by_slot(db, medicine_id)
    if in_use:
        raise HTTPException(
            status_code=409,
            detail=(
                f"Không thể xóa thuốc id={medicine_id} vì đang được gán vào ngăn khay. "
                f"Hãy xóa thuốc ra khỏi ngăn trước khi xóa."
            )
        )

    deleted = await medicine_repo.delete(db, medicine_id)
    if not deleted:
        raise HTTPException(status_code=404, detail=f"Thuốc id={medicine_id} không tồn tại.")

    logger.info(f"Medicine deleted: id={medicine_id}")
