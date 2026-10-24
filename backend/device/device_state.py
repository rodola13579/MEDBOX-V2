"""
device/device_state.py
───────────────────────
Cache trạng thái thiết bị ESP32 trong RAM.

Đây là "nguồn sự thật thời gian thực" (real-time source of truth).
Dữ liệu được cập nhật mỗi khi MQTT client nhận event từ ESP32.

Các thành phần có thể đọc device_state bất kỳ lúc nào:
  - API router (Task 08): GET /api/v1/device/status
  - Dispensing service (Task 06): kiểm tra device online trước khi gửi lệnh
  - WebSocket broadcaster (Task 07): đẩy trạng thái lên Web

QUAN TRỌNG: Không lưu state này vào database — database lưu lịch sử,
RAM lưu trạng thái hiện tại.

FSM States (tham chiếu SYSTEM_SPECIFICATION.md):
  OFFLINE         — Chưa kết nối MQTT
  HOMING_SEARCH   — Đang rà tìm Slot 0 (TCRT5000)
  SAFE_HOME_IDLE  — Đứng yên tại Slot 0, sẵn sàng nhận lệnh
  ROTATING        — Đang xoay motor đến slot đích
  WAITING_CONFIRM — Đã đến slot, chờ người dùng lấy thuốc
  ERROR_JAMMED    — Lỗi kẹt cơ, cần can thiệp thủ công
"""

from dataclasses import dataclass, field
from datetime import datetime, timezone


@dataclass
class DeviceState:
    """Trạng thái thời gian thực của ESP32 trong RAM."""

    # ── Kết nối ───────────────────────────────────────────────────────────────
    is_online: bool = False
    last_seen: datetime | None = None

    # ── Máy trạng thái ────────────────────────────────────────────────────────
    fsm_state: str = "OFFLINE"     # Một trong các giá trị FSM States ở trên
    current_slot: int = 0           # Vị trí vật lý hiện tại của khay

    # ── Telemetry (cập nhật định kỳ từ ESP32) ─────────────────────────────────
    rssi: int | None = None         # Wi-Fi signal strength (dBm)
    free_heap: int | None = None    # RAM còn trống trên ESP32 (bytes)
    firmware: str | None = None     # Phiên bản firmware
    ip_address: str | None = None   # Địa chỉ IP ESP32 trong mạng LAN
    uptime: int | None = None       # Thời gian chạy kể từ lần boot cuối (giây)
    temperature: float | None = None # Nhiệt độ môi trường DHT11 (°C)
    humidity: float | None = None    # Độ ẩm không khí DHT11 (%RH)

    def _now(self) -> datetime:
        return datetime.now(timezone.utc)

    # ── Phương thức cập nhật trạng thái ──────────────────────────────────────
    def set_online(self) -> None:
        """Gọi khi nhận event ONLINE từ ESP32 (LWT connect hoặc STATUS online)."""
        self.is_online = True
        self.fsm_state = "HOMING_SEARCH"
        self.last_seen = self._now()

    def set_offline(self) -> None:
        """Gọi khi nhận LWT OFFLINE hoặc mất kết nối MQTT."""
        self.is_online = False
        self.fsm_state = "OFFLINE"
        # Giữ lại current_slot và last_seen để biết lần cuối ở đâu

    def update_slot(self, slot: int) -> None:
        """Gọi khi nhận SLOT_REACHED hoặc HOME_REACHED."""
        self.current_slot = slot
        self.last_seen = self._now()

    def update_fsm(self, state: str) -> None:
        """Gọi khi nhận STATUS event từ ESP32."""
        self.fsm_state = state
        self.last_seen = self._now()

    def set_jammed(self) -> None:
        """Gọi khi nhận ERROR event với code JAMMED."""
        self.fsm_state = "ERROR_JAMMED"
        self.last_seen = self._now()

    def update_telemetry(
        self,
        rssi: int | None = None,
        free_heap: int | None = None,
        firmware: str | None = None,
        ip_address: str | None = None,
        uptime: int | None = None,
        temperature: float | None = None,
        humidity: float | None = None,
    ) -> None:
        """Gọi khi nhận TELEMETRY event."""
        if rssi is not None:
            self.rssi = rssi
        if free_heap is not None:
            self.free_heap = free_heap
        if firmware is not None:
            self.firmware = firmware
        if ip_address is not None:
            self.ip_address = ip_address
        if uptime is not None:
            self.uptime = uptime
        if temperature is not None:
            self.temperature = temperature
        if humidity is not None:
            self.humidity = humidity
        self.last_seen = self._now()

    def to_dict(self) -> dict:
        """Chuyển toàn bộ state thành dict — dùng cho API response và WebSocket broadcast."""
        return {
            "is_online": self.is_online,
            "fsm_state": self.fsm_state,
            "current_slot": self.current_slot,
            "last_seen": self.last_seen.isoformat() if self.last_seen else None,
            "rssi": self.rssi,
            "free_heap": self.free_heap,
            "firmware": self.firmware,
            "ip_address": self.ip_address,
            "uptime": self.uptime,
            "temperature": self.temperature,
            "humidity": self.humidity,
        }


# ── Singleton toàn cục ────────────────────────────────────────────────────────
# Import và dùng trực tiếp:
#   from device.device_state import device_state
#   device_state.set_online()
device_state = DeviceState()
