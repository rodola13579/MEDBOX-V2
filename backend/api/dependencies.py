"""
api/dependencies.py
────────────────────
FastAPI Dependency Injection — cung cấp DB connection và services cho các router.

Cách dùng trong router:
    from api.dependencies import get_db

    @router.get("/example")
    async def example(db: aiosqlite.Connection = Depends(get_db)):
        ...

Hoặc dùng Annotated (khuyến nghị với FastAPI >= 0.95):
    from typing import Annotated
    from fastapi import Depends

    DB = Annotated[aiosqlite.Connection, Depends(get_db)]

    @router.get("/example")
    async def example(db: DB):
        ...
"""

from typing import Annotated

import aiosqlite
from fastapi import Depends

from database.connection import get_db

# ── Type Alias tiện dụng cho các router ──────────────────────────────────────
# Dùng: async def endpoint(db: DB): ...
DB = Annotated[aiosqlite.Connection, Depends(get_db)]
