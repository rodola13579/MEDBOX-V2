# KẾ HOẠCH TRIỂN KHAI DỰ ÁN — SMART PILLBOX (PHASE 1)
**Dự án:** Hệ thống Hộp thuốc Thông minh  
**Ngày lập:** 2026-09-25  
**Trạng thái kiến trúc:** ĐÃ ĐÓNG BĂNG — Tham chiếu từ `SYSTEM_SPECIFICATION.md`

---

## TRẠNG THÁI TỔNG QUAN

| Task | Tên | Trạng Thái |
|:---:|---|:---:|
| 01 | Phân tích & Thiết kế Tổng thể | ✅ HOÀN THÀNH |
| 02 | Backend Skeleton + SQLite Database | ✅ HOÀN THÀNH |
| 03 | Medicine & Slot API | ✅ HOÀN THÀNH |
| 04 | Schedule API + Auto-Session Engine | ✅ HOÀN THÀNH |
| 05 | MQTT Integration & Device Protocol | ✅ HOÀN THÀNH |
| 06 | Dispensing Engine + Sequential Queue | ✅ HOÀN THÀNH |
| 07 | WebSocket Hub & Real-time Broadcasting | ✅ HOÀN THÀNH |
| 08 | Device Control & Diagnostics API | ✅ HOÀN THÀNH |
| 09 | Scheduler Engine + Watchdog 15 Phút | ✅ HOÀN THÀNH |
| 10 | Frontend — Kiến trúc SPA & Dashboard Y tế | ✅ HOÀN THÀNH |
| 11 | Frontend — Quản lý Thuốc theo Ngăn & Lịch uống 4 Buổi | ✅ HOÀN THÀNH |
| 12 | Frontend — Khay xoay 2D Hướng 6h & Console Tác vụ In-Place | ✅ HOÀN THÀNH |
| 13 | Frontend — Bảng Điều khiển & Chẩn đoán Phần cứng | ✅ HOÀN THÀNH |
| 14 | ESP32 Firmware — Ngoại vi & Hợp nhất FSM Offline | ✅ HOÀN THÀNH |
| 15 | ESP32 Firmware — Tích hợp Wi-Fi, MQTT & Đồng bộ Backend | 🔄 Giai đoạn tiếp theo |
| 16 | End-to-End Integration Testing — Vận hành liên thông Web/Backend/Mạch | ⏳ Kế hoạch sau Task 15 |

---

## CẤU TRÚC THƯ MỤC DỰ ÁN THỰC TẾ

```text
new/                               <- Thư mục gốc dự án
|
+-- SYSTEM_SPECIFICATION.md         <- Đặc tả hệ thống toàn diện (Single Source of Truth)
+-- PROJECT_PLAN.md                 <- Kế hoạch 16 Tasks & Nhật ký kiểm toán hệ thống
+-- firmware.md                     <- Cẩm nang chuyên sâu Firmware ESP32-S3 (FreeRTOS)
+-- start_app.bat                   <- File 1-click khởi chạy tự động Server & Web
+-- Chay_SmartMed.bat               <- File 1-click khởi chạy Mosquitto + Backend + Web
|
+-- backend/                        <- FastAPI Backend (Python 3.13)
|   +-- main.py                     <- Entrypoint FastAPI, phục vụ cả API và Frontend SPA
|   +-- config.py                   <- Biến môi trường và cấu hình hệ thống (DB_PATH anchored)
|   +-- ws_manager.py               <- Quản lý WebSocket hub thời gian thực
|   +-- pillbox.db                  <- SQLite Database (5 bảng quan hệ y tế)
|   +-- requirements.txt            <- Dependencies: fastapi, uvicorn, aiosqlite, aiomqtt...
|   +-- api/                        <- REST API Routers: slots, schedules, dispensing, device, ws...
|   +-- services/                   <- Engines: dispensing_engine, scheduler_engine, watchdog...
|   +-- device/                     <- Device drivers: mqtt_client, protocol, device_state...
|   +-- database/                   <- SQLite migrations, schema.sql, connection...
|   +-- schemas/                    <- Pydantic validation schemas
|   +-- tests/                      <- Bộ test tự động (audit_test.py: 17/17 PASS)
|
+-- frontend/                       <- Frontend SPA Siêu nhẹ (Vanilla HTML5 / CSS / JS)
|   +-- index.html                  <- Giao diện SPA chính (4 Tab phân hệ y tế)
|   +-- css/
|   |   +-- style.css               <- Design system y tế, Glassmorphism, animations
|   +-- js/
|       +-- api.js                  <- REST Client gọi API Backend
|       +-- ws.js                   <- WebSocket client thời gian thực (/ws)
|       +-- carousel2d.js           <- Mô hình Khay xoay 2D 8 ngăn phân màu, xoay chuẩn hướng 6h
|       +-- app.js                  <- Logic ứng dụng, Nạp thuốc In-Place, Lịch 4 buổi, Chẩn đoán
|
+-- firmware/                       <- ESP32 Firmware (C++ / Arduino / PlatformIO)
|   +-- esp32_pillbox/
|       +-- src/ & include/         <- Driver: motor, homing, st7789, ec11, i2s audio, dht11, fsm
|
+-- HUONG_DAN_CHI_TIET/             <- Bộ tài liệu thực hành nạp code & chạy hệ thống
```

---

## TASK 01 — PHÂN TÍCH & THIẾT KẾ TỔNG THỂ ✅

**Trạng thái:** HOÀN THÀNH

