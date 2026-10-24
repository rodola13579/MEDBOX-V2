-- ============================================================
-- SMART PILLBOX — Database Schema DDL
-- File: backend/database/schema.sql
-- Tham chiếu: SYSTEM_SPECIFICATION.md § 6.1
--
-- Tất cả bảng dùng CREATE TABLE IF NOT EXISTS
-- → An toàn khi chạy lại nhiều lần (idempotent)
-- ============================================================

-- Bật WAL và Foreign Keys cho session này
PRAGMA journal_mode = WAL;
PRAGMA foreign_keys = ON;


-- ============================================================
-- 1. BẢNG devices — Quản lý thiết bị phần cứng
-- ============================================================
CREATE TABLE IF NOT EXISTS devices (
    id              VARCHAR(64)  PRIMARY KEY,
    name            VARCHAR(128) NOT NULL,
    mac_address     VARCHAR(32)  UNIQUE,
    status          VARCHAR(20)  NOT NULL DEFAULT 'OFFLINE',
    total_slots     INTEGER      NOT NULL DEFAULT 8,
    current_slot    INTEGER      NOT NULL DEFAULT 0,
    last_heartbeat  DATETIME,
    created_at      DATETIME     DEFAULT CURRENT_TIMESTAMP,
    updated_at      DATETIME     DEFAULT CURRENT_TIMESTAMP,

    CHECK (total_slots = 8),
    CHECK (current_slot >= 0 AND current_slot <= 7),
    CHECK (status IN ('ONLINE', 'OFFLINE', 'ERROR', 'JAMMED'))
);


-- ============================================================
-- 2. BẢNG medicines — Danh mục thuốc
-- ============================================================
CREATE TABLE IF NOT EXISTS medicines (
    id            INTEGER      PRIMARY KEY AUTOINCREMENT,
    name          VARCHAR(150) NOT NULL,
    dosage_unit   VARCHAR(30)  NOT NULL DEFAULT 'viên',
    instructions  VARCHAR(50)  NOT NULL DEFAULT 'Không phụ thuộc',
    created_at    DATETIME     DEFAULT CURRENT_TIMESTAMP,
    updated_at    DATETIME     DEFAULT CURRENT_TIMESTAMP,

    CHECK (instructions IN ('Trước ăn', 'Sau ăn', 'Không phụ thuộc'))
);


-- ============================================================
-- 3. BẢNG slots — Cấu hình 8 ngăn vật lý trên khay tròn
--
-- QUY TẮC BẤT BIẾN:
--   Slot 0 = Safe Home: medicine_id IS NULL, capacity = 0, current_quantity = 0
--   Slot 1..7: ngăn chứa thuốc, capacity > 0
-- ============================================================
CREATE TABLE IF NOT EXISTS slots (
    id                INTEGER      PRIMARY KEY AUTOINCREMENT,
    device_id         VARCHAR(64)  NOT NULL
                          REFERENCES devices(id) ON DELETE CASCADE,
    slot_number       INTEGER      NOT NULL,
    medicine_id       INTEGER
                          REFERENCES medicines(id) ON DELETE SET NULL,
    capacity          INTEGER      NOT NULL DEFAULT 1000,
    current_quantity  INTEGER      NOT NULL DEFAULT 0,
    default_dose      INTEGER      NOT NULL DEFAULT 1,
    is_active         BOOLEAN      NOT NULL DEFAULT 1,
    last_refilled_at  DATETIME,
    updated_at        DATETIME     DEFAULT CURRENT_TIMESTAMP,

    -- Mỗi thiết bị chỉ có đúng 1 bản ghi cho mỗi slot_number
    UNIQUE (device_id, slot_number),

    CHECK (slot_number >= 0 AND slot_number <= 7),
    CHECK (current_quantity >= 0),

    -- RÀNG BUỘC TUYỆT ĐỐI: Slot 0 là nắp đóng an toàn, không chứa thuốc
    CHECK (
        (slot_number = 0 AND medicine_id IS NULL AND capacity = 0 AND current_quantity = 0)
        OR
        (slot_number > 0 AND capacity >= 0)
    )
);


