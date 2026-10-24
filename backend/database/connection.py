"""
database/connection.py
─────────────────────
Quản lý kết nối SQLite bất đồng bộ với aiosqlite.

Cung cấp hai cách lấy connection:
  1. get_db()         — FastAPI Dependency (dùng trong API routers)
  2. get_db_context() — Async context manager (dùng trong services/background tasks)

Cả hai đều tự động bật:
  - WAL mode     : Cho phép đọc song song trong khi đang ghi
  - Foreign Keys : Đảm bảo ràng buộc khóa ngoại (ON DELETE CASCADE, SET NULL...)
  - NORMAL sync  : Cân bằng hiệu năng và an toàn dữ liệu
"""

import logging
from contextlib import asynccontextmanager
from pathlib import Path
from typing import AsyncGenerator

import aiosqlite

from config import settings

logger = logging.getLogger(__name__)

DATABASE_PATH = Path(settings.DB_PATH)


async def _configure(db: aiosqlite.Connection) -> None:
    """Áp dụng PRAGMAs cần thiết cho mọi connection."""
    await db.execute("PRAGMA journal_mode = WAL")
    await db.execute("PRAGMA foreign_keys = ON")
    await db.execute("PRAGMA synchronous = NORMAL")
    # Trả về dict-style row thay vì tuple
    db.row_factory = aiosqlite.Row


async def get_db() -> AsyncGenerator[aiosqlite.Connection, None]:
    """
    FastAPI Dependency — Tự động mở và đóng connection theo vòng đời của request.

    Cách dùng trong router:
        @router.get("/example")
        async def example(db: aiosqlite.Connection = Depends(get_db)):
            ...
    """
    async with aiosqlite.connect(DATABASE_PATH) as db:
        await _configure(db)
        yield db


@asynccontextmanager
async def get_db_context():
    """
    Async context manager — Dùng trong background services và tasks.

    Cách dùng:
        async with get_db_context() as db:
            await db.execute("SELECT ...")
    """
    async with aiosqlite.connect(DATABASE_PATH) as db:
        await _configure(db)
        yield db
