"""
schemas/device.py
──────────────────
Pydantic response models cho Device Control & Diagnostics API.
"""

from pydantic import BaseModel


class DeviceStatusResponse(BaseModel):
    """Trạng thái thời gian thực của ESP32 (đọc từ RAM, không query DB)."""
    is_online: bool
    fsm_state: str
    current_slot: int
    last_seen: str | None
    rssi: int | None
    free_heap: int | None
    firmware: str | None
    ip_address: str | None
    uptime: int | None
    temperature: float | None = None
    humidity: float | None = None

    model_config = {"from_attributes": True}


class MoveCommandResponse(BaseModel):
    """Kết quả gửi lệnh di chuyển thủ công."""
    accepted: bool
    target_slot: int
    message: str