**Đầu ra:**
- `SYSTEM_SPECIFICATION.md` — Đặc tả kiến trúc toàn diện (Single Source of Truth)
- `PROJECT_PLAN.md` — File kế hoạch này

---

## TASK 02 — BACKEND SKELETON + SQLITE DATABASE

**Mục tiêu:** Tạo khung sườn FastAPI có thể chạy được, kết nối SQLite và khởi tạo đầy đủ schema database.

**Thứ tự tạo file:**

| # | File | Vị Trí | Ý Nghĩa |
|:---:|---|---|---|
| 1 | `requirements.txt` | `backend/` | Danh sách thư viện Python: `fastapi`, `uvicorn`, `aiosqlite`, `aiomqtt`, `apscheduler`, `pydantic-settings` |
| 2 | `config.py` | `backend/` | Đọc biến môi trường: HOST, PORT, DB_PATH, MQTT_BROKER, MQTT_PORT, DEVICE_ID |
| 3 | `connection.py` | `backend/database/` | Mở kết nối SQLite bất đồng bộ (`aiosqlite`), bật WAL mode và Foreign Keys |
| 4 | `schema.sql` | `backend/database/` | Toàn bộ DDL: 5 bảng, Constraints, Indexes — từ SYSTEM_SPECIFICATION.md |
| 5 | `migrations.py` | `backend/database/` | Tự động chạy `schema.sql` khi app khởi động lần đầu |
| 6 | `seed.sql` | `backend/database/` | Dữ liệu mẫu: 1 thiết bị, 8 slot (slot 0 = Safe Home), 3 loại thuốc mẫu |
| 7 | `main.py` | `backend/` | Khởi tạo FastAPI App, CORS, Lifespan Startup/Shutdown (chạy migration) |

**Kết quả kiểm thử — Task đã đạt:**
- [x] Chạy `uvicorn main:app` không có lỗi
- [x] `GET http://localhost:8000/health` trả về `200 OK`
- [x] Database `pillbox.db` được tạo tự động với đầy đủ 5 bảng
- [x] Ràng buộc Slot 0 Safe Home hoạt động đúng (thử INSERT vi phạm bị chặn 400 Bad Request)

---

## TASK 03 — MEDICINE & SLOT API

**Mục tiêu:** Xây dựng hoàn chỉnh API CRUD cho danh mục thuốc và cấu hình 8 ngăn khay.

**Thứ tự tạo file:**

| # | File | Vị Trí | Ý Nghĩa |
|:---:|---|---|---|
| 1 | `medicine.py` (schema) | `backend/schemas/` | Pydantic models: `MedicineCreate`, `MedicineUpdate`, `MedicineResponse` |
| 2 | `slot.py` (schema) | `backend/schemas/` | Pydantic models: `SlotUpdate`, `SlotResponse` kèm thông tin thuốc lồng nhau |
| 3 | `medicine_repo.py` | `backend/database/repositories/` | SQL thuần: `get_all()`, `get_by_id()`, `create()`, `update()`, `delete()` bảng medicines |
| 4 | `slot_repo.py` | `backend/database/repositories/` | SQL thuần: `get_all_slots()`, `get_slot()`, `update_slot()` — bảo vệ Slot 0 |
| 5 | `medicine_service.py` | `backend/services/` | Business logic: Kiểm tra thuốc đang dùng trước khi xóa (409 Conflict) |
| 6 | `slot_service.py` | `backend/services/` | Business logic: Từ chối sửa Slot 0, kiểm tra medicine_id tồn tại |
| 7 | `dependencies.py` | `backend/api/` | Dependency Injection: cung cấp DB session và Service instances cho các router |
| 8 | `medicines.py` (router) | `backend/api/` | 4 endpoints: GET, POST, PUT, DELETE /api/v1/medicines |
| 9 | `slots.py` (router) | `backend/api/` | Endpoints: GET /api/v1/slots, PUT /api/v1/slots/{slot_number} |

**Cập nhật file có sẵn:**
- `main.py` — Đăng ký 2 router mới vào app

**Kết quả kiểm thử — Task đạt khi:**
- [ ] `POST /api/v1/medicines` tạo thuốc mới thành công
- [ ] `DELETE /api/v1/medicines/1` trả về 409 nếu thuốc đang gán cho slot
- [ ] `PUT /api/v1/slots/0` bị chặn 400 Bad Request
- [ ] `GET /api/v1/slots` trả về 8 slot với Slot 0 là Safe Home

---

## TASK 04 — SCHEDULE API + AUTO-SESSION ENGINE & ESP32 SYNC ✅

**Mục tiêu:** Xây dựng hệ thống lập lịch uống thuốc hằng ngày với cơ chế tự động gán nhãn Buổi và đồng bộ cấu trúc bitmask xuống firmware ESP32.

**Thứ tự tạo & hoàn thiện file:**

| # | File | Vị Trí | Ý Nghĩa |
|:---:|---|---|---|
| 1 | `schedule.py` (schema) | `backend/schemas/` | Pydantic: `ScheduleCreate`, `ScheduleSlotItem`, `ScheduleBatchCreate`, `ScheduleBatchResponse`, `ScheduleResponse` |
| 2 | `schedule_repo.py` | `backend/database/repositories/` | SQL: `get_all()`, `create()`, `upsert()`, `toggle()`, `delete()`, `get_active_by_time()` |
| 3 | `schedule_service.py` | `backend/services/` | Logic cốt lõi: Tự động tính `label`, `create_batch_schedules()`, `sync_schedules_to_esp32()` qua MQTT |
| 4 | `schedules.py` (router) | `backend/api/` | Endpoints: GET, POST đơn lẻ, POST `/batch` (Set giờ -> chọn nhiều ngăn), PATCH `/toggle`, DELETE |

