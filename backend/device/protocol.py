"""
device/protocol.py
───────────────────
Định nghĩa giao thức MQTT giữa Backend và ESP32.

Bao gồm:
  1. Hằng số topic — tất cả MQTT topic patterns
  2. Command builders — tạo payload JSON gửi xuống ESP32
  3. Event parser — phân tích payload JSON nhận từ ESP32

QUY ƯỚC TOPIC (tham chiếu SYSTEM_SPECIFICATION.md § 5):
  Backend → ESP32 : pillbox/{device_id}/cmd/{command}
  ESP32 → Backend : pillbox/{device_id}/events/{event}
  LWT (kết nối)   : pillbox/{device_id}/lwt

Tất cả payload đều là JSON UTF-8.
"""

import json
import uuid
from datetime import datetime, timezone


# ── Topic Templates ──────────────────────────────────────────────────────────
def topic_cmd(device_id: str, command: str) -> str:
    """Tạo topic lệnh gửi xuống ESP32."""
    return f"pillbox/{device_id}/cmd/{command}"


def topic_events(device_id: str) -> str:
    """Wildcard topic subscribe nhận mọi event từ thiết bị cụ thể."""
    return f"pillbox/{device_id}/events/#"


def topic_lwt(device_id: str) -> str:
    """Topic Last Will and Testament — ESP32 tự gửi khi mất kết nối."""
    return f"pillbox/{device_id}/lwt"


def topic_state(device_id: str) -> str:
    """Wildcard topic subscribe nhận mọi bản tin state/telemetry từ thiết bị."""
    return f"pillbox/{device_id}/state/#"


def topic_all_events() -> str:
    """Wildcard subscribe nhận event từ TẤT CẢ thiết bị (dùng khi backend quản lý nhiều hộp)."""
    return "pillbox/+/events/#"


# ── Command Builders ─────────────────────────────────────────────────────────
def build_dispense_cmd(target_slot: int, timeout_seconds: int = 900, trigger_source: str = "EARLY_MANUAL") -> dict:
    """
    Lệnh xoay khay đến ngăn thuốc chỉ định.

    ESP32 sẽ:
      1. Xoay motor đến target_slot
      2. Gửi event SLOT_REACHED khi đến nơi
      3. Mở cơ chế lấy thuốc, chờ bấm nút hoặc timeout
    """
    return {
        "cmd": "DISPENSE_NOW",
        "target_slot": target_slot,
        "timeout": timeout_seconds,
        "trigger_source": trigger_source,
        "request_id": str(uuid.uuid4()),
        "timestamp": datetime.now(timezone.utc).isoformat()
    }


def build_home_cmd() -> dict:
    """
    Lệnh hồi vị khẩn cấp — Xoay khay về Slot 0 (Safe Home).

    Dùng khi:
      - Người dùng bấm nút "Về nhà" trên Web
      - Watchdog timeout 15 phút
      - Hủy lệnh cấp thuốc giữa chừng
    """
    return {
        "cmd": "RETURN_HOME_ZERO",
        "request_id": str(uuid.uuid4()),
        "timestamp": datetime.now(timezone.utc).isoformat()
    }


def build_calibrate_cmd() -> dict:
    """
    Lệnh Auto-Homing — Kích hoạt cảm biến TCRT5000 tìm vạch Slot 0.

    ESP32 sẽ:
      1. Rà chậm theo chiều CW
      2. Dừng khi TCRT5000 phát hiện vạch phản xạ
      3. Gửi event HOME_REACHED khi hoàn tất
    """
    return {
        "cmd": "CALIBRATE",
        "request_id": str(uuid.uuid4()),
        "timestamp": datetime.now(timezone.utc).isoformat()
    }


def build_move_cmd(target_slot: int) -> dict:
    """
    Lệnh di chuyển thủ công đến slot bất kỳ (dùng cho kiểm thử phần cứng).

    Khác với DISPENSE_NOW: không kích hoạt cơ chế lấy thuốc,
    chỉ xoay motor đến vị trí chỉ định.
    """
    return {
        "cmd": "MOVE",
        "target_slot": target_slot,
        "request_id": str(uuid.uuid4()),
        "timestamp": datetime.now(timezone.utc).isoformat()
    }


# ── Event Parser ─────────────────────────────────────────────────────────────
def parse_event(raw_payload: bytes | str) -> dict | None:
    """
    Phân tích payload JSON nhận từ ESP32.

    Trả về dict nếu parse thành công, None nếu payload không hợp lệ.
    Luôn xử lý lỗi json.JSONDecodeError — không để lỗi phần cứng crash backend.
    """
    try:
        if isinstance(raw_payload, bytes):
            raw_payload = raw_payload.decode("utf-8")
        return json.loads(raw_payload)
    except (json.JSONDecodeError, UnicodeDecodeError):
        return None


# ── Payload builders cho ESP32 phản hồi (dùng trong mock) ───────────────────
def build_slot_reached_event(slot: int) -> str:
    return json.dumps({
        "event": "SLOT_REACHED",
        "slot": slot,
        "timestamp": datetime.now(timezone.utc).isoformat()
    })


def build_home_reached_event() -> str:
    return json.dumps({
        "event": "HOME_REACHED",
        "slot": 0,
        "timestamp": datetime.now(timezone.utc).isoformat()
    })


def build_online_event(device_id: str) -> str:
    return json.dumps({
        "event": "STATUS",
        "status": "ONLINE",
        "state": "HOMING_SEARCH",
        "current_slot": 0,
        "timestamp": datetime.now(timezone.utc).isoformat()
    })


def build_offline_lwt() -> str:
    return json.dumps({
        "status": "OFFLINE",
        "timestamp": datetime.now(timezone.utc).isoformat()
    })
