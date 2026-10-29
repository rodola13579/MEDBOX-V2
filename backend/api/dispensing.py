"""
api/dispensing.py
──────────────────
Router cho Dispensing API.

Endpoints:
  POST /api/v1/dispense              — Lấy thuốc sớm (thủ công từ Web)
  GET  /api/v1/dispense/active       — Trạng thái phiên đang diễn ra
  POST /api/v1/dispense/confirm      — Xác nhận đã lấy thuốc qua Web
  POST /api/v1/dispense/home         — Hồi vị khẩn cấp (về Slot 0)
  POST /api/v1/dispense/calibrate    — Kích hoạt Auto-Homing TCRT5000
  GET  /api/v1/dispense/history      — Lịch sử nhật ký uống thuốc

LUỒNG THÔNG THƯỜNG (scheduler tự động — Task 09):
  scheduler → dispensing_engine.dispense_now() → MQTT → ESP32 → callbacks

LUỒNG THỦ CÔNG (người dùng từ Web):
  Web → POST /dispense → dispensing_engine.dispense_now() → ...
"""

from typing import Annotated

from fastapi import APIRouter, Query

from api.dependencies import DB
from database.repositories import dose_log_repo
from config import settings
from schemas.dispense import (
    ActiveSessionResponse,
    DoseLogResponse,
    ManualDispenseRequest,
    EarlySessionRequest,
)
from device.device_state import device_state
from services.dispensing_engine import dispensing_engine

router = APIRouter(
    prefix="/api/v1/dispense",
    tags=["Dispensing"],
)


@router.post(
    "",
    response_model=DoseLogResponse,
    status_code=202,
    summary="Lấy thuốc sớm (thủ công)",
    description=(
        "Kích hoạt phiên cấp thuốc thủ công từ Web.\n\n"
        "**Tiền điều kiện:**\n"
        "- ESP32 phải đang trực tuyến (`SAFE_HOME_IDLE`).\n"
        "- Slot phải có thuốc và còn tồn kho.\n"
        "- Không có phiên cấp thuốc nào đang chạy.\n\n"
        "**HTTP 202 Accepted**: Lệnh đã gửi xuống ESP32, chờ phản hồi qua WebSocket."
    ),
)
async def manual_dispense(payload: ManualDispenseRequest):
    return await dispensing_engine.dispense_now(
        slot_number=payload.slot_number,
        trigger_source="EARLY_MANUAL",
        schedule_id=payload.schedule_id,
    )


@router.post(
    "/early-session",
    response_model=DoseLogResponse,
    status_code=202,
    summary="Lấy thuốc sớm theo Buổi",
    description="Kích hoạt phiên cấp thuốc sớm cho danh sách slot theo buổi.",
)
async def early_session_dispense(payload: EarlySessionRequest):
    if not device_state.is_online:
        device_state.set_online()
        device_state.update_fsm("SAFE_HOME_IDLE")
        device_state.update_slot(0)

    combined_mask = sum(1 << (s - 1) for s in payload.slots if 1 <= s <= 7)
    first_slot = payload.slots[0]

    from device.mqtt_client import mqtt_client
    from device.protocol import topic_cmd
    from config import settings

    if device_state.is_online and len(payload.slots) > 1:
        await mqtt_client.publish(
            topic_cmd(settings.DEVICE_ID, "dispense"),
            {
                "command": "DISPENSE",
                "slot": first_slot,
                "target_slot": first_slot,
                "slots": payload.slots,
                "mask": combined_mask,
                "trigger_source": "EARLY_MANUAL",
                "session_label": payload.session_label,
                "timeout_seconds": settings.DISPENSE_TIMEOUT_SECONDS,
            }
        )

    return await dispensing_engine.dispense_now(
        slot_number=first_slot,
        trigger_source="EARLY_MANUAL",
    )



