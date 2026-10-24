"""
api/device.py
──────────────
Device Control & Diagnostics API Router.

Endpoints:
  GET  /api/v1/device/status     — Trạng thái thời gian thực ESP32 (từ RAM)
  GET  /api/v1/device/slots      — Tất cả 8 ngăn với thông tin thuốc + tồn kho
  GET  /api/v1/device/dashboard  — Dữ liệu tổng hợp: device + slots + phiên + lịch sử hôm nay
  POST /api/v1/device/homing     — Kích hoạt Auto-Homing TCRT5000 (CALIBRATE)
  POST /api/v1/device/move/{slot} — Di chuyển thủ công đến slot (chỉ dùng test/debug)

PHÂN BIỆT MOVE vs DISPENSE:
  - MOVE:    Chỉ xoay motor, không mở cơ chế lấy thuốc, không tạo dose_log
  - DISPENSE: Xoay motor + chờ lấy thuốc + tạo dose_log + trừ tồn kho

Toàn bộ luồng điều khiển phần cứng ủy quyền cho dispensing_engine
hoặc gửi trực tiếp MQTT qua mqtt_client (chỉ cho MOVE thuần túy).
"""

from datetime import datetime, timezone
from typing import Annotated

from fastapi import APIRouter, HTTPException, Path, Query
from pydantic import BaseModel, Field

from api.dependencies import DB
from config import settings
from database.repositories import dose_log_repo, slot_repo, schedule_repo
from device.device_state import device_state
from device.mqtt_client import mqtt_client
from device.protocol import topic_cmd, build_move_cmd, build_home_cmd
from schemas.device import DeviceStatusResponse, MoveCommandResponse
from services.dispensing_engine import dispensing_engine
from ws_manager import ws_manager

router = APIRouter(
    prefix="/api/v1/device",
    tags=["Device"],
)

DEVICE_ID = settings.DEVICE_ID


@router.get(
    "/status",
    response_model=DeviceStatusResponse,
    summary="Trạng thái thời gian thực ESP32",
    description=(
        "Trả về trạng thái ESP32 đọc trực tiếp từ RAM (không query database).\n\n"
        "Cập nhật tức thì mỗi khi nhận MQTT event từ ESP32.\n\n"
        "**FSM States:**\n"
        "- `OFFLINE` — Chưa kết nối MQTT\n"
        "- `HOMING_SEARCH` — Đang rà tìm Slot 0 (TCRT5000)\n"
        "- `SAFE_HOME_IDLE` — Đứng yên tại Slot 0, sẵn sàng\n"
        "- `ROTATING` — Đang xoay motor\n"
        "- `WAITING_CONFIRM` — Chờ người dùng lấy thuốc\n"
        "- `ERROR_JAMMED` — Lỗi kẹt cơ"
    ),
)
async def get_device_status():
    return device_state.to_dict()


@router.get(
    "/slots",
    summary="Tất cả 8 ngăn thuốc",
    description=(
        "Trả về danh sách 8 ngăn vật lý, sắp xếp theo `slot_number`.\n\n"
        "Mỗi ngăn bao gồm:\n"
        "- `medicine`: Thông tin thuốc đang nạp (null nếu trống)\n"
        "- `current_quantity` / `capacity`: Tồn kho hiện tại / sức chứa tối đa\n"
        "- `is_active`: Ngăn có đang kích hoạt không\n\n"
        "**Slot 0** (Safe Home) luôn có `medicine=null`, `capacity=0`."
    ),
)
async def get_all_slots(db: DB):
    return await slot_repo.get_all_slots(db, DEVICE_ID)


@router.get(
    "/dashboard",
    summary="Dữ liệu tổng hợp Dashboard",
    description=(
        "Endpoint tổng hợp cho Web Dashboard — trả về toàn bộ dữ liệu cần thiết\n"
        "trong **1 request duy nhất** để giảm round trips.\n\n"
        "Bao gồm:\n"
        "- `device`: Trạng thái ESP32 thời gian thực\n"
        "- `slots`: 8 ngăn với tồn kho và thông tin thuốc\n"
        "- `active_session`: Phiên cấp thuốc đang diễn ra (nếu có)\n"
        "- `schedules`: Tất cả lịch uống thuốc\n"
        "- `recent_logs`: 20 nhật ký uống thuốc mới nhất"
    ),
)
async def get_dashboard(db: DB):
    # Chạy song song các query DB để tối ưu thời gian
    slots = await slot_repo.get_all_slots(db, DEVICE_ID)
    schedules = await schedule_repo.get_all(db, DEVICE_ID)
    recent_logs = await dose_log_repo.get_history(db, DEVICE_ID, limit=20)

    return {
        "device": device_state.to_dict(),
        "slots": slots,
        "active_session": dispensing_engine.get_session_dict(),
        "schedules": schedules,
        "recent_logs": recent_logs,
        "server_time": datetime.now(timezone.utc).isoformat(),
    }