**Quy trình đồng bộ chuẩn phần cứng:**
```
Bước 1: Set giờ (HH:MM 24h) -> Tự động nhận diện Buổi (Sáng/Trưa/Chiều/Tối)
Bước 2: Tích chọn các ngăn có thuốc (Slot 1..7) & chỉnh liều dùng
Bước 3: Lưu cữ -> Backend gom nhóm bitmask Sched {h, m, mask} -> Phát MQTT topic cmd/sync_schedules -> Lưu Flash NVS ESP32
```

**Kết quả kiểm thử — Task đạt:**
- [x] `POST` với `time_of_day: "08:00"` tự động gán `label: "Buổi Sáng"`
- [x] `POST /api/v1/schedules/batch` tạo đồng thời nhiều ngăn cho 1 mốc giờ, tự động tính bitmask
- [x] Tự động phát MQTT `cmd/sync_schedules` xuống ESP32 khi thêm/sửa/xóa lịch
- [x] Không thể lập lịch với `slot_number: 0` (Slot 0 Safe Home được bảo vệ tuyệt đối)

---

## TASK 05 — MQTT INTEGRATION & DEVICE PROTOCOL

**Mục tiêu:** Kết nối FastAPI Backend với MQTT Broker, thiết lập toàn bộ kênh giao tiếp hai chiều với ESP32.

**Thứ tự tạo file:**

| # | File | Vị Trí | Ý Nghĩa |
|:---:|---|---|---|
| 1 | `protocol.py` | `backend/device/` | JSON Command builders và Event parsers. Hàm `build_dispense_cmd()`, `build_home_cmd()`, `parse_event()` |
| 2 | `mqtt_client.py` | `backend/device/` | Kết nối `aiomqtt` tới Broker, Subscribe `pillbox/+/events/#`, dispatch event tới handler |
| 3 | `device_state.py` | `backend/device/` | Cache trạng thái thiết bị trong RAM: `current_slot`, `fsm_state`, `is_online`, `last_seen` |
| 4 | `mock_esp32_mqtt.py` | `backend/tests/` | Script giả lập ESP32: kết nối Broker, nhận cmd, tự reply `SLOT_REACHED` và `HOME_REACHED` |

**Cập nhật file có sẵn:**
- `main.py` — Khởi động MQTT Client service trong Lifespan Startup

**Kết quả kiểm thử — Task đạt khi:**
- [ ] Backend kết nối Mosquitto thành công, log ghi nhận Subscribe các topics
- [ ] Chạy `mock_esp32_mqtt.py`, Backend nhận được LWT ONLINE
- [ ] Publish cmd/dispense thủ công → Mock ESP32 nhận và reply `SLOT_REACHED`
- [ ] Backend parse đúng payload event từ Mock

---

## TASK 06 — DISPENSING ENGINE + SEQUENTIAL QUEUE ✅

**Mục tiêu:** Xây dựng nghiệp vụ cốt lõi phức tạp nhất — Cấp thuốc theo hàng đợi tuần tự, xác nhận từng ngăn, hồi vị khẩn cấp.

**Thứ tự tạo file:**

| # | File | Vị Trí | Ý Nghĩa |
|:---:|---|---|---|
| 1 | `dispensing.py` (schema) | `backend/schemas/` | Pydantic: `EarlySessionRequest`, `ConfirmRequest`, `DispenseResponse` |
| 2 | `dose_log_repo.py` | `backend/database/repositories/` | SQL: `create_log()`, `update_status()`, `get_active_logs()` — KHÔNG có hàm DELETE |
| 3 | `dispensing_service.py` | `backend/services/` | **File nghiệp vụ trung tâm.** Queue `[slot_a, slot_b,...]`, kiểm tra tồn kho, trừ kho ACID Transaction, điều phối MQTT |
| 4 | `dispensing.py` (router) | `backend/api/` | Endpoints: POST /early-session, POST /confirm, POST /home |

**Logic Queue (trong `dispensing_service.py`):**

```
Khi nhận confirm slot hiện tại:
  1. BEGIN TRANSACTION
  2. Trừ 1 viên slots.current_quantity
  3. UPDATE dose_logs.status = 'TAKEN'
  4. COMMIT
  5. Nếu còn slot tiếp trong Queue -> publish DISPENSE_NOW(next_slot)
  6. Nếu Queue rỗng             -> publish RETURN_HOME_ZERO
```

**Kết quả kiểm thử — Task đã đạt:**
- [x] POST early-session `{slots: [1, 3]}` → Mock nhận lệnh slot 1 trước
- [x] POST confirm slot 1 → Tồn kho giảm 1, Mock nhận lệnh slot 3 tiếp theo
- [x] POST confirm slot 3 → Tồn kho giảm 1, Mock nhận lệnh HOME
- [x] POST home giữa chừng → Queue bị xóa, Mock nhận HOME ngay
- [x] Tồn kho = 0 → 400 Bad Request, không gửi lệnh nào xuống phần cứng

---

## TASK 07 — WEBSOCKET HUB & REAL-TIME BROADCASTING ✅

**Mục tiêu:** Xây dựng kênh truyền dữ liệu thời gian thực từ Backend đến tất cả Web Client đang kết nối.