@router.get(
    "/active",
    response_model=ActiveSessionResponse,
    summary="Trạng thái phiên đang diễn ra",
    description="Trả về thông tin phiên cấp thuốc đang chạy (nếu có). Dùng để Web polling.",
)
async def get_active_session():
    return dispensing_engine.get_session_dict()


@router.post(
    "/confirm",
    response_model=DoseLogResponse,
    summary="Xác nhận đã lấy thuốc (Web)",
    description=(
        "Xác nhận người dùng đã lấy thuốc qua Web UI.\n\n"
        "Tương đương bấm nút vật lý trên hộp thuốc.\n"
        "- Cập nhật dose_log → **TAKEN**\n"
        "- Trừ 1 viên tồn kho\n"
        "- Gửi lệnh về Slot 0 (RETURN_HOME_ZERO)\n\n"
        "**404** nếu không có phiên nào đang chạy."
    ),
)
async def confirm_taken():
    return await dispensing_engine.confirm_taken_manual()


@router.post(
    "/confirm-dose",
    status_code=202,
    summary="Xác nhận đến uống thuốc cữ hẹn từ Web UI",
    description="Tương đương bấm nút PUSH / K0 trên hộp khi đang báo cữ hẹn 15 phút.",
)
async def confirm_dose():
    from device.mqtt_client import mqtt_client
    from device.protocol import topic_cmd
    from config import settings
    if device_state.is_online:
        await mqtt_client.publish(
            topic_cmd(settings.DEVICE_ID, "confirm_dose"),
            {"command": "CONFIRM_DOSE"}
        )
    return {"accepted": True, "message": "Đã gửi lệnh xác nhận uống thuốc xuống ESP32."}


@router.post(
    "/home",
    status_code=202,
    summary="Hồi vị khẩn cấp",
    description=(
        "Gửi lệnh RETURN_HOME_ZERO xuống ESP32.\n\n"
        "- Nếu có phiên đang chạy → cập nhật → **MISSED** (tồn kho KHÔNG thay đổi)\n"
        "- ESP32 xoay khay về Slot 0\n\n"
        "Dùng khi cần dừng khẩn cấp hoặc hết thời gian."
    ),
)
async def return_home():
    await dispensing_engine.return_home_now()
    return {"message": "Lệnh RETURN_HOME_ZERO đã gửi.", "status": "accepted"}


@router.post(
    "/calibrate",
    status_code=202,
    summary="Kích hoạt Auto-Homing (TCRT5000)",
    description=(
        "Gửi lệnh CALIBRATE xuống ESP32.\n\n"
        "ESP32 sẽ rà chậm tìm vạch phản xạ TCRT5000 và dừng tại Slot 0.\n"
        "Dùng khi khay bị lệch vị trí hoặc khi khởi động lần đầu."
    ),
)
async def calibrate():
    await dispensing_engine.start_calibration()
    return {"message": "Lệnh CALIBRATE đã gửi. Đang rà tìm Slot 0...", "status": "accepted"}


@router.get(
    "/history",
    response_model=list[DoseLogResponse],
    summary="Lịch sử nhật ký uống thuốc",
    description=(
        "Trả về lịch sử các cữ uống thuốc theo ngày, mới nhất trước.\n\n"
        "**Lọc theo ngày:** `date=YYYY-MM-DD`\n"
        "**Lọc theo status:** `TAKEN`, `EARLY_TAKEN`, `MISSED`\n"
        "**Giới hạn:** tối đa 200 bản ghi."
    ),
)
async def get_history(
    db: DB,
    limit: Annotated[int, Query(ge=1, le=200, description="Số bản ghi tối đa")] = 50,
    status: Annotated[str | None, Query(description="Lọc theo status")] = None,
    date: Annotated[str | None, Query(description="Lọc theo ngày (YYYY-MM-DD)")] = None,
):
    return await dose_log_repo.get_history(
        db,
        device_id=settings.DEVICE_ID,
        limit=limit,
        status_filter=status,
        date_filter=date,
    )
