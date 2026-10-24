"""
schemas/slot.py
───────────────
Pydantic models cho Slot API.

SlotUpdate    — dùng khi PUT (chỉnh sửa cấu hình ngăn)
SlotResponse  — dùng khi trả về, bao gồm thông tin thuốc lồng nhau (nested)

QUY TẮC BẤT BIẾN (phải giống SYSTEM_SPECIFICATION.md):
  - Slot 0 tuyệt đối không thể chỉnh sửa qua API
  - Không thể gán medicine_id vào Slot 0 (ràng buộc kiểm tra ở service layer)
"""

from datetime import datetime
from pydantic import BaseModel, Field


class MedicineInSlot(BaseModel):
    """Thông tin thuốc tối giản — nhúng bên trong SlotResponse."""
    id: int
    name: str
    dosage_unit: str
    instructions: str | None = None

    model_config = {"from_attributes": True}


class SlotUpdate(BaseModel):
    """
    Cập nhật cấu hình ngăn.

    Tất cả field đều optional — chỉ cập nhật những gì được truyền vào.
    Để xóa thuốc khỏi ngăn, truyền: {"medicine_id": null}
    """
    medicine_id: int | None = Field(
        default=...,  # Bắt buộc phải có key này trong request (kể cả khi null)
        description="ID thuốc gán vào ngăn. null = xóa thuốc khỏi ngăn"
    )
    capacity: int | None = Field(default=None, ge=0, description="Sức chứa tối đa (số viên)")
    current_quantity: int | None = Field(default=None, ge=0, description="Số viên hiện tại (dùng khi nạp thuốc)")
    default_dose: int | None = Field(default=None, ge=1, le=10, description="Liều dùng mỗi lần uống (số viên/cữ)")
    is_active: bool | None = Field(default=None, description="Bật/tắt ngăn")

    model_config = {
        "json_schema_extra": {
            "example": {
                "medicine_id": 2,
                "capacity": 14,
                "current_quantity": 14,
                "default_dose": 1,
                "is_active": True
            }
        }
    }


class SlotUpdatePartial(BaseModel):
    """
    Phiên bản partial update — dùng khi chỉ muốn cập nhật 1 vài field.
    medicine_id mặc định là MISSING (không truyền = không thay đổi).
    """
    medicine_id: int | None | str = Field(default="__UNSET__")
    capacity: int | None = Field(default=None, ge=0)
    current_quantity: int | None = Field(default=None, ge=0)
    default_dose: int | None = Field(default=None, ge=1, le=10)
    is_active: bool | None = Field(default=None)


class SlotResponse(BaseModel):
    id: int
    device_id: str
    slot_number: int
    medicine_id: int | None
    medicine: MedicineInSlot | None   # None nếu ngăn trống hoặc là Slot 0
    capacity: int
    current_quantity: int
    default_dose: int = 1
    is_active: bool
    last_refilled_at: datetime | None
    updated_at: datetime

    model_config = {"from_attributes": True}
