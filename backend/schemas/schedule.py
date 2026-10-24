"""
schemas/schedule.py
────────────────────
Pydantic models cho Schedule API.

ScheduleCreate   — dùng khi POST (tạo lịch mới)
ScheduleResponse — dùng khi trả về client (bao gồm label tự động + tên thuốc)

QUY TẮC:
  - slot_number chỉ được từ 1..7 (Slot 0 = Safe Home, KHÔNG lập lịch)
  - time_of_day phải đúng định dạng "HH:MM" 24 giờ
  - label được hệ thống TỰ TÍNH từ time_of_day, client không cần truyền
"""

import re
from pydantic import BaseModel, Field, field_validator, model_validator


class ScheduleCreate(BaseModel):
    slot_number: int = Field(
        ...,
        ge=1, le=7,
        description="Ngăn thuốc (1..7). Slot 0 không được lập lịch."
    )
    time_of_day: str | None = Field(
        default=None,
        description="Giờ uống thuốc — định dạng 24h: 'HH:MM' (00:00 – 23:59, giờ từ 0 đến 23)"
    )
    time: str | None = Field(
        default=None,
        description="Alias cho time_of_day"
    )
    dose_quantity: int = Field(
        default=1,
        ge=1,
        description="Số viên mỗi lần uống"
    )
    pill_count: int | None = Field(
        default=None,
        description="Alias cho dose_quantity"
    )

    @model_validator(mode="before")
    @classmethod
    def resolve_aliases(cls, data):
        if isinstance(data, dict):
            if not data.get("time_of_day") and data.get("time"):
                data["time_of_day"] = data["time"]
            if not data.get("dose_quantity") and data.get("pill_count"):
                data["dose_quantity"] = data["pill_count"]
        return data

    @field_validator("time_of_day")
    @classmethod
    def validate_time_format(cls, v: str | None) -> str:
        """Kiểm tra định dạng HH:MM hợp lệ chuẩn 24h (giờ 00-23, phút 00-59)."""
        if not v:
            raise ValueError("Giờ uống thuốc (time_of_day) không được để trống.")
        pattern = r"^([01]\d|2[0-3]):[0-5]\d$"
        if not re.match(pattern, v):
            raise ValueError(
                f"'{v}' không hợp lệ. Định dạng yêu cầu: 'HH:MM' chuẩn 24 giờ (00:00 – 23:59, khung giờ 0 đến 23)"
            )
        return v

    model_config = {
        "json_schema_extra": {
            "example": {
                "slot_number": 1,
                "time_of_day": "08:00",
                "dose_quantity": 1
            }
        }
    }


class ScheduleResponse(BaseModel):
    id: int
    device_id: str
    slot_number: int
    time_of_day: str
    label: str              # Buổi Sáng / Buổi Trưa / Buổi Chiều / Buổi Tối (auto-computed)
    dose_quantity: int
    is_active: bool
    medicine_name: str | None     # Tên thuốc hiện đang gán vào slot (từ JOIN)
    medicine_dosage_unit: str | None
    created_at: str
    updated_at: str

    model_config = {"from_attributes": True}


class ScheduleSlotItem(BaseModel):
    slot_number: int = Field(..., ge=1, le=7, description="Ngăn thuốc (1..7)")
    dose_quantity: int = Field(default=1, ge=1, le=10, description="Số viên mỗi lần uống")


class ScheduleBatchCreate(BaseModel):
    time_of_day: str | None = Field(
        default=None,
        description="Giờ uống thuốc — định dạng 24h: 'HH:MM' (00:00 – 23:59)"
    )
    time: str | None = Field(
        default=None,
        description="Alias cho time_of_day"
    )
    slots: list[ScheduleSlotItem] = Field(
        ...,
        min_length=1,
        description="Danh sách các ngăn được chọn tương ứng với mốc giờ này"
    )

    @model_validator(mode="before")
    @classmethod
    def resolve_aliases(cls, data):
        if isinstance(data, dict):
            if not data.get("time_of_day") and data.get("time"):
                data["time_of_day"] = data["time"]
        return data

    @field_validator("time_of_day")
    @classmethod
    def validate_time_format(cls, v: str | None) -> str:
        if not v:
            raise ValueError("Giờ uống thuốc (time_of_day) không được để trống.")
        pattern = r"^([01]\d|2[0-3]):[0-5]\d$"
        if not re.match(pattern, v):
            raise ValueError(
                f"'{v}' không hợp lệ. Định dạng yêu cầu: 'HH:MM' chuẩn 24 giờ (00:00 – 23:59)"
            )
        return v


class ScheduleBatchResponse(BaseModel):
    time_of_day: str
    label: str
    created_count: int
    schedules: list[ScheduleResponse]

