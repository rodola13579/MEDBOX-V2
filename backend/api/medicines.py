"""
api/medicines.py
─────────────────
Router cho Medicine API.

Endpoints:
  GET    /api/v1/medicines           — Danh sách tất cả thuốc
  POST   /api/v1/medicines           — Tạo thuốc mới
  GET    /api/v1/medicines/{id}      — Chi tiết 1 thuốc
  PUT    /api/v1/medicines/{id}      — Cập nhật thuốc (partial)
  DELETE /api/v1/medicines/{id}      — Xóa thuốc (409 nếu đang dùng)
"""

from fastapi import APIRouter

from api.dependencies import DB
from schemas.medicine import MedicineCreate, MedicineResponse, MedicineUpdate
from services import medicine_service

router = APIRouter(
    prefix="/api/v1/medicines",
    tags=["Medicines"],
)


@router.get(
    "",
    response_model=list[MedicineResponse],
    summary="Danh sách thuốc",
    description="Trả về toàn bộ danh sách thuốc, sắp xếp theo tên A-Z.",
)
async def list_medicines(db: DB):
    return await medicine_service.list_medicines(db)


@router.post(
    "",
    response_model=MedicineResponse,
    status_code=201,
    summary="Tạo thuốc mới",
    description=(
        "Tạo một loại thuốc mới trong danh mục.\n\n"
        "- Tên thuốc phải duy nhất (case-insensitive)."
    ),
)
async def create_medicine(payload: MedicineCreate, db: DB):
    return await medicine_service.create_medicine(db, payload)


@router.get(
    "/{medicine_id}",
    response_model=MedicineResponse,
    summary="Chi tiết thuốc",
)
async def get_medicine(medicine_id: int, db: DB):
    return await medicine_service.get_medicine_or_404(db, medicine_id)


@router.put(
    "/{medicine_id}",
    response_model=MedicineResponse,
    summary="Cập nhật thuốc",
    description="Partial update — chỉ các field được truyền vào mới thay đổi.",
)
async def update_medicine(medicine_id: int, payload: MedicineUpdate, db: DB):
    return await medicine_service.update_medicine(db, medicine_id, payload)


@router.delete(
    "/{medicine_id}",
    status_code=204,
    summary="Xóa thuốc",
    description=(
        "Xóa thuốc khỏi danh mục.\n\n"
        "- **409 Conflict**: Nếu thuốc đang được gán vào ngăn khay.\n"
        "- Hãy xóa thuốc ra khỏi ngăn trước khi xóa."
    ),
)
async def delete_medicine(medicine_id: int, db: DB):
    await medicine_service.delete_medicine(db, medicine_id)
    # 204 No Content — không trả body
