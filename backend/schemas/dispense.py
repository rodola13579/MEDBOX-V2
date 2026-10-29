"""
schemas/dispense.py
────────────────────
Pydantic models cho Dispensing Engine API.

Ánh xạ với dose_logs table trong schema.sql:
  status: PENDING → DISPENSED → TAKEN / MISSED / JAMMED
  trigger_source: AUTO_SCHEDULE | EARLY_MANUAL | LOCAL_BUTTON | EMERGENCY_HOME
"""

from datetime import datetime
from typing import Literal

from pydantic import BaseModel, Field


# Giá trị hợp lệ — phải khớp CHECK constraint trong schema.sql
StatusType = Literal["PENDING", "DISPENSED", "TAKEN", "EARLY_TAKEN", "MISSED", "JAMMED"]
TriggerType = Literal["AUTO_SCHEDULE", "EARLY_MANUAL", "LOCAL_BUTTON", "EMERGENCY_HOME"]


class ManualDispenseRequest(BaseModel):
    """Request body cho POST /api/v1/dispense (lấy thuốc sớm thủ công)."""
    slot_number: int = Field(..., ge=1, le=7, description="Ngăn cần cấp thuốc (1..7)")
    schedule_id: int | None = Field(
        default=None,
        description="ID lịch liên kết (None = lấy sớm không có lịch)"
    )

    model_config = {
        "json_schema_extra": {
            "example": {"slot_number": 1, "schedule_id": None}
        }
    }


class EarlySessionRequest(BaseModel):
    """Request body cho POST /api/v1/dispense/early-session (lấy thuốc sớm theo Buổi)."""
    slots: list[int] = Field(..., min_length=1, description="Danh sách các ngăn cần cấp thuốc")

    model_config = {
        "json_schema_extra": {
            "example": {"slots": [1, 2]}
        }
    }



class DoseLogResponse(BaseModel):
    """Phản hồi đầy đủ thông tin 1 dose_log (nhật ký cữ uống)."""
    id: int
    schedule_id: int | None
    device_id: str
    slot_number: int
    scheduled_time: str
    dispensed_time: str | None
    confirmed_time: str | None
    status: StatusType
    trigger_source: TriggerType
    notes: str | None
    created_at: str
    medicine_name: str | None = None
    medicine_dosage_unit: str | None = None
    schedule_time: str | None = None
    schedule_label: str | None = None

    model_config = {"from_attributes": True}


class ActiveSessionResponse(BaseModel):
    """Trạng thái phiên cấp thuốc đang diễn ra (dùng cho Web polling)."""
    is_active: bool
    dose_log_id: int | None = None
    slot_number: int | None = None
    status: str | None = None           # PENDING | DISPENSED
    trigger_source: str | None = None
    started_at: str | None = None
    timeout_seconds: int | None = None
    seconds_remaining: int | None = None
