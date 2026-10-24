"""Test get_orphaned() and orphan recovery."""
import asyncio
import sys
sys.path.insert(0, ".")

from database.connection import get_db_context
from database.repositories import dose_log_repo


async def run():
    async with get_db_context() as db:
        # Tao 2 log orphan (PENDING va DISPENSED)
        log1 = await dose_log_repo.create_pending(
            db, "ESP32_PILLBOX_01", 4, "2026-09-26 15:00:00", "AUTO_SCHEDULE"
        )
        log2 = await dose_log_repo.create_pending(
            db, "ESP32_PILLBOX_01", 5, "2026-09-26 15:00:00", "AUTO_SCHEDULE"
        )
        await dose_log_repo.mark_dispensed(db, log2["id"])

        orphans = await dose_log_repo.get_orphaned(db, "ESP32_PILLBOX_01")
        print(f"Orphans found: {len(orphans)}")
        assert len(orphans) >= 2, f"Expected >= 2, got {len(orphans)}"

        for o in orphans:
            print(f"  orphan id={o['id']} status={o['status']} slot={o['slot_number']}")

        # Test orphan recovery
        for o in orphans:
            await dose_log_repo.mark_missed(db, o["id"], notes="Test recovery")

        orphans_after = await dose_log_repo.get_orphaned(db, "ESP32_PILLBOX_01")
        print(f"Orphans after recovery: {len(orphans_after)} (expected 0)")
        assert len(orphans_after) == 0, f"Expected 0 after recovery, got {len(orphans_after)}"

        print("PASS: get_orphaned + recovery logic OK")


if __name__ == "__main__":
    asyncio.run(run())
