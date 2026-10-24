"""
api/slots.py
─────────────
Router cho Slot API.

Endpoints:
  GET  /api/v1/slots              — Danh sách 8 ngăn (kèm thông tin thuốc)
  GET  /api/v1/slots/{slot_number} — Chi tiết 1 ngăn
  PUT  /api/v1/slots/{slot_number} — Cập nhật cấu hình ngăn

QUY TẮC:
  PUT /api/v1/slots/0 luôn trả về 400 Bad Request (Slot 0 = Safe Home bất khả xâm phạm).
"""

from typing import Annotated

from fastapi import APIRouter, Body, Path

from api.dependencies import DB
from schemas.slot import SlotResponse
from services import slot_service

router = APIRouter(
    prefix="/api/v1/slots",
    tags=["Slots"],
)


@router.get(
    "",
    response_model=list[SlotResponse],
    summary="Danh sách 8 ngăn",
    description=(
        "Trả về cấu hình 8 ngăn (Slot 0..7), kèm thông tin thuốc lồng nhau.\n\n"
        "- **Slot 0**: Safe Home — luôn trống, `medicine=null`, `capacity=0`.\n"
        "- **Slot 1..7**: Ngăn thuốc — có thể có hoặc không có thuốc."
    ),
)
async def list_slots(db: DB):
    return await slot_service.list_slots(db)


@router.get(
    "/{slot_number}",
    response_model=SlotResponse,
    summary="Chi tiết 1 ngăn",
)
async def get_slot(
    slot_number: Annotated[int, Path(ge=0, le=7, description="Số ngăn (0..7)")],
    db: DB,
):
    return await slot_service.get_slot_or_404(db, slot_number)


@router.put(
    "/{slot_number}",
    response_model=SlotResponse,
    summary="Cập nhật cấu hình ngăn",
    description=(
        "Cập nhật cấu hình ngăn thuốc.\n\n"
        "**Quy tắc:**\n"
        "- `slot_number=0` → **400 Bad Request** (Slot 0 không thể chỉnh sửa).\n"
        "- `medicine_id=null` → Xóa thuốc khỏi ngăn.\n"
        "- `medicine_id=<id>` → Gán thuốc (404 nếu không tồn tại).\n"
        "- `current_quantity > capacity` → **422 Unprocessable Entity**.\n\n"
        "Chỉ truyền các field cần thay đổi."
    ),
)
async def update_slot(
    slot_number: Annotated[int, Path(ge=0, le=7, description="Số ngăn (0..7)")],
    db: DB,
    medicine_id: Annotated[int | None, Body()] = "__UNSET__",
    capacity: Annotated[int | None, Body()] = None,
    current_quantity: Annotated[int | None, Body()] = None,
    default_dose: Annotated[int | None, Body()] = None,
    is_active: Annotated[bool | None, Body()] = None,
):
    return await slot_service.update_slot(
        db=db,
        slot_number=slot_number,
        medicine_id=medicine_id,
        capacity=capacity,
        current_quantity=current_quantity,
        is_active=is_active,
        default_dose=default_dose,
    )