**Thứ tự file đã triển khai:**

| # | File | Vị Trí | Ý Nghĩa |
|:---:|---|---|---|
| 1 | `ws_manager.py` | `backend/` | Singleton: Quản lý danh sách kết nối WebSocket. Hàm `broadcast()` gửi tới tất cả client |
| 2 | `ws.py` (router) | `backend/api/` | Endpoint `/ws`: Chấp nhận kết nối WS, gửi snapshot trạng thái, ping/pong keep-alive 30s |
| 3 | `ws.js` | `frontend/js/` | WebSocket Client phía Web: Tự động kết nối lại (Exponential backoff), bắt sự kiện FSM, SLOT_REACHED, TELEMETRY |

**Cập nhật file có sẵn:**
- `mqtt_client.py` — Khi nhận MQTT event từ ESP32 → gọi `ws_manager.broadcast()`
- `dispensing_engine.py` — Sau mỗi bước xoay/hoàn tất cữ → gọi `ws_manager.broadcast()`
- `main.py` — Đăng ký `ws_router` vào FastAPI

**Kết quả kiểm thử — Task đã đạt:**
- [x] Mở kết nối WebSocket tới `ws://localhost:8000/ws` thành công
- [x] Server gửi snapshot trạng thái đầy đủ ngay khi client vừa kết nối (`connected`)
- [x] Mock ESP32 publish `SLOT_REACHED` → Web nhận được event qua WS ngay lập tức
- [x] Nhiều tab web mở cùng lúc → tất cả đều nhận broadcast đồng bộ thời gian thực

---

## TASK 08 — DEVICE CONTROL & DIAGNOSTICS API ✅

**Mục tiêu:** Xây dựng bảng điều khiển kiểm thử phần cứng độc lập — không liên quan nghiệp vụ thuốc.

**Thứ tự tạo file:**

| # | File | Vị Trí | Ý Nghĩa |
|:---:|---|---|---|
| 1 | `device.py` (schema) | `backend/schemas/` | Pydantic: `MoveRequest`, `TestHardwareRequest`, `DeviceStatusResponse` |
| 2 | `device_repo.py` | `backend/database/repositories/` | SQL: `get_device()`, `update_status()`, `update_current_slot()`, `update_heartbeat()` |
| 3 | `device_service.py` | `backend/services/` | Logic: Kiểm tra device online trước khi gửi lệnh, validate target_slot (0..7) |
| 4 | `device.py` (router) | `backend/api/` | Endpoints: GET /status, POST /move, POST /calibrate, POST /test-hardware |

**Kết quả kiểm thử — Task đã đạt:**
- [x] `GET /api/v1/device/status` trả về đúng trạng thái từ `device_state.py`
- [x] `POST /api/v1/device/move {target_slot: 5}` → Mock nhận lệnh MOVE đến slot 5
- [x] `POST /api/v1/device/move {target_slot: 8}` → 422 Unprocessable Entity
- [x] `POST /api/v1/device/calibrate` → Mock nhận lệnh CALIBRATE

---

## TASK 09 — SCHEDULER ENGINE + WATCHDOG 15 PHÚT ✅

**Mục tiêu:** Tự động kích hoạt cữ thuốc đúng giờ và đảm bảo khay tự thu về Slot 0 nếu bị bỏ quên.

**Thứ tự tạo file:**

| # | File | Vị Trí | Ý Nghĩa |
|:---:|---|---|---|
| 1 | `scheduler_engine.py` | `backend/services/` | APScheduler AsyncIOScheduler: Mỗi phút quét `schedules` tìm cữ đến giờ → kích hoạt Queue tự động |
| 2 | `watchdog_service.py` | `backend/services/` | Đặt asyncio timer 900s sau `SLOT_REACHED`. Hết giờ → publish HOME, log MISSED, broadcast cảnh báo |

**Cập nhật file có sẵn:**
- `main.py` — Khởi động APScheduler trong Lifespan Startup
- `dispensing_service.py` — Sau khi gửi DISPENSE → kích hoạt watchdog

**Kết quả kiểm thử — Task đã đạt:**
- [x] Tạo lịch cách 2 phút → Đúng giờ, scheduler tự kích hoạt cấp thuốc
- [x] Khay đang mở, sau 900s → Mock nhận lệnh HOME tự động
- [x] `dose_logs` cập nhật `status = 'MISSED'`, tồn kho không bị trừ

---

## TASK 10 — FRONTEND SPA FOUNDATION & MEDICAL DASHBOARD ✅

**Mục tiêu:** Xây dựng ứng dụng Single Page Application (SPA) siêu nhẹ (Vanilla HTML5/CSS/JavaScript ES6+), thiết lập hệ thống Design System y tế, kết nối REST API và WebSocket real-time phục vụ trực tiếp từ FastAPI.

**Cấu trúc tệp tin đã triển khai:**

| # | File | Vị Trí | Ý Nghĩa Thực Tế |
|:---:|---|---|---|
| 1 | `index.html` | `frontend/` | HTML5 SPA chuẩn y tế: Header trạng thái, 5 Tab phân hệ (Tổng quan, Lịch uống, Lịch sử, Cấu hình, Chẩn đoán phần cứng) |
| 2 | `style.css` | `frontend/css/` | Toàn bộ Design System y tế: Glassmorphism, Dark/Light mode, Medical Cyan/Green/Red tokens, hiệu ứng mượt mà |
| 3 | `api.js` | `frontend/js/` | REST Client kết nối FastAPI: CRUD slots, schedules, dispensing, device diagnostics |
| 4 | `ws.js` | `frontend/js/` | WebSocket Manager thời gian thực: Tự động kết nối lại (`/ws/web`), dispatch sự kiện FSM, SLOT_REACHED, TELEMETRY |
| 5 | `app.js` | `frontend/js/` | Logic điều khiển trung tâm: Quản lý Tab, Header sống (ONLINE/OFFLINE, RSSI, Giờ hệ thống), Activity Feed, Thống kê |

