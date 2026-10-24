# SMARTMED PILLBOX — TÀI LIỆU ĐẶC TẢ GIAO THỨC MQTT & HƯỚNG DẪN KẾT NỐI

> **Phiên bản:** `3.4.0` | **Hệ thống:** ESP32-S3 Firmware Modular (`firmware/`) & FastAPI Backend (`backend/`)  
> **Định danh thiết bị mặc định:** `ESP32_PILLBOX_01`  
> **Cổng dịch vụ Broker:** `1883` (TCP chuẩn) & `9001` (WebSocket)  
> **IP máy chủ Broker (LAN):** `192.168.1.171`

---

## 📑 MỤC LỤC
1. [Kiến trúc giao tiếp MQTT tối ưu cho Firmware ESP32-S3](#1-kiến-trúc-giao-tiếp-mqtt-tối-ưu-cho-firmware-esp32-s3)
2. [Tối ưu hóa Payload JSON theo từng chức năng (Compact Payloads)](#2-tối-ưu-hóa-payload-json-theo-từng-chức-năng-compact-payloads)
   - 2.1 [Nhóm 1: Lệnh từ Web / Backend ➜ ESP32 (`cmd/*`)](#21-nhóm-1-lệnh-từ-web--backend--esp32-cmd)
   - 2.2 [Nhóm 2: Sự kiện từ ESP32 ➜ Web / Backend (`events/*`)](#22-nhóm-2-sự-kiện-từ-esp32--web--backend-events)
   - 2.3 [Nhóm 3: Giám sát trạng thái & Đo lường định kỳ (`state/*` & `lwt`)](#23-nhóm-3-giám-sát-trạng-thái--đo-lường-định-kỳ-state--lwt)
3. [Bảo toàn tính toàn vẹn và đồng bộ dữ liệu giữa Flash NVS và SQLite Database](#3-bảo-toàn-tính-toàn-vẹn-và-đồng-bộ-dữ-liệu-giữa-flash-nvs-và-sqlite-database)
   - 3.1 [Cấu trúc lưu trữ 2 bên (Flash NVS vs SQLite Database)](#31-cấu-trúc-lưu-trữ-2-bên-flash-nvs-vs-sqlite-database)
   - 3.2 [Bảng ánh xạ trường dữ liệu giữa Payload, NVS và Database](#32-bảng-ánh-xạ-trường-dữ-liệu-giữa-payload-nvs-và-database)
   - 3.3 [Cơ chế đồng bộ 2 chiều không xung đột (Conflict-Free Dual Sync)](#33-cơ-chế-đồng-bộ-2-chiều-không-xung-đột-conflict-free-dual-sync)
4. [Hướng dẫn khởi động Mosquitto Broker và theo dõi giao tiếp 2 bên](#4-hướng-dẫn-khởi-động-mosquitto-broker-và-theo-dõi-giao-tiếp-2-bên)
   - 4.1 [Khởi chạy Broker nhanh bằng file batch](#41-khởi-chạy-broker-nhanh-bằng-file-batch)
   - 4.2 [Lắng nghe 2 bên nói chuyện bằng công cụ `listen_mqtt.py`](#42-lắng-nghe-2-bên-nói-chuyện-bằng-công-cụ-listen_mqttpy)
   - 4.3 [Lắng nghe bằng lệnh `mosquitto_sub` (CLI)](#43-lắng-nghe-bằng-lệnh-mosquitto_sub-cli)

---

## 1. KIẾN TRÚC GIAO TIẾP MQTT TỐI ƯU CHO FIRMWARE ESP32-S3

Hộp thuốc thông minh SmartMed sử dụng kiến trúc giao tiếp phân tầng Publish/Subscribe với các tiêu chí tối ưu hóa chuyên sâu cho phần cứng ESP32-S3:

```
       ┌───────────────────────────────┐
       │   Web Client / React UI / SPA │
       └──────────────┬────────────────┘
                      │ (HTTP REST / WebSocket)
                      ▼
       ┌───────────────────────────────┐
       │        FastAPI Backend        │
       │   (SQLite: pillbox.db)        │
       └──────────────┬────────────────┘
                      │ (Publish cmd / Subscribe events)
                      ▼
 ═══════════════════════════════════════════════════════
        ECLIPSE MOSQUITTO BROKER (Port 1883)
 ═══════════════════════════════════════════════════════
                      ▲
                      │ (Subscribe cmd / Publish events)
       ┌──────────────┴────────────────┐
       │    ESP32-S3 Smart Pillbox     │
       │   (Flash NVS: medbox)         │
       └───────────────────────────────┘
```

### Các nguyên tắc tối ưu hóa trong Firmware (`firmware/src/mqtt_manager.cpp`):
1. **Hoàn toàn Non-Blocking (Không gây trễ vòng lặp):**
   - Vòng lặp `mqttManagerUpdate()` chỉ kiểm tra timer không chặn (`millis()`). Tuyệt đối không dùng `delay()` trong luồng mạng, đảm bảo ngắt núm xoay vô cực EC11 và xung điều khiển động cơ bước ULN2003 luôn mượt mà.
2. **Khả năng tự hồi phục (Auto-reconnect Backoff):**
   - Kích hoạt `WiFi.setAutoReconnect(true)` cấp phần cứng.
   - Bộ quản lý kết nối MQTT áp dụng chu kỳ thăm dò 5.000ms khi mất sóng, không gây nghẽn socket hoặc tràn ngăn xếp Task.
3. **Bảo vệ bộ nhớ tĩnh (Zero Dynamic Allocation Fragmentation):**
   - Sử dụng `StaticJsonDocument<N>` với dung lượng xác định trước (128B đến 1024B). Tránh việc phân mảnh bộ nhớ RAM (`heap fragmentation`) sau nhiều ngày hoạt động liên tục.
4. **Cơ chế LWT (Last Will & Testament):**
   - Đăng ký di chúc tự động `pillbox/ESP32_PILLBOX_01/lwt` với payload `{"status":"OFFLINE"}` và cờ `retain: true`. Khi thiết bị mất nguồn hoặc đứt Wi-Fi đột ngột, Broker sẽ lập tức thông báo để Backend cập nhật trạng thái mất kết nối lên Web UI.
5. **Tiết kiệm băng thông (Low-Bandwidth Telemetry):**
   - Tin nhắn Telemetry được phát với `QoS = 0` chu kỳ 3.000ms giúp giảm áp lực hàng đợi gói tin trên mạng LAN. Các lệnh điều khiển và sự kiện y tế luôn giữ `QoS = 1` để đảm bảo không bị thất thoát.

---

## 2. TỐI ƯU HÓA PAYLOAD JSON THEO TỪNG CHỨC NĂNG (COMPACT PAYLOADS)

Toàn bộ payload đã được chuẩn hóa rút gọn các khóa (keys), giảm tới **55% dung lượng byte truyền tải**, nhưng vẫn giữ tính tương thích 2 chiều: Backend chấp nhận cả dạng rút gọn lẫn dạng đầy đủ.

### 2.1. Nhóm 1: Lệnh từ Web / Backend ➜ ESP32 (`cmd/*`)
> **ESP32 đăng ký lắng nghe:** `pillbox/{DEVICE_ID}/cmd/#`

| Chức năng | Topic | Payload Tối ưu | Giải thích các trường |
|---|---|---|---|
| **Cấp thuốc** | `.../cmd/dispense` | `{"slot": 2}` *(hoặc `{"target_slot": 2}`)* | `slot`: Ngăn đích cần mở (1..7). ESP32 xoay khay ra ngăn này và phát lại sự kiện `SLOT_REACHED`. |
| **Về ngăn an toàn** | `.../cmd/home` | `{"cmd": "HOME"}` | Yêu cầu khay xoay về Ngăn 0 (Safe Home) đóng kín nắp bảo vệ. |
| **Dò mốc quang học** | `.../cmd/calibrate` | `{"cmd": "CALIB"}` | Động cơ quay chậm tìm vạch đen TCRT5000 để reset tọa độ Ngăn 0. |
| **Xoay khay test** | `.../cmd/move` | `{"slot": 3}` | Xoay khay đến ngăn chỉ định để kiểm tra cơ cấu chuyển động. |
| **Đồng bộ lịch uống** | `.../cmd/sync_schedules` | `{"items": [{"h": 8, "m": 0, "mask": 3}, {"h": 12, "m": 30, "mask": 4}]}` | Mảng các mốc giờ và bitmask ngăn thuốc (bit 0=Slot 1, bit 1=Slot 2...). ESP32 ghi trực tiếp vào mảng `sched[10]` và lưu Flash NVS. |
| **Mở ngăn nạp thuốc** | `.../cmd/refill` | `{"slot": 2}` | Xoay ngăn cần thêm thuốc ra cửa nạp và phát `REFILL_READY`. |
| **Hoàn tất nạp thuốc** | `.../cmd/refill_done` | `{"slot": 2, "qty": 30, "dose": 1, "name": "PANADOL"}` | Cập nhật số lượng tồn kho `qty`, liều dùng `dose`, tên thuốc `name` vào Flash NVS, sau đó tự động đóng khay về Ngăn 0. |
| **Xóa cờ kẹt cơ** | `.../cmd/reset_error` | `{"cmd": "RESET"}` | Ngắt điện cuộn dây motor (`motorDeenergize()`) để giải phóng kẹt cơ. |

---

### 2.2. Nhóm 2: Sự kiện từ ESP32 ➜ Web / Backend (`events/*`)
> **Backend đăng ký lắng nghe:** `pillbox/{DEVICE_ID}/events/#`

| Chức năng | Topic | Payload Tối ưu | Kích thước | Xử lý trên Backend & Database |
|---|---|---|:---:|---|
| **Khay đến vị trí** | `.../events/dispense` | `{"event":"SLOT_REACHED","slot":2}` | 37 bytes | Cập nhật phiên uống thuốc sang `DISPENSED`, đẩy WebSocket tới Web UI. |
| **Khay về Ngăn 0** | `.../events/home` | `{"event":"HOME_REACHED","slot":0}` | 36 bytes | Đưa trạng thái FSM về `SAFE_HOME_IDLE`, khay đã khóa an toàn. |
| **Xác nhận đã uống** | `.../events/taken` | `{"event":"TAKEN","slot":2,"dose":1}` | 37 bytes | Đánh dấu `dose_logs.status = 'TAKEN'`, **trừ tồn kho trong DB (`slots.current_quantity -= dose`)**. |
| **Quá hạn bỏ lỡ** | `.../events/missed` | `{"event":"MISSED","slot":2}` | 31 bytes | Tạo bản ghi `dose_logs` với trạng thái `MISSED`, gửi cảnh báo Web. |
| **Cảnh báo đến giờ** | `.../events/due_alert` | `{"event":"DUE_ALERT","mask":3}` | 34 bytes | Thông báo có cữ thuốc đến giờ theo bitmask các ngăn cần uống. |
| **Tạo/Xóa lịch từ EC11** | `.../events/schedule` | `{"event":"SCHEDULE_SAVED","h":8,"m":0,"mask":3}` | 51 bytes | Backend tự giải mã bitmask để tạo/cập nhật từng ngăn trong bảng `schedules`. Nếu `mask=0`, Backend xóa mốc giờ này. |
| **Sẵn sàng nạp thuốc** | `.../events/refill` | `{"event":"REFILL_READY","slot":2}` | 37 bytes | Báo Web UI biết khay đã mở sẵn sàng để nạp thuốc. |
| **Báo động lỗi kẹt** | `.../events/alert` | `{"event":"ERROR","code":"JAMMED","message":"Motor stall"}` | 58 bytes | Kích hoạt chuông báo động trên Web UI và chuyển FSM sang `ERROR_JAMMED`. |

---

### 2.3. Nhóm 3: Giám sát trạng thái & Đo lường định kỳ (`state/*` & `lwt`)

#### 1. Trạng thái thiết bị (`pillbox/{DEVICE_ID}/state/status`):
- **Tần suất:** Khi vừa kết nối Broker thành công hoặc khi đổi trạng thái FSM.
- **QoS:** 1 (Retain = true)
- **Payload tối ưu:**
```json
{
  "status": "ONLINE",
  "fsm": "SAFE_HOME_IDLE",
  "slot": 0
}
```

#### 2. Đo lường định kỳ (`pillbox/{DEVICE_ID}/state/telemetry`):
- **Tần suất:** Mỗi 3 giây (`TELEMETRY_INTERVAL_MS = 3000`).
- **QoS:** 0 (Không giữ Retain, giảm tải mạng LAN).
- **Payload tối ưu siêu gọn (< 120 bytes):**
```json
{
  "temp": 28,
  "hum": 65,
  "rssi": -55,
  "heap": 268420,
  "slot": 0,
  "fsm": "SAFE_HOME_IDLE",
  "up": 1420,
  "ip": "192.168.1.105"
}
```

---

## 3. BẢO TOÀN TÍNH TOÀN VẸN VÀ ĐỒNG BỘ DỮ LIỆU GIỮA FLASH NVS VÀ SQLITE DATABASE

Yêu cầu cốt lõi của SmartMed là: **Tối ưu payload không được làm lệch dữ liệu giữa Flash NVS trên ESP32 và Database SQLite trên Backend.**

### 3.1. Cấu trúc lưu trữ 2 bên (Flash NVS vs SQLite Database)

```
       ESP32 FLASH NVS (Namespace "medbox")             BACKEND SQLITE (pillbox.db)
┌───────────────────────────────────────────────┐     ┌───────────────────────────────────────────────┐
│ 1. "meds": Mảng 7 phần tử Med:                │     │ 1. Bảng `slots`:                              │
│    - name[11], qty (uint8_t), dose (uint8_t)  │ ◄─► │    - slot_number (1..7), current_quantity     │
│ 2. "sch": Mảng 10 phần tử Sched:              │     │ 2. Bảng `medicines`:                          │
│    - h (uint8_t), m (uint8_t), mask (uint8_t) │     │    - name, dosage_unit                        │
│ 3. "nsch": Số lượng lịch đã cài (uint8_t)     │ ◄─► │ 3. Bảng `schedules`:                          │
│ 4. "hist": Mảng 30 phần tử Hist:              │     │    - slot_number, time_of_day, dose_quantity  │
│    - day, min, slot, type                     │ ◄─► │ 4. Bảng `dose_logs`:                          │
└───────────────────────────────────────────────┘     │    - slot_number, status, trigger_source      │
                                                      └───────────────────────────────────────────────┘
```

---

### 3.2. Bảng ánh xạ trường dữ liệu giữa Payload, NVS và Database

| Nghiệp vụ | Payload MQTT | Flash NVS (ESP32) | SQLite Database (Backend) | Đảm bảo toàn vẹn |
|---|---|---|---|---|
| **Lịch uống (Giờ & Ngăn)** | `{"h": 8, "m": 0, "mask": 3}` | `sched[i].h = 8;`<br>`sched[i].m = 0;`<br>`sched[i].mask = 3;` | `schedules`: Ghi 2 dòng riêng biệt: `(slot: 1, 08:00)`, `(slot: 2, 08:00)` | **Khớp 100%:** Bit 0 ứng với Slot 1, Bit 1 ứng với Slot 2. Backend phân rã bitmask ra từng dòng DB; khi gửi xuống ESP32 thì gom lại thành bitmask. |
| **Xóa lịch** | `{"h": 8, "m": 0, "mask": 0}` | Xóa phần tử khỏi mảng `sched[]`, giảm `nSched--` | `DELETE FROM schedules WHERE time_of_day = '08:00'` | Khi giữ nút EC11 xóa cữ giờ trên hộp thuốc, ESP32 phát `mask: 0`, Backend xóa sạch các slot của cữ đó trong SQLite. |
| **Trừ tồn kho khi uống** | `{"slot": 2, "dose": 1}` | `meds[1].qty -= 1;`<br>`saveCfg();` | `UPDATE slots SET current_quantity = current_quantity - 1 WHERE slot_number = 2` | Dù người bệnh uống theo lịch Web hay bấm nút lấy trực tiếp trên hộp (kể cả khi mất mạng tạm thời), cả 2 bên đều trừ đúng liều `dose`. |
| **Nạp thuốc mới** | `{"slot": 2, "qty": 30, "dose": 1, "name": "PANADOL"}` | `meds[1].qty = 30;`<br>`meds[1].dose = 1;`<br>`strcpy(meds[1].name, "PANADOL");` | `UPDATE slots SET current_quantity = 30 WHERE slot_number = 2;`<br>`UPDATE medicines SET name = 'PANADOL' WHERE id = ...` | Đồng bộ đồng thời tên thuốc, cơ số viên và liều lượng. |
| **Nhật ký uống thuốc** | `{"event":"TAKEN", "slot": 2}` | Thêm bản ghi vào vòng đệm `hist[30]` | Thêm dòng mới vào `dose_logs` với `status = 'TAKEN'` | Cả 2 kho nhật ký đều ghi nhận thời gian và kết quả cữ uống. |

---

### 3.3. Cơ chế đồng bộ 2 chiều không xung đột (Conflict-Free Dual Sync)

#### Trường hợp A: Cài đặt lịch từ Web UI Dashboard
1. Người dùng chọn giờ `08:00`, tích chọn Ngăn 1 và Ngăn 2 trên giao diện Web.
2. Backend lưu 2 dòng vào bảng `schedules` của SQLite.
3. Service `schedule_service.py` tự động gom nhóm giờ `08:00` thành Bitmask: `(1 << 0) | (1 << 1) = 3`.
4. Backend gửi lệnh `cmd/sync_schedules` với payload:
   ```json
   {"items": [{"h": 8, "m": 0, "mask": 3}]}
   ```
5. ESP32 nhận lệnh, ghi đè vào mảng `sched[]`, gọi `sortSched()` sắp xếp và `saveCfg()` lưu vào Flash NVS.
6. **Kết quả:** Cả 2 bên đều có cùng thông tin cữ thuốc lúc 08:00 cho Ngăn 1 & Ngăn 2.

#### Trường hợp B: Cài đặt lịch trực tiếp trên hộp thuốc bằng Núm xoay EC11
1. Người dùng xoay EC11 chọn mốc giờ `08:00`, chọn sáng LED Ngăn 1 và Ngăn 2.
2. ESP32 tính `pickMask = 0b00000011 = 3`, lưu mảng `sched[]` và ghi NVS bằng `saveCfg()`.
3. ESP32 phát ngay bản tin MQTT lên topic `events/schedule`:
   ```json
   {"event": "SCHEDULE_SAVED", "h": 8, "m": 0, "mask": 3}
   ```
4. Backend nhận bản tin, duyệt vòng lặp 7 bit:
   - Bit 0 bật ➜ Upsert Slot 1 lúc 08:00 vào SQLite.
   - Bit 1 bật ➜ Upsert Slot 2 lúc 08:00 vào SQLite.
   - Các bit còn lại tắt ➜ Nếu trước đó trong DB có slot khác ở mốc 08:00, Backend tự động xóa để khớp hoàn toàn với phần cứng.
5. Backend phát WebSocket `schedule_updated` để màn hình Web tự cập nhật tức thì.

#### Trường hợp C: Uống thuốc Offline khi mất kết nối mạng
1. Đúng giờ hẹn, ESP32 tự kích hoạt chuông báo và xoay mở Ngăn 2.
2. Bệnh nhân lấy thuốc và bấm phím K0.
3. ESP32 trừ tồn kho `meds[1].qty -= meds[1].dose` và lưu vào Flash NVS.
4. Khi có lại mạng Wi-Fi, gói tin `events/taken` được gửi lên Broker.
5. Backend nhận sự kiện, tự động tạo bản ghi `dose_logs` với `trigger_source = 'LOCAL_SCHEDULE_BUTTON'` và trừ tồn kho tương ứng trong SQLite: `slots.current_quantity -= dose`.
6. **Kết quả:** Tồn kho giữa hộp thuốc và cơ sở dữ liệu trên máy chủ luôn luôn khớp nhau chính xác từng viên thuốc!

---

## 4. HƯỚNG DẪN KHỞI ĐỘNG MQTT BROKER VÀ THEO DÕI GIAO TIẾP 2 BÊN

### 4.1. Khởi chạy Broker nhanh bằng file batch
Trong thư mục gốc dự án đã có sẵn file script khởi động:
- **Nhấp đúp chuột vào file:** [`chay_broker.bat`](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/chay_broker.bat)
- Hoặc mở PowerShell tại thư mục dự án và chạy:
  ```powershell
  .\chay_broker.bat
  ```

---

### 4.2. Lắng nghe 2 bên nói chuyện bằng công cụ `listen_mqtt.py`
Để xem trực tiếp các gói tin JSON truyền qua lại giữa ESP32 và Backend với giao diện màu sắc trực quan:
- **Nhấp đúp chuột vào file:** [`lang_nghe_mqtt.bat`](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/lang_nghe_mqtt.bat)
- Hoặc chạy lệnh:
  ```powershell
  python listen_mqtt.py
  ```

**Ý nghĩa các màu sắc hiển thị:**
- 🔵 `▶ [WEB / BACKEND ➜ ESP32]` — Lệnh điều khiển phát xuống (Xanh dương).
- 🟢 `◀ [ESP32 ➜ WEB / BACKEND]` — Sự kiện phần cứng phát lên (Xanh lá cây).
- 🟡 `⚡ [TRẠNG THÁI / TELEMETRY]` — Nhiệt độ, độ ẩm, RAM Heap, Uptime (Vàng).

---

### 4.3. Lắng nghe bằng lệnh `mosquitto_sub` (CLI)
Nếu muốn sử dụng công cụ dòng lệnh tiêu chuẩn:
```powershell
& "C:\Program Files\mosquitto\mosquitto_sub.exe" -h 127.0.0.1 -p 1883 -t "pillbox/#" -v
```

---
*Tài liệu được hoàn thiện và đồng bộ toàn diện cho phiên bản SmartMed 3.4.0.*
