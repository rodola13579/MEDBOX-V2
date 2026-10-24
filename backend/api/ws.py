"""
api/ws.py
──────────
WebSocket endpoint — Real-time communication giữa Backend và Web Frontend.

Endpoint duy nhất: ws://localhost:8000/ws

Lifecycle mỗi kết nối:
  1. Client kết nối (ws.accept)
  2. Server gửi snapshot trạng thái hiện tại (connected message)
  3. Vòng lặp:
       - Server gửi ping mỗi 30 giây (application-level keepalive)
       - Client có thể gửi pong hoặc request snapshot
  4. Client ngắt kết nối → tự dọn dẹp

Các loại message SERVER → CLIENT:
  connected     — Snapshot đầy đủ ngay khi kết nối
  ping          — Keepalive mỗi 30 giây
  device_online — ESP32 vừa kết nối MQTT
  device_offline— ESP32 mất kết nối
  device_status — Thay đổi trạng thái FSM
  telemetry     — RSSI, RAM, uptime từ ESP32
  dispense_started — Bắt đầu phiên cấp thuốc
  slot_reached  — Khay xoay đến slot đích
  home_reached  — Khay về Slot 0
  taken         — Người dùng đã lấy thuốc
  missed        — Hết timeout, bỏ lỡ cữ
  jammed        — Lỗi kẹt cơ ESP32
  error         — Lỗi chung

Các loại message CLIENT → SERVER:
  ping          — Client keepalive
  pong          — Response cho server ping
  get_status    — Yêu cầu gửi lại snapshot trạng thái
"""

import asyncio
import json
import logging

from fastapi import APIRouter, WebSocket
from starlette.websockets import WebSocketDisconnect, WebSocketState

from device.device_state import device_state
from services.dispensing_engine import dispensing_engine
from ws_manager import ws_manager, WS_CONNECTED, WS_PING, WS_PONG, WS_DEVICE_STATUS

logger = logging.getLogger(__name__)

router = APIRouter(tags=["WebSocket"])

# Gửi server ping sau mỗi 30 giây nếu client không gửi gì
PING_INTERVAL_SECONDS = 30


@router.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    """
    WebSocket endpoint cho real-time updates.

    **Kết nối:** `ws://localhost:8000/ws`

    Ngay khi kết nối thành công, server gửi snapshot đầy đủ trạng thái
    thiết bị và phiên cấp thuốc đang diễn ra (nếu có).
    """
    await ws_manager.connect(websocket)
    try:
        # ── Gửi snapshot trạng thái hiện tại ngay khi kết nối ───────────────
        initial_state = {
            "device": device_state.to_dict(),
            "active_session": dispensing_engine.get_session_dict(),
            "connections": ws_manager.connection_count,
            "server": "Smart Pillbox Backend v1.0.0",
        }
        await ws_manager.send_to(websocket, WS_CONNECTED, initial_state)
        logger.debug(f"Sent initial snapshot to {websocket.client}")

        # ── Vòng lặp nhận message từ client ──────────────────────────────────
        while True:
            try:
                # Chờ message từ client, timeout sau PING_INTERVAL_SECONDS
                raw = await asyncio.wait_for(
                    websocket.receive_text(),
                    timeout=PING_INTERVAL_SECONDS
                )
                await _handle_client_message(websocket, raw)

            except asyncio.TimeoutError:
                # Không nhận được gì từ client → server gửi ping để kiểm tra
                if websocket.client_state == WebSocketState.CONNECTED:
                    await ws_manager.send_to(
                        websocket, WS_PING,
                        {"connections": ws_manager.connection_count}
                    )
                else:
                    break

            except WebSocketDisconnect:
                logger.debug(f"Client {websocket.client} ngắt kết nối.")
                break

    except WebSocketDisconnect:
        pass
    except Exception as e:
        logger.error(f"WebSocket error ({websocket.client}): {e}", exc_info=True)
    finally:
        await ws_manager.disconnect(websocket)


async def _handle_client_message(ws: WebSocket, raw: str) -> None:
    """Xử lý message JSON nhận được từ client."""
    try:
        msg = json.loads(raw)
    except json.JSONDecodeError:
        logger.debug(f"WS: Non-JSON message from {ws.client}: {raw[:60]}")
        return

    msg_type = msg.get("type", "")

    if msg_type == "ping":
        # Client ping → server pong
        await ws_manager.send_to(ws, WS_PONG, {})

    elif msg_type == "pong":
        # Client phản hồi server ping — không cần làm gì
        pass

    elif msg_type == "get_status":
        # Client yêu cầu snapshot trạng thái hiện tại
        await ws_manager.send_to(ws, WS_DEVICE_STATUS, {
            "device": device_state.to_dict(),
            "active_session": dispensing_engine.get_session_dict(),
            "connections": ws_manager.connection_count,
        })

    else:
        logger.debug(f"WS: Unknown message type '{msg_type}' from {ws.client}")
