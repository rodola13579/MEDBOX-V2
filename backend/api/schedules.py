"""
api/schedules.py
─────────────────
Router cho Schedule API.

Endpoints:
  GET    /api/v1/schedules              — Danh sách lịch (kèm tên thuốc)
  POST   /api/v1/schedules              — Tạo lịch mới (label tự động tính)
  GET    /api/v1/schedules/{id}         — Chi tiết 1 lịch
  PATCH  /api/v1/schedules/{id}/toggle  — Bật/tắt lịch
  DELETE /api/v1/schedules/{id}         — Xóa lịch
"""

from typing import Annotated

from fastapi import APIRouter, Path, HTTPException

from api.dependencies import DB
from device.device_state import device_state
from schemas.schedule import (
    ScheduleCreate,
    ScheduleResponse,
    ScheduleBatchCreate,
    ScheduleBatchResponse,
)
from services import schedule_service

router = APIRouter(
    prefix="/api/v1/schedules",
    tags=["Schedules"],
)


@router.get(
    "",
    response_model=list[ScheduleResponse],
    summary="Danh sách lịch uống thuốc",
    description=(
        "Trả về tất cả lịch uống thuốc, sắp xếp theo giờ tăng dần.\n\n"
        "Mỗi lịch bao gồm:\n"
        "- `label`: Buổi Sáng / Buổi Trưa / Buổi Chiều / Buổi Tối (tự động tính)\n"
        "- `medicine_name`: Tên thuốc đang gán vào ngăn đó (nếu có)\n"
        "- `is_active`: Trạng thái bật/tắt"
    ),
)
async def list_schedules(db: DB):
    return await schedule_service.list_schedules(db)


@router.post(
    "",
    response_model=ScheduleResponse,
    status_code=201,
    summary="Tạo lịch uống thuốc mới (Đơn lẻ)",
    description=(
        "Tạo lịch uống thuốc lặp lại hằng ngày.\n\n"
        "**Quy tắc:**\n"
        "- `slot_number` phải từ **1 đến 7** (Slot 0 = Safe Home, không được lập lịch).\n"
        "- `time_of_day` định dạng **'HH:MM'** (24h), ví dụ: `'08:00'`, `'21:30'`.\n"
        "- Nhãn `label` (Buổi Sáng/Trưa/Chiều/Tối) được **tự động tính** — không cần truyền.\n"
        "- **409 Conflict** nếu cùng slot đã có lịch tại cùng mốc giờ.\n"
        "- **503 Service Unavailable** nếu thiết bị đang ngoại tuyến (Offline)."
    ),
)
async def create_schedule(payload: ScheduleCreate, db: DB):
    if not device_state.is_online:
        raise HTTPException(
            status_code=503,
            detail="Hộp thuốc đang ngoại tuyến (Offline). Bạn chỉ có thể cài đặt lịch uống thuốc khi thiết bị kết nối Wi-Fi."
        )
    return await schedule_service.create_schedule(db, payload)


@router.post(
    "/batch",
    response_model=ScheduleBatchResponse,
    status_code=201,
    summary="Đặt lịch uống thuốc theo mốc giờ và danh sách ngăn (Đồng bộ ESP32)",
    description=(
        "Quy trình đồng bộ chuẩn với phần cứng:\n"
        "1. Thiết lập giờ uống (time_of_day).\n"
        "2. Tích chọn các ngăn tương ứng (slots) và liều dùng.\n"
        "3. Tự động tính bitmask và phát MQTT đồng bộ xuống ESP32.\n"
        "4. **503 Service Unavailable** nếu thiết bị đang ngoại tuyến (Offline)."
    ),
)
async def create_schedule_batch(payload: ScheduleBatchCreate, db: DB):
    if not device_state.is_online:
        raise HTTPException(
            status_code=503,
            detail="Hộp thuốc đang ngoại tuyến (Offline). Bạn chỉ có thể cài đặt lịch uống thuốc khi thiết bị kết nối Wi-Fi."
        )
    return await schedule_service.create_batch_schedules(db, payload)


@router.get(
    "/{schedule_id}",
    response_model=ScheduleResponse,
    summary="Chi tiết 1 lịch",
)
async def get_schedule(
    schedule_id: Annotated[int, Path(ge=1)],
    db: DB,
):
    return await schedule_service.get_schedule_or_404(db, schedule_id)


@router.patch(
    "/{schedule_id}/toggle",
    response_model=ScheduleResponse,
    summary="Bật / Tắt lịch",
    description=(
        "Lật cờ `is_active` của lịch:\n"
        "- `true` → `false`: Tắt lịch (scheduler sẽ bỏ qua cữ này)\n"
        "- `false` → `true`: Bật lại lịch\n"
        "- **503 Service Unavailable** nếu thiết bị đang ngoại tuyến (Offline)."
    ),
)
async def toggle_schedule(
    schedule_id: Annotated[int, Path(ge=1)],
    db: DB,
):
    if not device_state.is_online:
        raise HTTPException(
            status_code=503,
            detail="Hộp thuốc đang ngoại tuyến (Offline). Không thể bật/tắt lịch khi thiết bị mất kết nối."
        )
    return await schedule_service.toggle_schedule(db, schedule_id)


@router.delete(
    "/{schedule_id}",
    status_code=204,
    summary="Xóa lịch",
    description="Xóa lịch uống thuốc. **404** nếu không tìm thấy. **503** nếu thiết bị đang ngoại tuyến.",
)
async def delete_schedule(
    schedule_id: Annotated[int, Path(ge=1)],
    db: DB,
):
    if not device_state.is_online:
        raise HTTPException(
            status_code=503,
            detail="Hộp thuốc đang ngoại tuyến (Offline). Không thể xóa lịch khi thiết bị mất kết nối."
        )
    await schedule_service.delete_schedule(db, schedule_id)


@router.post(
    "/sync",
    summary="Kích hoạt đồng bộ lịch xuống ESP32",
    description="Chủ động phát lệnh SYNC_SCHEDULES xuống phần cứng ESP32 qua MQTT.",
)
async def sync_schedules_manually(db: DB):
    if not device_state.is_online:
        raise HTTPException(
            status_code=503,
            detail="Hộp thuốc đang ngoại tuyến (Offline). Không thể đồng bộ lịch khi thiết bị chưa kết nối Wi-Fi."
        )
    ok = await schedule_service.sync_schedules_to_esp32(db)
    all_schedules = await schedule_service.list_schedules(db)
    return {
        "status": "success" if ok else "warning",
        "synced": ok,
        "count": len(all_schedules),
        "message": "Đã phát lệnh đồng bộ lịch xuống hộp thuốc thành công!" if ok else "Đã lưu nhưng hộp thuốc chưa kết nối MQTT."
    }