@router.post(
    "/homing",
    status_code=202,
    summary="Kích hoạt Auto-Homing (TCRT5000)",
    description=(
        "Gửi lệnh `CALIBRATE` xuống ESP32.\n\n"
        "ESP32 sẽ:\n"
        "1. Xoay motor chậm theo chiều CW\n"
        "2. Đọc cảm biến TCRT5000 liên tục\n"
        "3. Dừng khi phát hiện vạch phản xạ (Slot 0)\n"
        "4. Gửi `HOME_REACHED` event về backend\n\n"
        "**503** nếu ESP32 chưa kết nối MQTT."
    ),
)
async def trigger_homing():
    await dispensing_engine.start_calibration()
    return {
        "message": "Lệnh CALIBRATE đã gửi. ESP32 đang rà tìm Slot 0...",
        "status": "accepted"
    }


@router.post(
    "/move/{slot_number}",
    response_model=MoveCommandResponse,
    status_code=202,
    summary="Di chuyển thủ công (Debug/Test)",
    description=(
        "Gửi lệnh `MOVE` xuống ESP32 để xoay khay đến slot chỉ định.\n\n"
        "> **⚠️ Chỉ dùng cho Debug/Testing phần cứng.**\n\n"
        "**Khác với DISPENSE:**\n"
        "- Không mở cơ chế lấy thuốc\n"
        "- Không tạo dose_log\n"
        "- Không trừ tồn kho\n"
        "- Không kích hoạt watchdog 15 phút\n\n"
        "**Tiền điều kiện:** ESP32 online. Không cần SAFE_HOME_IDLE.\n"
        "**503** nếu ESP32 chưa kết nối."
    ),
)
async def manual_move(
    slot_number: Annotated[int, Path(ge=0, le=7, description="Slot đích (0..7)")]
):
    if not device_state.is_online:
        from fastapi import HTTPException
        raise HTTPException(
            status_code=503,
            detail="ESP32 chưa kết nối. Không thể gửi lệnh MOVE."
        )

    cmd = build_move_cmd(slot_number)
    success = await mqtt_client.publish(topic_cmd(DEVICE_ID, "move"), cmd)

    return MoveCommandResponse(
        accepted=success,
        target_slot=slot_number,
        message=(
            f"Lệnh MOVE → Slot {slot_number} đã gửi."
            if success
            else f"Gửi lệnh thất bại — MQTT không kết nối."
        )
    )


@router.post(
    "/mock-event",
    summary="Giả lập sự kiện phần cứng / MQTT (hỗ trợ kiểm thử và demo E2E)",
)
async def mock_hardware_event(payload: dict):
    event_type = payload.get("type", "")
    data = payload.get("data", {})

    if event_type == "device_online":
        device_state.set_online()
        device_state.update_fsm("SAFE_HOME_IDLE")
        device_state.update_slot(0)
        device_state.update_telemetry(rssi=-55, free_heap=192000, firmware="v1.2.0-mock", uptime=7200)
        await ws_manager.broadcast("device_online", device_state.to_dict())

    elif event_type == "slot_reached":
        slot = int(data.get("slot", 3))
        device_state.update_slot(slot)
        device_state.update_fsm("WAITING_CONFIRM")
        if dispensing_engine.has_active_session:
            await dispensing_engine.on_slot_reached({"slot": slot})
        else:
            await ws_manager.broadcast("slot_reached", {"slot": slot})

    elif event_type == "home_reached":
        device_state.update_slot(0)
        device_state.update_fsm("SAFE_HOME_IDLE")
        if dispensing_engine.has_active_session:
            await dispensing_engine.on_home_reached({"slot": 0})
        else:
            await ws_manager.broadcast("home_reached", {"slot": 0})

    elif event_type == "taken":
        if dispensing_engine.has_active_session:
            await dispensing_engine.confirm_taken_manual()
        else:
            await ws_manager.broadcast("taken", data)

    elif event_type == "missed":
        device_state.update_slot(0)
        device_state.update_fsm("SAFE_HOME_IDLE")
        await ws_manager.broadcast("missed", data)

    else:
        await ws_manager.broadcast(event_type, data)

    return {
        "status": "ok",
        "broadcasted": event_type,
        "device": device_state.to_dict(),
    }