**Kết quả kiểm thử — Task đạt khi:**
- [x] Truy cập `http://localhost:8000/` tải toàn bộ giao diện SPA mượt mà không cần Node.js hay bundle build.
- [x] Header hiển thị chính xác trạng thái kết nối WebSocket và tình trạng thiết bị (ONLINE/OFFLINE).
- [x] Widget "Lộ trình uống thuốc tiếp theo" tự động tính toán và hiển thị cữ thuốc kế tiếp từ cơ sở dữ liệu.

---

## TASK 11 — QUẢN LÝ THUỐC THEO NGĂN & LỊCH UỐNG 4 BUỔI ✅

**Mục tiêu:** Quản lý thông tin thuốc gắn chặt với từng ngăn khay (bỏ danh mục thuốc độc lập), thiết lập ràng buộc chỉ cài lịch cho ngăn đã có thuốc, và tự động phân nhóm lịch theo 4 Buổi chuẩn 24h.

**Đặc tả nghiệp vụ đã hoàn thành:**

| # | Chức Năng | Vị Trí | Chi Tiết Nghiệp Vụ |
|:---:|---|---|---|
| 1 | **Quản lý Thuốc theo Ngăn (Slots Inventory)** | `frontend/` (Tab 1 & 4) | Thông tin thuốc (tên, số viên tồn kho, chỉ định Trước/Sau ăn) được quản lý gắn liền trực tiếp với từng Ngăn 1..7 khi nạp thuốc |
| 2 | **Cài đặt Lịch uống 4 Buổi (Schedule Timeline)** | `frontend/js/app.js` (Tab 2) | Tự động gom nhóm cữ uống trong ngày thành 4 Buổi: Sáng (05:00–10:59), Trưa (11:00–13:59), Chiều (14:00–17:59), Tối (18:00–04:59) |
| 3 | **Ràng buộc ngặt nghèo (Fail-Safe)** | `frontend/js/app.js` | **Chỉ cho phép tạo lịch uống cho các ngăn đã có thuốc (`quantity > 0`).** Ngăn trống bị vô hiệu hóa trong form chọn ngăn |
| 4 | **Chuẩn thời gian 24 Giờ (24-Hour TimePicker)** | `frontend/index.html` | Sử dụng khung giờ 24h (`00:00` đến `23:59`) tránh nhầm lẫn y tế. Nhập giờ tự động phân loại đúng nhãn Buổi tương ứng |
| 5 | **Công tắc bật/tắt cữ tức thì (Toggle On/Off)** | `frontend/js/app.js` | Bật/tắt kích hoạt cữ uống cập nhật ngay lập tức xuống backend qua API |

**Kết quả kiểm thử — Task đạt khi:**
- [x] Nạp thuốc vào ngăn → Số lượng và tên thuốc hiển thị ngay trên mô hình khay và danh sách.
- [x] Form thêm cữ chỉ hiển thị các ngăn đang có thuốc.
- [x] Nhập `08:00` tự gắn nhãn 🌅 Buổi Sáng, nhập `12:30` tự gắn nhãn ☀️ Buổi Trưa, `18:30` gắn 🌙 Buổi Tối.
- [x] Toggle bật/tắt lịch cữ phản hồi tức thì mà không cần tải lại trang.

---

## TASK 12 — KHAY XOAY 2D HƯỚNG 6 GIỜ & TASK CONSOLE IN-PLACE ✅

**Mục tiêu:** Mô phỏng trực quan mô hình khay thuốc 2D xoay hướng 6 giờ, phân 8 màu sắc riêng biệt, và cơ chế Bảng điều khiển tác vụ In-Place kèm xác nhận hồi vị Safe Home.

**Cấu trúc tệp tin & Cơ chế nghiệp vụ:**

| # | Thành Phần | Vị Trí | Chi Tiết Kỹ Thuật |
|:---:|---|---|---|
| 1 | `carousel2d.js` | `frontend/js/` | Vẽ mô hình khay 2D với 8 cung phân đoạn (45°/ngăn), phân 8 màu sắc rực rỡ riêng biệt (Slot 0 màu xám Safe Home có biểu tượng khóa 🔒) |
| 2 | **Nguyên lý xoay hướng 6 giờ** | `frontend/js/carousel2d.js` | Nắp hộp cố định có cửa mở ở **vị trí 6 giờ** (hướng nhìn của người dùng). Khi yêu cầu ngăn nào, **toàn bộ khay thuốc tự xoay để ngăn đó dịch chuyển đến đúng vị trí 6 giờ** |
| 3 | **Bảng điều khiển Tác vụ In-Place** | `frontend/js/app.js` | Thay vì làm mờ màn hình bằng modal popup, khu vực "Lộ trình xem nhanh đơn thuốc" tự động chuyển đổi thành Console tương tác trực tiếp khi Nạp thuốc hoặc Lấy thuốc sớm |
| 4 | **Cơ chế Xoay Nạp Thuốc** | `frontend/js/app.js` | Khi chọn nạp ngăn nào (ví dụ Ngăn 3), mô hình khay lập tức xoay Ngăn 3 ra cửa 6 giờ để người dùng đổ thuốc vào |
| 5 | **Nút Xác nhận & Hồi vị Safe Home** | `frontend/js/app.js` | Sau khi đổ thuốc xong (hoặc lấy thuốc xong), xuất hiện nút: `✅ Xác nhận đã bỏ thuốc vào & Về vị trí Safe 🔒`. Bấm nút này khay sẽ tự động xoay về Ngăn 0 Safe Home che kín nắp |

