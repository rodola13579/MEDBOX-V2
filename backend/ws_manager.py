"""
ws_manager.py
──────────────
WebSocket Connection Manager — phát sóng real-time đến tất cả trình duyệt đang kết nối.

Thiết kế:
  - Singleton `ws_manager` — các module khác import và gọi `broadcast()` trực tiếp
  - Thread-safe: dùng asyncio.Lock bảo vệ tập kết nối
  - Tự động dọn dẹp client bị ngắt kết nối (dead connection pruning)
  - Không block nếu không có client nào đang kết nối

Format tin nhắn WebSocket:
  {
    "type": "event_type",   -- Loại sự kiện (xem WS_TYPE_* constants bên dưới)
    "data": {...},           -- Payload tùy theo loại sự kiện
    "ts": "ISO 8601"         -- Timestamp UTC
  }

Sử dụng (trong bất kỳ module nào):
  from ws_manager import ws_manager
  await ws_manager.broadcast("device_status", device_state.to_dict())
"""

import asyncio
import json
import logging
from datetime import datetime, timezone

from fastapi import WebSocket
from starlette.websockets import WebSocketDisconnect

logger = logging.getLogger(__name__)

# ── Event type constants ───────────────────────────────────────────────────────
# Các loại sự kiện WebSocket — phải khớp với frontend (Task 10-13)
WS_CONNECTED       = "connected"        # Gửi ngay khi client vừa kết nối
WS_PING            = "ping"             # Server → Client keepalive
WS_PONG            = "pong"             # Client → Server response
WS_DEVICE_ONLINE   = "device_online"    # ESP32 kết nối MQTT
WS_DEVICE_OFFLINE  = "device_offline"   # ESP32 mất kết nối
WS_DEVICE_STATUS   = "device_status"    # Cập nhật trạng thái chung
WS_TELEMETRY       = "telemetry"        # RSSI, RAM, uptime...
WS_DISPENSE_START  = "dispense_started" # Bắt đầu phiên cấp thuốc
WS_SLOT_REACHED    = "slot_reached"     # Khay đã xoay đến đích
WS_HOME_REACHED    = "home_reached"     # Khay về Slot 0
WS_TAKEN           = "taken"            # Người dùng đã lấy thuốc
WS_MISSED          = "missed"           # Hết timeout
WS_JAMMED          = "jammed"           # Lỗi kẹt cơ
WS_ERROR           = "error"            # Lỗi chung từ ESP32
WS_SCHEDULE_UPDATED = "schedule_updated" # Cập nhật lịch trình từ ESP32 hoặc Web


def _now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


class WebSocketManager:
    """Quản lý tập hợp các WebSocket connection đang hoạt động."""

    def __init__(self):
        self._connections: set[WebSocket] = set()
        self._lock = asyncio.Lock()

    # ── Connection lifecycle ──────────────────────────────────────────────────
    async def connect(self, ws: WebSocket) -> None:
        """Accept và đăng ký WebSocket connection mới."""
        await ws.accept()
        async with self._lock:
            self._connections.add(ws)
        logger.info(
            f"WebSocket connected (total={len(self._connections)}, "
            f"client={ws.client})"
        )

    async def disconnect(self, ws: WebSocket) -> None:
        """Hủy đăng ký WebSocket connection."""
        async with self._lock:
            self._connections.discard(ws)
        logger.info(
            f"WebSocket disconnected (total={len(self._connections)}, "
            f"client={ws.client})"
        )

    @property
    def connection_count(self) -> int:
        """Số lượng client đang kết nối."""
        return len(self._connections)

    # ── Broadcasting ──────────────────────────────────────────────────────────
    async def broadcast(self, event_type: str, data: dict) -> None:
        """
        Gửi message đến TẤT CẢ client đang kết nối.
        Tự động loại bỏ các kết nối đã chết.
        Không làm gì nếu không có client nào.
        """
        if not self._connections:
            return

        payload = json.dumps(
            {"type": event_type, "data": data, "ts": _now_iso()},
            ensure_ascii=False,
            default=str
        )

        # Lấy snapshot tập connection để tránh race condition
        async with self._lock:
            connections_snapshot = set(self._connections)

        dead: set[WebSocket] = set()
        for ws in connections_snapshot:
            try:
                await ws.send_text(payload)
            except (WebSocketDisconnect, RuntimeError, Exception):
                dead.add(ws)

        # Dọn dẹp các kết nối đã chết
        if dead:
            async with self._lock:
                self._connections -= dead
            logger.debug(f"Pruned {len(dead)} dead WebSocket connection(s)")

        logger.debug(
            f"WS broadcast [{event_type}] → {len(connections_snapshot) - len(dead)} client(s)"
        )

    async def send_to(self, ws: WebSocket, event_type: str, data: dict) -> bool:
        """
        Gửi message chỉ đến 1 client cụ thể (dùng khi mới kết nối).
        Trả về True nếu gửi thành công.
        """
        try:
            payload = json.dumps(
                {"type": event_type, "data": data, "ts": _now_iso()},
                ensure_ascii=False,
                default=str
            )
            await ws.send_text(payload)
            return True
        except Exception as e:
            logger.warning(f"send_to failed: {e}")
            return False


# ── Singleton toàn cục ────────────────────────────────────────────────────────
# Import và dùng trực tiếp:
#   from ws_manager import ws_manager
#   await ws_manager.broadcast(WS_SLOT_REACHED, {"slot": 3})
ws_manager = WebSocketManager()
