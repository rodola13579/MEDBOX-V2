from pathlib import Path
from pydantic_settings import BaseSettings, SettingsConfigDict

BASE_DIR = Path(__file__).resolve().parent


class Settings(BaseSettings):
    """
    Cấu hình ứng dụng đọc từ biến môi trường hoặc file .env.
    Mọi giá trị đều có default an toàn cho môi trường dev local.
    """

    # ── Ứng dụng ──────────────────────────────────────────────────────────────
    APP_HOST: str = "0.0.0.0"
    APP_PORT: int = 8000
    DEBUG: bool = True

    # ── Cơ sở dữ liệu ─────────────────────────────────────────────────────────
    DB_PATH: str = str(BASE_DIR / "pillbox.db")

    # ── MQTT Broker ───────────────────────────────────────────────────────────
    MQTT_BROKER: str = "localhost"
    MQTT_PORT: int = 1883
    MQTT_USERNAME: str = ""
    MQTT_PASSWORD: str = ""
    MQTT_CLIENT_ID: str = "smartpillbox-backend"

    # ── Thiết bị ──────────────────────────────────────────────────────────────
    DEVICE_ID: str = "ESP32_PILLBOX_01"
    TOTAL_SLOTS: int = 8  # Slot 0 (Safe Home) + Slot 1..7 (ngăn thuốc)

    # ── Watchdog an toàn ──────────────────────────────────────────────────────
    # Thời gian tối đa chờ người dùng lấy thuốc (giây).
    # Hết thời gian → khay tự về Slot 0, ghi log MISSED.
    DISPENSE_TIMEOUT_SECONDS: int = 900  # 15 phút

    model_config = SettingsConfigDict(
        env_file=".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )


# Singleton instance — import và dùng trực tiếp:
#   from config import settings
settings = Settings()
DEVICE_ID = settings.DEVICE_ID