**Logic xoay khay 2D:**
```text
Cửa lấy thuốc cố định tại hướng 6 giờ (góc 90 độ phía dưới)
Góc xoay khay: rotationAngle = 90 - (slotIndex * 45)
Hiệu ứng chuyển động mượt mà: transition: transform 1.2s cubic-bezier(0.4, 0, 0.2, 1)
```

**Kết quả kiểm thử — Task đạt khi:**
- [x] Chọn nạp Ngăn 1..7 → Khay xoay mượt mà đưa ngăn đó về đúng hướng 6 giờ.
- [x] Bấm nút xác nhận nạp/lấy thuốc → Khay tự động xoay về Ngăn 0 Safe Home.
- [x] Bảng tác vụ in-place hoạt động trơn tru, không gây che khuất hay làm mờ mô hình khay xoay.

---

## TASK 13 — BẢNG ĐIỀU KHIỂN & CHẨN ĐOÁN PHẦN CỨNG ✅

**Mục tiêu:** Tích hợp bảng điều khiển chẩn đoán phần cứng dành cho kỹ sư vào Tab "Chẩn đoán phần cứng" trên Web Dashboard.

**Chức năng đã tích hợp:**

| # | Chức Năng Chẩn Đoán | Vị Trí | Chi Tiết |
|:---:|---|---|---|
| 1 | **Motor Manual Dock** | Tab Chẩn đoán | 8 nút xoay trực tiếp đến Slot 0..7, nút xoay bước CW/CCW 45° |
| 2 | **Auto-Homing TCRT5000 Button** | Tab Chẩn đoán | Nút kích hoạt chu trình rà mốc gốc cảm biến TCRT5000 |
| 3 | **Simulation & Test Hardware** | Tab Chẩn đoán | Nút mô phỏng kẹt cơ (Jammed), nút test chuông/còi báo động |
| 4 | **Telemetry Specs Card** | Tab Chẩn đoán | Cập nhật thời gian thực qua WebSocket: Uptime, Free Heap, Wi-Fi RSSI, Firmware Version, IP LAN ESP32 |
| 5 | **Audit Logs Console** | Tab Chẩn đoán | Bảng nhật ký kiểm toán ghi nhận mọi lệnh hạ xuống phần cứng kèm timestamp |

**Kết quả kiểm thử — Task đạt khi:**
- [x] Bấm nút xoay thủ công đến Ngăn 5 → Mô hình khay và backend nhận lệnh MOVE tương ứng.
- [x] Bấm nút Calibrate → Backend gửi lệnh CALIBRATE qua MQTT.
- [x] Thẻ thông số Telemetry cập nhật liên tục các chỉ số từ ESP32/Mock qua WebSocket.

---

## TASK 14 — ESP32 FIRMWARE — NGOẠI VI & HỢP NHẤT FSM OFFLINE ✅

**Mục tiêu:** Xây dựng toàn bộ driver ngoại vi phần cứng, tối ưu hóa hiển thị/âm thanh, và hợp nhất máy trạng thái FSM vận hành độc lập (Offline Mode) an toàn trên bo mạch ESP32-S3 N16R8.

**Các thành phần mã nguồn đã hoàn thành:**

| # | Phân Hệ Ngoại Vi | File Nguồn | Trạng Thái & Đặc Tính Kỹ Thuật |
|:---:|---|---|---|
| 1 | **Cấu hình Chân an toàn** | `firmware/esp32_pillbox/config.h` | Bản đồ chân chuẩn: Motor (4,5,6,7), TCRT5000 (1), Loa I2S (15,16,17), TFT ST7789 (8,9,10,11,12,3), EC11 (38,39,40), Nút K0 (2), DHT11 (41). Tránh tuyệt đối bus OPI 26..37. |
| 2 | **Động cơ bước 28BYJ-48** | `motor_stepper.cpp/.h` | Điều khiển half-step 8 bước, xoay 512 bước/ngăn, **bắt buộc ngắt điện cuộn dây (de-energize)** ngay sau khi dừng giúp động cơ hoàn toàn nguội mát. |
| 3 | **Cảm biến mốc TCRT5000** | `tcrt5000_homing.cpp/.h` | Tự động rà tìm vạch phản xạ tại Slot 0 khi khởi động, cơ chế bảo vệ kẹt cơ ngắt sau 4.500 bước. |
| 4 | **Màn hình ST7789 2.4"** | `display_st7789.cpp/.h` | Độ phân giải 240x320 IPS, sử dụng phông chữ phần cứng TFT_eSPI (Font 2, 4, 6, 7) và **khóa cố định `setTextSize(1)`** triệt tiêu 100% hiện tượng lỗi vỡ font / nhòe chữ khi chuyển menu. |
| 5 | **Núm xoay EC11 & Nút K0** | `encoder_ec11.cpp/.h` | Bắt ngắt xung xoay mượt mà, chống rung phím (debounce 50ms) cho cả nút SW (uống thuốc/xác nhận) và nút vật lý K0 (quay lại menu). |
| 6 | **Loa I2S MAX98357A** | `audio_i2s.cpp/.h` | Giải mã phát file MP3 giọng nói tiếng Việt từ phân vùng Flash LittleFS, duy trì xung I2S liên tục chống nổ loa khi khởi động lại. |
| 7 | **Cảm biến DHT11** | `dht11_sensor.cpp/.h` | Đo nhiệt độ / độ ẩm phòng, cập nhật chu kỳ 3s không block hệ thống. |
| 8 | **Hợp nhất FSM Offline** | `fsm_controller.cpp/.h` | Quản lý 7 trạng thái: `HOMING_SEARCH` $\to$ `SAFE_HOME_IDLE` $\to$ `ALERTING_DUE` $\to$ `ROTATING_TO_DOSE` $\to$ `WAITING_TAKEN` $\to$ `ROTATING_TO_HOME`. |

