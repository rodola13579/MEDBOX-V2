-- ============================================================
-- SMART PILLBOX — Seed Data (Dữ liệu mẫu phát triển)
-- File: backend/database/seed.sql
--
-- Chỉ chạy khi bảng devices đang rỗng (kiểm tra trong migrations.py)
-- Dữ liệu này phục vụ mục đích dev/test — KHÔNG dùng production
-- ============================================================

BEGIN TRANSACTION;

-- ── 1. Thiết bị mẫu ──────────────────────────────────────────
INSERT OR IGNORE INTO devices (id, name, mac_address, status, total_slots, current_slot)
VALUES (
    'ESP32_PILLBOX_01',
    'Hộp thuốc phòng khách',
    'C8:F0:9E:A1:B2:C3',
    'OFFLINE',
    8,
    0
);

-- ── 2. Danh mục thuốc mẫu ────────────────────────────────────
INSERT OR IGNORE INTO medicines (id, name, dosage_unit, instructions) VALUES
(1, 'Panadol Extra 500mg',   'viên', 'Sau ăn'),
(2, 'Glucosamine 1500mg',    'viên', 'Sau ăn'),
(3, 'Vitamin C 1000mg',      'viên', 'Không phụ thuộc');

-- ── 3. Cấu hình 8 ngăn ───────────────────────────────────────
-- Slot 0: Safe Home — tuyệt đối KHÔNG chứa thuốc (capacity=0)
INSERT OR IGNORE INTO slots (device_id, slot_number, medicine_id, capacity, current_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 0, NULL, 0, 0, 1);

-- Slot 1: Panadol (8/10 viên còn lại)
INSERT OR IGNORE INTO slots (device_id, slot_number, medicine_id, capacity, current_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 1, 1, 10, 8, 1);

-- Slot 2: Glucosamine (10/10 viên còn lại)
INSERT OR IGNORE INTO slots (device_id, slot_number, medicine_id, capacity, current_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 2, 2, 10, 10, 1);

-- Slot 3: Vitamin C (5/10 viên còn lại)
INSERT OR IGNORE INTO slots (device_id, slot_number, medicine_id, capacity, current_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 3, 3, 10, 5, 1);

-- Slot 4–7: Trống (chưa gán thuốc)
INSERT OR IGNORE INTO slots (device_id, slot_number, medicine_id, capacity, current_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 4, NULL, 10, 0, 0);

INSERT OR IGNORE INTO slots (device_id, slot_number, medicine_id, capacity, current_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 5, NULL, 10, 0, 0);

INSERT OR IGNORE INTO slots (device_id, slot_number, medicine_id, capacity, current_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 6, NULL, 10, 0, 0);

INSERT OR IGNORE INTO slots (device_id, slot_number, medicine_id, capacity, current_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 7, NULL, 10, 0, 0);

-- ── 4. Lịch uống hằng ngày mẫu ───────────────────────────────
-- Buổi Sáng (08:00): Panadol ở Slot 1
INSERT OR IGNORE INTO schedules (device_id, slot_number, time_of_day, label, dose_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 1, '08:00', 'Buổi Sáng', 1, 1);

-- Buổi Sáng (08:30): Vitamin C ở Slot 3
INSERT OR IGNORE INTO schedules (device_id, slot_number, time_of_day, label, dose_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 3, '08:30', 'Buổi Sáng', 1, 1);

-- Buổi Trưa (12:30): Glucosamine ở Slot 2
INSERT OR IGNORE INTO schedules (device_id, slot_number, time_of_day, label, dose_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 2, '12:30', 'Buổi Trưa', 1, 1);

-- Buổi Tối (19:30): Panadol ở Slot 1
INSERT OR IGNORE INTO schedules (device_id, slot_number, time_of_day, label, dose_quantity, is_active)
VALUES ('ESP32_PILLBOX_01', 1, '19:30', 'Buổi Tối', 1, 1);

COMMIT;
