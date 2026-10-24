"""
database/migrations.py
──────────────────────
Tự động khởi tạo và seed cơ sở dữ liệu khi ứng dụng khởi động.

Hàm run_migrations():
  - Đọc schema.sql và thực thi DDL
  - Dùng CREATE TABLE IF NOT EXISTS → an toàn khi gọi lại nhiều lần
  - Không bao giờ xóa dữ liệu hiện có

Hàm seed_database():
  - Nạp dữ liệu mẫu từ seed.sql
  - Chỉ chạy nếu bảng devices đang rỗng (tránh duplicate)
"""

import logging
from pathlib import Path

import aiosqlite

from config import settings

logger = logging.getLogger(__name__)

# Đường dẫn tuyệt đối tới các file SQL (tính từ vị trí của file này)
SCHEMA_FILE = Path(__file__).parent / "schema.sql"
SEED_FILE = Path(__file__).parent / "seed.sql"
DATABASE_PATH = Path(settings.DB_PATH)


async def run_migrations() -> None:
    """
    Khởi tạo schema database.

    Đọc schema.sql và thực thi bằng executescript().
    Sử dụng CREATE TABLE IF NOT EXISTS nên an toàn khi gọi lại nhiều lần.
    Foreign Keys tắt trong lúc tạo schema để tránh lỗi thứ tự, bật lại sau.
    """
    if not SCHEMA_FILE.exists():
        raise FileNotFoundError(
            f"Schema file không tìm thấy: {SCHEMA_FILE}\n"
            f"Hãy kiểm tra file backend/database/schema.sql tồn tại."
        )

    logger.info(f"Đang chạy migrations trên: {DATABASE_PATH.resolve()}")
    schema_sql = SCHEMA_FILE.read_text(encoding="utf-8")

    async with aiosqlite.connect(DATABASE_PATH) as db:
        # Tắt FK trong lúc tạo bảng để tránh phụ thuộc thứ tự CREATE
        await db.execute("PRAGMA foreign_keys = OFF")
        await db.executescript(schema_sql)

        # Migration an toàn: Thêm default_dose vào slots nếu chưa tồn tại
        try:
            await db.execute("ALTER TABLE slots ADD COLUMN default_dose INTEGER NOT NULL DEFAULT 1")
        except Exception:
            pass

        # Migration an toàn: Gỡ bỏ ràng buộc current_quantity <= capacity trên bảng slots
        try:
            cur = await db.execute("SELECT sql FROM sqlite_master WHERE type='table' AND name='slots'")
            row = await cur.fetchone()
            if row and "current_quantity <= capacity" in row[0]:
                await db.execute("""
                    CREATE TABLE IF NOT EXISTS slots_new (
                        id                INTEGER      PRIMARY KEY AUTOINCREMENT,
                        device_id         VARCHAR(64)  NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
                        slot_number       INTEGER      NOT NULL,
                        medicine_id       INTEGER REFERENCES medicines(id) ON DELETE SET NULL,
                        capacity          INTEGER      NOT NULL DEFAULT 1000,
                        current_quantity  INTEGER      NOT NULL DEFAULT 0,
                        default_dose      INTEGER      NOT NULL DEFAULT 1,
                        is_active         BOOLEAN      NOT NULL DEFAULT 1,
                        last_refilled_at  DATETIME,
                        updated_at        DATETIME     DEFAULT CURRENT_TIMESTAMP,
                        UNIQUE (device_id, slot_number),
                        CHECK (slot_number >= 0 AND slot_number <= 7),
                        CHECK (current_quantity >= 0),
                        CHECK (
                            (slot_number = 0 AND medicine_id IS NULL AND capacity = 0 AND current_quantity = 0)
                            OR
                            (slot_number > 0 AND capacity >= 0)
                        )
                    )
                """)
                await db.execute("""
                    INSERT INTO slots_new (id, device_id, slot_number, medicine_id, capacity, current_quantity, default_dose, is_active, last_refilled_at, updated_at)
                    SELECT id, device_id, slot_number, medicine_id, CASE WHEN slot_number = 0 THEN 0 ELSE MAX(capacity, 1000) END, current_quantity, default_dose, is_active, last_refilled_at, updated_at FROM slots
                """)
                await db.execute("DROP TABLE slots")
                await db.execute("ALTER TABLE slots_new RENAME TO slots")
                logger.info("Đã migrate thành công bảng slots: Gỡ bỏ giới hạn capacity")
        except Exception as e:
            logger.warning(f"Bỏ qua migration slots: {e}")

        await db.execute("PRAGMA foreign_keys = ON")
        await db.commit()

    logger.info("Migrations hoàn tất.")


async def seed_database() -> None:
    """
    Nạp dữ liệu mẫu vào database (chỉ khi database đang rỗng).

    Kiểm tra bảng devices: nếu đã có dữ liệu → bỏ qua.
    Đây là biện pháp an toàn chống nạp trùng lặp khi restart app nhiều lần.
    """
    if not SEED_FILE.exists():
        logger.warning(f"Seed file không tìm thấy: {SEED_FILE} — bỏ qua.")
        return

    async with aiosqlite.connect(DATABASE_PATH) as db:
        await db.execute("PRAGMA foreign_keys = ON")

        # Kiểm tra đã có dữ liệu chưa
        cursor = await db.execute("SELECT COUNT(*) FROM devices")
        row = await cursor.fetchone()
        device_count = row[0] if row else 0

        if device_count > 0:
            logger.info(f"Database đã có {device_count} thiết bị — bỏ qua seed.")
            return

        logger.info("Đang nạp dữ liệu mẫu (seed)...")
        seed_sql = SEED_FILE.read_text(encoding="utf-8")
        await db.executescript(seed_sql)
        await db.commit()
        logger.info("Dữ liệu mẫu đã được nạp thành công.")