**Kết quả kiểm thử thực tế (Hardware-in-the-Loop trên COM14):**
- [x] Nạp firmware trực tiếp qua cổng COM14 thành công 100%.
- [x] Khởi động: Motor tự động rà mốc TCRT5000 và dừng chuẩn xác tại Slot 0 Safe Home.
- [x] Cuộn dây động cơ ngắt điện hoàn toàn khi dừng (motor mát, không nóng sau nhiều giờ).
- [x] Màn hình ST7789 hiển thị sắc nét đồng hồ, nhiệt độ/độ ẩm DHT11, card cữ thuốc, menu chuyển đổi mượt mà không vỡ font.
- [x] Núm xoay EC11 và nút bấm SW, K0 phản hồi nhạy, âm thanh giọng nói MP3 LittleFS phát rõ ràng qua loa MAX98357A.
- [x] Quy trình uống thuốc 2 giai đoạn tại chỗ (Offline) chạy trơn tru: Bấm SW $\to$ Xoay ra ngăn thuốc $\to$ Nhắc uống $\to$ Bấm SW lần 2 $\to$ Tự động hồi vị Ngăn 0 Safe Home.

---

## TASK 15 — ESP32 FIRMWARE — TÍCH HỢP WI-FI, MQTT & ĐỒNG BỘ BACKEND 🔄

**Mục tiêu:** Kết nối Firmware FSM đã hoạt động ổn định với mạng Wi-Fi và MQTT Broker, đồng bộ lịch uống giữa Backend FastAPI và Flash NVS, sẵn sàng vận hành liên thông hai chiều.

**Thứ tự triển khai:**

| # | Module Tích Hợp | File Triển Khai | Ý Nghĩa Kỹ Thuật |
|:---:|---|---|---|
| 1 | **Wi-Fi Connection Manager** | `firmware/esp32_pillbox/wifi_manager.cpp/.h` | Kết nối Wi-Fi tự động, cơ chế Auto-Reconnect ngầm không làm gián đoạn FSM offline khi mất sóng |
| 2 | **MQTT Client Driver** | `firmware/esp32_pillbox/mqtt_handler.cpp/.h` | Kết nối Mosquitto Broker qua TCP 1883, đăng ký Di chúc mạng LWT (`pillbox/{id}/state/status`) |
| 3 | **Command Dispatcher** | `firmware/esp32_pillbox/cmd_dispatcher.cpp/.h` | Subscribe `pillbox/{id}/cmd/#`, giải mã JSON lệnh: `dispense`, `confirm`, `home`, `calibrate`, `sync_schedules` |
| 4 | **Event & Telemetry Reporter**| `firmware/esp32_pillbox/telemetry_reporter.cpp/.h`| Publish định kỳ `telemetry` (RSSI, Heap, Vị trí slot) và phát sự kiện `SLOT_REACHED`, `HOME_REACHED`, `TAKEN` |
| 5 | **NVS Schedule Synchronizer** | `firmware/esp32_pillbox/schedule_sync.cpp/.h` | Lưu bảng lịch từ Backend xuống bộ nhớ Flash NVS để duy trì cữ uống khi mất mạng |

**Kết quả kiểm thử — Task đạt khi:**
- [ ] ESP32 kết nối Wi-Fi và MQTT Broker thành công, gửi bản tin `{"online": true}` lên Broker.
- [ ] Backend gửi lệnh `POST /api/v1/device/move {target_slot: 3}` $\to$ ESP32 nhận lệnh qua MQTT, xoay khay ra Ngăn 3 và báo `SLOT_REACHED`.
- [ ] Người bệnh nhấn nút SW trên hộp $\to$ ESP32 publish `TAKEN_BUTTON_PRESSED` $\to$ Backend trừ kho và Web hiển thị hoàn thành.
- [ ] Rút dây mạng Wi-Fi: ESP32 vẫn tiếp tục chạy FSM cảnh báo theo lịch NVS, cắm mạng lại tự động kết nối và đồng bộ.

---

## TASK 16 — END-TO-END INTEGRATION TESTING

**Mục tiêu:** Kiểm thử toàn diện khi tất cả thành phần hoạt động đồng thời.

**Các kịch bản phải vượt qua:**