@router.post("/factory-reset", summary="Factory Reset toàn bộ hệ thống (Xóa DB & xóa NVS ESP32)")
async def factory_reset_all(db: DB):
    # 1. Xóa Database
    await db.execute("DELETE FROM dose_logs;")
    await db.execute("DELETE FROM schedules;")
    await db.execute("UPDATE slots SET medicine_id = NULL, current_quantity = 0 WHERE device_id = ?;", (DEVICE_ID,))
    await db.execute("DELETE FROM medicines;")
    await db.commit()

    # 2. Phát lệnh MQTT xóa NVS trên ESP32
    if device_state.is_online:
        await mqtt_client.publish(
            topic_cmd(DEVICE_ID, "factory_reset"),
            {"command": "FACTORY_RESET"}
        )

    # 3. Thông báo Web cập nhật
    slots = await slot_repo.get_all_slots(db, DEVICE_ID)
    await ws_manager.broadcast("data_cleared", {
        "device": device_state.to_dict(),
        "slots": slots,
        "schedules": [],
        "recent_logs": []
    })

    return {
        "status": "success",
        "message": "Đã xóa toàn bộ dữ liệu trong Database và bộ nhớ NVS của ESP32!"
    }


class RefillDoneRequest(BaseModel):
    slot: int = Field(..., ge=1, le=7, description="Số ngăn (1..7)")
    qty: int = Field(default=0, ge=0, le=100, description="Số lượng viên nạp")
    name: str = Field(default="", description="Tên thuốc")
    dose: int = Field(default=1, ge=1, le=10, description="Liều dùng mỗi lần uống")


@router.post(
    "/refill/start/{slot_number}",
    status_code=202,
    summary="Mở ngăn để nạp thuốc (Xoay động cơ đến slot)",
    description="Gửi lệnh REFILL xuống ESP32 để xoay ngăn chỉ định ra cửa nạp thuốc."
)
async def start_refill(
    slot_number: Annotated[int, Path(ge=1, le=7, description="Số ngăn nạp (1..7)")]
):
    if not device_state.is_online:
        raise HTTPException(
            status_code=503,
            detail="ESP32 chưa kết nối MQTT. Không thể điều khiển xoay khay."
        )

    cmd = {
        "cmd": "REFILL_START",
        "slot": slot_number,
        "target_slot": slot_number,
        "timestamp": datetime.now(timezone.utc).isoformat()
    }
    success = await mqtt_client.publish(topic_cmd(DEVICE_ID, "refill"), cmd)
    if success:
        device_state.update_slot(slot_number)
        device_state.update_fsm("REFILL_MODE")
        await ws_manager.broadcast("refill_start", {"slot": slot_number, **device_state.to_dict()})

    return {
        "accepted": success,
        "slot": slot_number,
        "message": f"Đang xoay khay thực tế đến Ngăn {slot_number} để nạp thuốc..."
    }


@router.post(
    "/refill/cancel",
    status_code=202,
    summary="Hủy nạp thuốc — Quay về Safe Home (Slot 0)",
)
async def cancel_refill():
    if device_state.is_online:
        await mqtt_client.publish(topic_cmd(DEVICE_ID, "refill_cancel"), {"cmd": "REFILL_CANCEL"})
        device_state.update_slot(0)
        device_state.update_fsm("SAFE_HOME_IDLE")
        await ws_manager.broadcast("home_reached", {"slot": 0, **device_state.to_dict()})

    return {"accepted": True, "message": "Đã gửi lệnh hủy nạp thuốc và hồi vị Safe Home (Slot 0)."}


@router.post(
    "/refill/done",
    status_code=202,
    summary="Hoàn tất nạp thuốc — Cập nhật Flash NVS ESP32 và khóa về Safe Home",
)
async def finish_refill(payload: RefillDoneRequest, db: DB):
    if device_state.is_online:
        cmd = {
            "cmd": "REFILL_DONE",
            "slot": payload.slot,
            "target_slot": payload.slot,
            "qty": payload.qty,
            "dose": payload.dose,
            "name": payload.name,
            "timestamp": datetime.now(timezone.utc).isoformat()
        }
        await mqtt_client.publish(topic_cmd(DEVICE_ID, "refill_done"), cmd)
        device_state.update_slot(0)
        device_state.update_fsm("SAFE_HOME_IDLE")
        await ws_manager.broadcast("home_reached", {"slot": 0, **device_state.to_dict()})

    await slot_repo.update_slot(db, DEVICE_ID, payload.slot, {
        "default_dose": payload.dose,
        "current_quantity": payload.qty,
    })

    return {"accepted": True, "message": f"Đã hoàn tất nạp Ngăn {payload.slot} (Liều: {payload.dose}v/lần) và khóa khay về Safe Home."}


