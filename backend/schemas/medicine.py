"""
schemas/medicine.py
───────────────────
Pydantic models cho Medicine API.

MedicineCreate  — dùng khi POST (tạo mới)
MedicineUpdate  — dùng khi PUT (sửa, tất cả field đều optional)
MedicineResponse — dùng khi trả về cho client
"""

from datetime import datetime

from pydantic import BaseModel, Field


class MedicineCreate(BaseModel):
    name: str = Field(..., min_length=1, max_length=150, description="Tên thuốc")
    dosage_unit: str = Field(default="viên", max_length=30, description="Đơn vị liều (viên, mg, ml...)")
    instructions: str | None = Field(
        default=None,
        description="Chỉ định dùng thuốc (tùy chọn / không bắt buộc)"
    )

    model_config = {
        "json_schema_extra": {
            "example": {
                "name": "Panadol Extra 500mg",
                "dosage_unit": "viên"
            }
        }
    }


class MedicineUpdate(BaseModel):
    """Tất cả field đều optional — chỉ cập nhật những gì được truyền vào."""
    name: str | None = Field(default=None, min_length=1, max_length=150)
    dosage_unit: str | None = Field(default=None, max_length=30)
    instructions: str | None = None

    model_config = {
        "json_schema_extra": {
            "example": {
                "dosage_unit": "viên"
            }
        }
    }


class MedicineResponse(BaseModel):
    id: int
    name: str
    dosage_unit: str
    instructions: str | None = None
    created_at: datetime
    updated_at: datetime

    model_config = {"from_attributes": True}