-- ============================================================
-- 4. BẢNG schedules — Lịch uống thuốc hằng ngày (lặp lại)
--
-- QUY TẮC:
--   - Chỉ lập lịch cho Slot 1..7 (nghiêm cấm Slot 0)
--   - Hệ thống tự gán label (Buổi Sáng/Trưa/Chiều/Tối) theo time_of_day
--   - Chống trùng: 1 slot chỉ có 1 lịch tại cùng mốc giờ HH:MM
-- ============================================================
CREATE TABLE IF NOT EXISTS schedules (
    id            INTEGER     PRIMARY KEY AUTOINCREMENT,
    device_id     VARCHAR(64) NOT NULL
                      REFERENCES devices(id) ON DELETE CASCADE,
    slot_number   INTEGER     NOT NULL,
    time_of_day   VARCHAR(5)  NOT NULL,
    label         VARCHAR(50) NOT NULL,
    dose_quantity INTEGER     NOT NULL DEFAULT 1,
    is_active     BOOLEAN     NOT NULL DEFAULT 1,
    created_at    DATETIME    DEFAULT CURRENT_TIMESTAMP,
    updated_at    DATETIME    DEFAULT CURRENT_TIMESTAMP,

    -- Chặn trùng: cùng thiết bị + cùng giờ + cùng slot
    UNIQUE (device_id, time_of_day, slot_number),

    -- Chỉ được lập lịch cho Slot 1..7, nghiêm cấm Slot 0
    CHECK (slot_number >= 1 AND slot_number <= 7),
    CHECK (dose_quantity > 0),
    -- Định dạng giờ phút 24h: HH:MM
    CHECK (time_of_day GLOB '[0-2][0-9]:[0-5][0-9]'),
    CHECK (label IN ('Buổi Sáng', 'Buổi Trưa', 'Buổi Chiều', 'Buổi Tối'))
);


-- ============================================================
-- 5. BẢNG dose_logs — Nhật ký kiểm toán cữ uống thuốc
--
-- QUY TẮC BẤT BIẾN:
--   - KHÔNG BAO GIỜ XÓA bản ghi trong bảng này
--   - Chỉ INSERT và UPDATE trạng thái
--   - Tồn kho chỉ bị trừ khi status = 'TAKEN' hoặc 'EARLY_TAKEN'
--   - Khi MISSED: tồn kho KHÔNG thay đổi
-- ============================================================
CREATE TABLE IF NOT EXISTS dose_logs (
    id              INTEGER     PRIMARY KEY AUTOINCREMENT,
    schedule_id     INTEGER
                        REFERENCES schedules(id) ON DELETE SET NULL,
    device_id       VARCHAR(64) NOT NULL
                        REFERENCES devices(id) ON DELETE CASCADE,
    slot_number     INTEGER     NOT NULL,
    scheduled_time  DATETIME    NOT NULL,
    dispensed_time  DATETIME,
    confirmed_time  DATETIME,
    status          VARCHAR(25) NOT NULL DEFAULT 'PENDING',
    trigger_source  VARCHAR(30) NOT NULL DEFAULT 'AUTO_SCHEDULE',
    notes           TEXT,
    created_at      DATETIME    DEFAULT CURRENT_TIMESTAMP,

    CHECK (status IN ('PENDING', 'DISPENSED', 'TAKEN', 'EARLY_TAKEN', 'MISSED', 'JAMMED')),
    CHECK (trigger_source IN ('AUTO_SCHEDULE', 'EARLY_MANUAL', 'LOCAL_BUTTON', 'EMERGENCY_HOME'))
);


-- ============================================================
-- INDEXES — Tối ưu truy vấn quan trọng
-- ============================================================

-- Tìm cấu hình ngăn nhanh theo device + slot number
CREATE INDEX IF NOT EXISTS idx_slots_dev_slot
    ON slots(device_id, slot_number);

-- Scheduler quét lịch active theo giờ mỗi phút
CREATE INDEX IF NOT EXISTS idx_schedules_scan
    ON schedules(device_id, is_active, time_of_day);

-- Watchdog tìm log đang ở trạng thái DISPENSED để tính timeout
CREATE INDEX IF NOT EXISTS idx_dose_logs_watchdog
    ON dose_logs(device_id, status);

-- Timeline: tải lịch sử theo thời gian mới nhất trước
CREATE INDEX IF NOT EXISTS idx_dose_logs_history
    ON dose_logs(device_id, scheduled_time DESC);

-- Tìm cữ PENDING kế tiếp gần nhất (dùng cho tính năng Lấy sớm)
CREATE INDEX IF NOT EXISTS idx_dose_logs_next_pending
    ON dose_logs(device_id, status, scheduled_time ASC);