| # | Kịch bản | Kết quả kỳ vọng |
|:---:|---|---|
| 1 | Cấp thuốc bình thường (1 ngăn) | Web → Backend → MQTT → ESP32 → Motor → SLOT_REACHED → Web xác nhận |
| 2 | Cấp thuốc tuần tự (2 ngăn) | Hàng đợi [1, 3] đúng thứ tự, khay về 0 sau ngăn cuối |
| 3 | Timeout 15 phút không lấy | Khay tự về 0, log MISSED, tồn kho không đổi |
| 4 | Rút nguồn ESP32 giữa chừng | LWT MQTT → Backend OFFLINE → Web banner đỏ |
| 5 | Cắm nguồn lại ESP32 | Homing → ONLINE → Web cập nhật xanh |
| 6 | Kẹt cơ (chặn khay bằng tay) | Vượt 4500 bước → JAMMED → Web cảnh báo đỏ |
| 7 | Slot hết thuốc | Backend chặn → Web thông báo, không gửi lệnh phần cứng |
| 8 | Bấm đúp nút Lấy thuốc | Chỉ 1 lệnh xử lý, 409 cho request thứ hai |

---

## GHI CHÚ QUAN TRỌNG

> [!NOTE]
> **Thứ tự phụ thuộc (Dependencies):**
> - Task 03, 04 phụ thuộc Task 02 (cần Database)
> - Task 06 phụ thuộc Task 04 + Task 05 (cần Schedule + MQTT)
> - Task 07 phụ thuộc Task 05 (cần MQTT Client)
> - Task 10–13 phụ thuộc Task 02–09 (cần Backend hoàn chỉnh)
> - Task 15 phụ thuộc Task 14 (cần module Homing)
> - Task 16 chỉ bắt đầu khi Task 01–15 hoàn tất

> [!IMPORTANT]
> **Nguồn tham chiếu kiến trúc:**
> Mọi quyết định thiết kế khi code phải đối chiếu với `SYSTEM_SPECIFICATION.md`.
> Không tự ý thay đổi kiến trúc trong quá trình code mà không cập nhật tài liệu.

> [!CAUTION]
> **Các quy tắc BẤT BIẾN không được vi phạm:**
> 1. Slot 0 tuyệt đối không chứa thuốc (capacity = 0)
> 2. Chỉ trừ kho khi trạng thái chuyển sang TAKEN — không trừ khi MISSED
> 3. Không xóa bảng dose_logs
> 4. Ngắt điện cuộn dây động cơ ngay sau khi dừng bước
> 5. Không coi command_sent = operation_succeeded (phải chờ ACK từ ESP32)

---

## 🏆 NHẬT KÝ KIỂM THỬ TOÀN DIỆN & TIẾN ĐỘ HỆ THỐNG

### 1. Kết Quả Kiểm Thử Hệ Thống (Audit Test Suite — 17/17 Tiêu Chí Đạt 100%)
- **Test script:** `backend/tests/audit_test.py`
- [x] `test_01_backend_health` — 200 OK, database & scheduler active
- [x] `test_02_api_info` — Phiên bản 1.0.0
- [x] `test_03_frontend_static_spa_served` — Phục vụ SPA index.html từ thư mục gốc
- [x] `test_04_frontend_css_design_system` — Design system CSS y tế đầy đủ token
- [x] `test_05_frontend_api_client_exists` — Module REST Client hoàn chỉnh
- [x] `test_06_frontend_ws_client_exists` — WebSocket client có auto-reconnect
- [x] `test_07_frontend_carousel_2d_engine` — Động cơ Khay xoay 2D 8 ngăn phân màu, hướng 6h
- [x] `test_08_frontend_app_orchestrator` — Logic điều khiển trung tâm 4 tab
- [x] `test_09_database_has_8_slots` — Đúng 8 slot y tế (0..7)
- [x] `test_10_slot_0_safe_home_enforcement` — Chặn cập nhật thuốc vào Slot 0 (400 Bad Request)
- [x] `test_11_medicines_catalog_functional` — CRUD thuốc hoạt động ổn định
- [x] `test_12_device_state_in_memory` — RAM state cache phản hồi chuẩn
- [x] `test_13_device_dashboard_aggregate` — Endpoint tổng hợp trả lời đầy đủ
- [x] `test_14_schedule_slot_0_protection` — Chặn đặt lịch cho Slot 0 (422 Unprocessable)
- [x] `test_15_schedules_listing` — Danh sách lịch cữ hoạt động chuẩn xác
- [x] `test_16_dispense_history_accessible` — Nhật ký kiểm toán dose_logs truy xuất nhanh
- [x] `test_17_active_session_state` — Quản lý phiên cấp thuốc tuần tự chuẩn FSM

### 2. Kết Quả Kiểm Thử Phần Cứng ESP32-S3 (COM14 Live Testing)
- [x] Nạp firmware qua cổng COM14 thành công 100%.
- [x] Auto-Homing TCRT5000: Dừng chính xác tại Slot 0 Safe Home.
- [x] Động cơ bước 28BYJ-48: De-energize cuộn dây ngay sau khi dừng bước, không nóng sau nhiều giờ.
- [x] Màn hình ST7789 2.4" IPS 320x240: Khóa `setTextSize(1)` với font phần cứng, triệt tiêu lỗi nhòe/vỡ font.
- [x] Núm xoay EC11 + Nút K0: Phản hồi nhạy, chống rung phím 50ms.
- [x] Loa I2S MAX98357A: Phát giọng nói tiếng Việt MP3 từ phân vùng LittleFS Flash.
- [x] FSM Offline 7 trạng thái: Vận hành chu trình cấp thuốc 2 giai đoạn trơn tru tại chỗ.
