"""
main.py
───────
Entry point của FastAPI Backend — Smart Pillbox.

Để chạy:
    cd backend
    uvicorn main:app --reload --host 0.0.0.0 --port 8000

Lifespan lifecycle:
    STARTUP  → Migrations → Seed → MQTT connect → Scheduler
    SHUTDOWN → MQTT disconnect → Scheduler shutdown
"""

import sys
import asyncio

# Khắc phục lỗi ProactorEventLoop trên Windows cho aiomqtt
if sys.platform == "win32":
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())

import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from config import settings
from database.migrations import run_migrations, seed_database
from device.mqtt_client import mqtt_client
from device.device_state import device_state
from services.dispensing_engine import dispensing_engine
from ws_manager import ws_manager
from api.medicines import router as medicines_router
from api.slots import router as slots_router
from api.schedules import router as schedules_router
from api.dispensing import router as dispensing_router
from api.ws import router as ws_router
from api.device import router as device_router
from services.scheduler_engine import scheduler_engine

# ── Cấu hình logging ──────────────────────────────────────────────────────────
logging.basicConfig(
    level=logging.DEBUG if settings.DEBUG else logging.INFO,
    format="%(asctime)s | %(levelname)-8s | %(name)-30s | %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
)
logger = logging.getLogger(__name__)


# ── Lifespan (Startup / Shutdown) ─────────────────────────────────────────────
@asynccontextmanager
async def lifespan(app: FastAPI):
    """
    Quản lý vòng đời ứng dụng.
    Code trước `yield` chạy khi STARTUP.
    Code sau `yield` chạy khi SHUTDOWN.
    """
    # ── STARTUP ──────────────────────────────────────────────────────────────
    logger.info("=" * 60)
    logger.info("  SMART PILLBOX BACKEND — Đang khởi động")
    logger.info("=" * 60)
    logger.info(f"  Device ID   : {settings.DEVICE_ID}")
    logger.info(f"  Database    : {settings.DB_PATH}")
    logger.info(f"  MQTT Broker : {settings.MQTT_BROKER}:{settings.MQTT_PORT}")
    logger.info(f"  Debug Mode  : {settings.DEBUG}")

    # 1. Khởi tạo schema database
    await run_migrations()

    # 2. Nạp dữ liệu mẫu nếu database rỗng
    await seed_database()

    # 3. Khởi động MQTT Client — kết nối Broker, bắt đầu nhận events từ ESP32
    await mqtt_client.start()

    # 4. Gắn MQTT callbacks vào Dispensing Engine
    #    (phải đăng ký SAU khi mqtt_client khởi động — tránh circular import)
    mqtt_client.on_slot_reached   = dispensing_engine.on_slot_reached
    mqtt_client.on_home_reached   = dispensing_engine.on_home_reached
    mqtt_client.on_taken_button   = dispensing_engine.on_taken_button
    mqtt_client.on_error          = dispensing_engine.on_error
    logger.info("  MQTT callbacks → Dispensing Engine đã đăng ký.")

    # 5. Khởi động Scheduler Engine — quét lịch mỗi phút, xử lý hàng chờ
    await scheduler_engine.start()

    logger.info("  Backend sẵn sàng nhận kết nối.")
    logger.info("=" * 60)

    # Nhường quyền kiểm soát cho ứng dụng
    yield

    # ── SHUTDOWN ─────────────────────────────────────────────────────────────
    logger.info("Smart Pillbox Backend — Đang tắt server...")
    # 1. Dừng Scheduler trước — không tạo thêm session mới
    await scheduler_engine.stop()
    # 2. Dừng MQTT client — hủy background task
    await mqtt_client.stop()
    logger.info("Server đã tắt.")


# ── Khởi tạo FastAPI App ─────────────────────────────────────────────────────
app = FastAPI(
    title="Smart Pillbox API",
    description=(
        "Backend API cho Hệ thống Hộp thuốc Thông minh (Smart Pill Dispenser).\n\n"
        "Quản lý: Thuốc, Ngăn khay, Lịch uống thuốc, Cấp phát và Nhật ký.\n"
        "Giao tiếp: Web Frontend (REST + WebSocket) và ESP32 (MQTT)."
    ),
    version="1.0.0",
    lifespan=lifespan,
    docs_url="/docs",
    redoc_url="/redoc",
)

# ── CORS ─────────────────────────────────────────────────────────────────────
# Cho phép React dev server (Vite :5173) gọi API khi phát triển local
app.add_middleware(
    CORSMiddleware,
    allow_origins=[
        "http://localhost:5173",    # Vite dev server
        "http://localhost:3000",    # Alternative React port
        "http://127.0.0.1:5173",
    ],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── No-Cache cho Static Files để tránh lỗi cache trình duyệt khi cập nhật giao diện ──
@app.middleware("http")
async def add_no_cache_headers(request, call_next):
    response = await call_next(request)
    path = request.url.path
    if path == "/" or path.endswith((".html", ".js", ".css")):
        response.headers["Cache-Control"] = "no-cache, no-store, must-revalidate"
        response.headers["Pragma"] = "no-cache"
        response.headers["Expires"] = "0"
    return response

# ── Routers ───────────────────────────────────────────────────────────────────
app.include_router(medicines_router)   # Task 03
app.include_router(slots_router)       # Task 03
app.include_router(schedules_router)   # Task 04
app.include_router(dispensing_router)  # Task 06
app.include_router(ws_router)          # Task 07
app.include_router(device_router)      # Task 08
# Task 09: scheduler → lifespan startup


# ── Endpoints hệ thống ────────────────────────────────────────────────────────
@app.get("/health", tags=["System"], summary="Kiểm tra trạng thái server")
async def health_check():
    """
    Health check endpoint.
    Trả về 200 OK nếu server đang hoạt động bình thường.
    """
    return {
        "status": "ok",
        "service": "Smart Pillbox Backend",
        "version": "1.0.0",
        "device_id": settings.DEVICE_ID,
        "mqtt_connected": device_state.is_online,
        "device_fsm": device_state.fsm_state,
        "ws_connections": ws_manager.connection_count,
        "scheduler_running": scheduler_engine.is_running,
        "scheduler_queue": scheduler_engine.queue_size,
    }


@app.get("/api/info", tags=["System"], summary="Thông tin API")
async def api_info():
    """Thông tin cơ bản về API và đường dẫn tài liệu."""
    return {
        "message": "Smart Pillbox API đang hoạt động.",
        "docs": "/docs",
        "health": "/health",
    }


# ── Phục vụ giao diện Web tĩnh (Thư mục frontend/ độc lập ở cấp gốc) ──────────
import os
from fastapi.staticfiles import StaticFiles

# Thư mục frontend/ nằm ngang hàng với backend/ ở cấp thư mục gốc
ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FRONTEND_DIR = os.path.join(ROOT_DIR, "frontend")
if not os.path.exists(FRONTEND_DIR):
    os.makedirs(FRONTEND_DIR, exist_ok=True)

app.mount("/", StaticFiles(directory=FRONTEND_DIR, html=True), name="frontend")


if __name__ == "__main__":
    import uvicorn
    if sys.platform == "win32":
        asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
    uvicorn.run("main:app", host="0.0.0.0", port=8000, loop="asyncio", reload=False)


