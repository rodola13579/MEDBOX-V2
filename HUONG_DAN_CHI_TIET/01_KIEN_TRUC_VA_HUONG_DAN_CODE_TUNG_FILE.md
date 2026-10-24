# HƯỚNG DẪN 01: KIẾN TRÚC MÃ NGUỒN VÀ HƯỚNG DẪN CODE TỪNG FILE TỪ ĐẦU

> **Mục tiêu tài liệu:** Giúp bạn hiểu toàn bộ cấu trúc dự án Firmware ESP32-S3 SmartMed, nắm rõ từng file có nhiệm vụ gì, hàm nào gọi hàm nào, và thứ tự xây dựng lại mã nguồn từ con số 0.

---

## 1. TỔNG QUAN KIẾN TRÚC HỆ THỐNG (3 TẦNG PHÂN CẤP)

Firmware SmartMed được thiết kế theo mô hình **3 Tầng Độc Lập (Layered Architecture)** kết hợp với **FreeRTOS Đa nhân (Dual-Core)** để đảm bảo an toàn y tế và không bao giờ bị nghẽn:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        MAIN.CPP (ĐIỀU PHỐI CHÍNH)                      │
│      Khởi tạo hệ thống & phân chia 2 Task chạy trên 2 nhân CPU độc lập   │
└───────────────────┬────────────────────────────────┬───────────────────┘
                    │                                │
    [CORE 0: Mạng & Âm thanh]         [CORE 1: Cơ khí & Giao diện]
                    │                                │
┌───────────────────▼─────────────┐   ┌──────────────▼───────────────────┐
│       TẦNG 2: SERVICES          │   │        TẦNG 3: MÁY TRẠNG THÁI     │
│ • network_service (Wi-Fi/NTP)   │   │ • fsm_controller (Máy trạng thái)│
│ • mqtt_service (Truyền nhận)    │   │   - 7 trạng thái y tế an toàn    │
│ • storage_service (NVS Flash)   │   │   - Điều phối logic cấp thuốc    │
│ • scheduler_service (Lập lịch)  │   │                                  │
│ • tts_service (Tải giọng nói)   │   │                                  │
└───────────────────┬─────────────┘   └──────────────┬───────────────────┘
                    │                                │
┌───────────────────▼────────────────────────────────▼───────────────────┐
│                   TẦNG 1: DRIVERS (ĐIỀU KHIỂN PHẦN CỨNG)                │
│ • motor_driver: Động cơ bước 28BYJ-48 + ULN2003 (Half-step, De-energize)│
│ • homing_sensor: Cảm biến quang TCRT5000 tìm mốc Slot 0 (Safe Home)    │
│ • audio_player: Loa I2S MAX98357A + LittleFS phát file MP3             │
│ • display_ui: Màn hình TFT 2.4" ST7789 qua PSRAM Sprite 60fps          │
│ • rotary_encoder: Núm xoay EC11 đọc bước xoay & nút bấm khử rung       │
│ • dht_sensor: Cảm biến nhiệt độ & độ ẩm môi trường DHT11              │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. THỨ TỰ XÂY DỰNG FILE TỪ CON SỐ 0 (CODE THEO THỨ TỰ NÀY)

Khi bắt đầu code một dự án nhúng từ đầu, **nguyên tắc vàng** là:
1. Viết file cấu hình chung trước (chân cẳng, thông số).
2. Viết tầng Driver phần cứng cơ sở (test từng linh kiện).
3. Viết tầng Service lưu trữ và mạng.
4. Viết tầng FSM (Bộ não điều khiển logic nghiệp vụ).
5. Cuối cùng mới viết `main.cpp` để xâu chuỗi tất cả lại.

---

### BƯỚC 1: FILE CẤU HÌNH HỆ THỐNG

#### 📁 `include/config.h`
* **Mục đích:** Là "trung tâm đầu não cấu hình". Tất cả chân GPIO, tham số thời gian, thông tin Wi-Fi, MQTT đều nằm ở đây. Không bao giờ gõ số chân cứng (hardcode) vào file code khác.
* **Sơ đồ đấu nối Phần cứng Module tích hợp ST7789 2.4" + EC11 + Nút K0 (Header 12 chân JP1):**

| Chân JP1 | Tên chân Module | Chức năng kỹ thuật | Kết nối ESP32-S3 | Chế độ IO / Ghi chú |
|:---:|:---:|---|:---:|---|
| **1** | **GND** | Mass nguồn (Power source Ground) | **GND** | Nối Mass chung hệ thống |
| **2** | **VCC** | Nguồn dương (3.3V power supply) | **3.3V** | Nguồn 3.3V của ESP32-S3 |
| **3** | **SCL** | Clock SPI màn hình TFT | **GPIO 12** | SPI SCLK (`PIN_TFT_SCLK`) |
| **4** | **SDA** | Data MOSI SPI màn hình TFT | **GPIO 11** | SPI MOSI (`PIN_TFT_MOSI`) |
| **5** | **RES** | Reset phần cứng màn hình TFT | **GPIO 8** | Output Reset (`PIN_TFT_RST`) |
| **6** | **DC** | Data / Command Selection | **GPIO 9** | Output Data/Cmd (`PIN_TFT_DC`) |
| **7** | **CS** | Chip Select màn hình (Low active) | **GPIO 10** | Output Chip Select (`PIN_TFT_CS`) |
| **8** | **BLK** | Đèn nền màn hình (Backlight) | **GPIO 3** | Nối GPIO 3 (hoặc 3.3V / để hở vì mặc định mạch đã kéo sáng) |
| **9** | **A** | Phase A núm xoay EC11 | **GPIO 38** | Input Pullup (`PIN_EC11_CLK`, Ngắt quay) |
| **10** | **B** | Phase B núm xoay EC11 | **GPIO 39** | Input Pullup (`PIN_EC11_DT`, Đọc hướng quay) |
| **11** | **PUSH** | Nút bấm tích hợp trên trục EC11 | **GPIO 40** | Input Pullup (`PIN_EC11_SW`, Xác nhận / Uống thuốc / Click) |
| **12** | **KEY0 (K0)** | Nút bấm phụ vật lý ngoài | **GPIO 2** | Input Pullup (`PIN_BTN_K0`, Menu Return / Thoát / Xác nhận) |

* **Các ngoại vi khác trên ESP32-S3:**
  - `PIN_MOTOR_IN1..4` (GPIO 4, 5, 6, 7): 4 chân điều khiển động cơ bước qua driver ULN2003.
  - `PIN_TCRT_SENSOR` (GPIO 1): Chân Digital Out cảm biến TCRT5000 dò vạch mốc Slot 0 (Safe Home).
  - `PIN_I2S_BCLK, LRC, DIN` (GPIO 15, 16, 17): Giao tiếp âm thanh I2S xuất ra loa MAX98357A.
  - `PIN_DHT_DATA` (GPIO 41): Chân 1-Wire cảm biến nhiệt độ & độ ẩm DHT11.
  - `TELEMETRY_INTERVAL_MS` (3000ms): Chu kỳ gửi dữ liệu cảm biến lên Web Dashboard.

> [!WARNING]
> **CẢNH BÁO TỬ HUYỆT ESP32-S3 N16R8:** Tuyệt đối **CẤM DÙNG GPIO 26..37** vì đây là bus nội bộ của Octal Flash 16MB & Octal PSRAM 8MB. Toàn bộ chân cấu hình ở trên (1, 2, 3, 4..12, 15..17, 38..41) đều nằm ngoài vùng cấm, đảm bảo an toàn 100%.

* **Đoạn mã mẫu chuẩn trong `config.h`:**
```cpp
// [Cụm hiển thị & điều khiển: TFT 2.4" ST7789 + Núm xoay EC11 + Nút K0 (Header 12 chân JP1)]
#define PIN_TFT_MOSI            11      // Chân 4 (SDA) trên JP1
#define PIN_TFT_SCLK            12      // Chân 3 (SCL) trên JP1
#define PIN_TFT_CS              10      // Chân 7 (CS) trên JP1
#define PIN_TFT_DC              9       // Chân 6 (DC) trên JP1
#define PIN_TFT_RST             8       // Chân 5 (RES) trên JP1
#define PIN_TFT_BLK             3       // Chân 8 (BLK) trên JP1 (hoặc để hở/3.3V)

#define PIN_EC11_CLK            38      // Chân 9 (A) trên JP1
#define PIN_EC11_DT             39      // Chân 10 (B) trên JP1
#define PIN_EC11_SW             40      // Chân 11 (PUSH) trên JP1
#define PIN_BTN_K0              2       // Chân 12 (KEY0/K0) trên JP1
```

#### 📁 `include/mqtt_topics.h`
* **Mục đích:** Định nghĩa toàn bộ tên kênh MQTT (Topic) để đồng bộ 100% với Backend FastAPI.
* **Các kênh chính:**
  - Lệnh từ Web xuống: `pillbox/ESP32_PILLBOX_01/cmd/dispense`, `cmd/move`, `cmd/home`...
  - Báo cáo từ ESP32 lên: `pillbox/ESP32_PILLBOX_01/state/telemetry`, `events/dispense`...

---

### BƯỚC 2: TẦNG DRIVERS (ĐIỀU KHIỂN THIẾT BỊ NGOẠI VI)

Mỗi Driver đều là một Class C++ đóng gói riêng biệt:

#### 1. `drivers/motor_driver.h` và `.cpp`
* **Mục đích:** Điều khiển động cơ bước 28BYJ-48 xoay khay thuốc chính xác từng góc $45^\circ$.
* **Hàm quan trọng:**
  * `init()`: Cài đặt 4 chân GPIO làm `OUTPUT`.
  * `stepRelative(steps, delayUs)`: Phát xung theo bảng Half-step 8 bước (CW/CCW).
  * `rotateToSlot(slot)`: Tính góc quay ngắn nhất từ ngăn hiện tại tới ngăn đích (0..7) rồi quay.
  * `deenergize()`: **Cực kỳ quan trọng!** Ngắt toàn bộ điện áp 4 chân để IC ULN2003 và động cơ **không bị nóng ran** khi đứng yên.

#### 2. `drivers/homing_sensor.h` và `.cpp`
* **Mục đích:** Đọc cảm biến quang TCRT5000 để xác định vị trí mốc gốc Slot 0 (Safe Home).
* **Hàm quan trọng:**
  * `init()`: Cài đặt chân cảm biến ở chế độ `INPUT_PULLUP`.
  * `isAtHome()`: Đọc `digitalRead()`. Trả về `true` khi gặp mốc phản xạ.
  * `searchHome()`: Ra lệnh cho motor nhích từng bước chậm rãi cho tới khi `isAtHome() == true`, dừng lại và đặt `currentSlot = 0`.

#### 3. `drivers/audio_player.h` và `.cpp`
* **Mục đích:** Đọc file nhạc MP3 trong bộ nhớ Flash LittleFS và giải mã đẩy ra IC MAX98357A qua giao tiếp I2S.
* **Hàm quan trọng:**
  * `init()`: Mount phân vùng `littlefs` và cấu hình I2S chân BCLK, LRC, DIN.
  * `play(filepath, priority)`: Nhận đường dẫn file (ví dụ `"/audio/alert_due.mp3"`) và cấp ưu tiên.
  * `loop()`: Đẩy dữ liệu âm thanh vào DMA buffer liên tục (gọi trong Core 0).
  * `stop()`: Dừng phát tức thì.

#### 4. `drivers/dht_sensor.h` và `.cpp`
* **Mục đích:** Đọc nhiệt độ và độ ẩm từ cảm biến DHT11.
* **Hàm quan trọng:**
  * `init()`: Khởi động thư viện DHT.
  * `update()`: Kiểm tra theo chu kỳ 3 giây thì gọi `forceRead()`.
  * `forceRead()`: Đọc dữ liệu, loại bỏ lỗi `nan`, làm tròn thành số nguyên (`roundf`).
  * `getTemperature()`, `getHumidity()`: Trả về kết quả cho hệ thống.

#### 5. `drivers/rotary_encoder.h` và `.cpp`
* **Mục đích:** Đọc bước xoay trái/phải của núm xoay EC11 và xử lý 2 nút bấm vật lý trên module:
  - **Nút PUSH (chân 11):** Bấm ngắn để chọn / xác nhận uống thuốc; Bấm giữ 3s để thoát lỗi kẹt cơ.
  - **Nút K0 (chân 12):** Bấm để Return / Thoát khỏi Menu cấu hình về màn hình chính; Hoặc dùng làm phím xác nhận phụ.
* **Hàm quan trọng:**
  * `init()`: Cài đặt `INPUT_PULLUP` cho Phase A, B, PUSH và K0. Gắn ngắt phần cứng (Interrupt) trên chân CLK.
  * `update()`: Đọc bước xoay và khử rung (Debounce) cho cả 2 nút PUSH và K0.
  * `getDelta()`: Trả về `+1` (xoay phải) hoặc `-1` (xoay trái).
  * `getButtonEvent()`: Trả về các sự kiện: `BTN_EVENT_CLICK`, `BTN_EVENT_HOLD_3S`, hoặc `BTN_EVENT_K0_CLICK`.
  * `isPressed()`, `isK0Pressed()`: Kiểm tra trạng thái đang đè của từng nút.

#### 6. `drivers/display_ui.h` và `.cpp`
* **Mục đích:** Vẽ giao diện đồ họa y tế chuyên nghiệp lên màn hình màu ST7789 240x320.
* **Điểm kỹ thuật đặc biệt:** Tận dụng 8MB PSRAM để tạo **TFT_eSprite Full-Screen** (Double Buffering), vẽ toàn bộ màn hình trên RAM rồi mới bắn ra SPI trong 1 lần $\rightarrow$ **Triệt tiêu hoàn toàn hiện tượng nhấp nháy màn hình (flicker-free)**.
* **Hàm quan trọng:**
  * `init()`: Khởi tạo ST7789 SPI, cấp phát 153KB Sprite buffer trên PSRAM.
  * `renderBoot(text, percent)`: Vẽ thanh tiến trình lúc khởi động.
  * `renderDashboard(data)`: Vẽ đồng hồ lớn, nhiệt ẩm, trạng thái khay và icon Wi-Fi/MQTT.
  * `renderAlerting(alertData)`: Vẽ màn hình cảnh báo đỏ viền chớp "ĐẾN GIỜ UỐNG THUỐC".
  * `renderErrorJammed()`: Vẽ màn hình cảnh báo kẹt cơ.

---

### BƯỚC 3: TẦNG SERVICES (DỮ LIỆU & TRUYỀN THÔNG)

#### 1. `services/storage_service.h` và `.cpp`
* **Mục đích:** Quản lý bộ nhớ NVS Flash không bay hơi (kể cả khi mất điện rút nguồn).
* **Hàm quan trọng:**
  * `saveFsmState(state)` / `getLastFsmState()`: Phục hồi trạng thái sau mất nguồn (Power Loss Recovery).
  * `saveSchedule(slot, schedule)` / `getSchedule(slot)`: Lưu trữ giờ uống thuốc từng ngăn.
  * `pushOfflineLog()` / `flushOfflineLogs()`: Lưu lịch sử uống thuốc vào Flash nếu lúc đó bị mất mạng Wi-Fi, khi có mạng lại tự động bắn bù lên Server.

#### 2. `services/scheduler_service.h` và `.cpp`
* **Mục đích:** Lấy giờ chuẩn qua NTP Internet (UTC+7) và so khớp từng giây xem đã đến cữ uống thuốc chưa.
* **Hàm quan trọng:**
  * `tick()`: Gọi mỗi giây 1 lần để kiểm tra `giờ:phút` hiện tại với danh sách lịch.
  * `setDoseDueCallback(cb)`: Đăng ký hàm thông báo khi phát hiện cữ thuốc đến giờ.

#### 3. `services/network_service.h` và `.cpp`
* **Mục đích:** Quản lý kết nối Wi-Fi ngầm, tự động kết nối lại khi rớt mạng mà không làm đơ máy.
* **Hàm quan trọng:**
  * `init(ssid, pass)`: Bắt đầu kết nối Wi-Fi non-blocking.
  * `update()`: Kiểm tra trạng thái và xử lý Reconnect.
  * `isConnected()`: Trả về trạng thái mạng.

#### 4. `services/mqtt_service.h` và `.cpp`
* **Mục đích:** Kết nối Mosquitto Broker, giải mã lệnh JSON từ Web và xuất bản trạng thái.
* **Hàm quan trọng:**
  * `update()`: Duy trì kết nối MQTT, tự động kết nối lại mỗi 5s (không làm nghẽn âm thanh).
  * `parseAndDispatchCommand()`: Nhận gói tin JSON từ Web, bóc tách lệnh (`MOVE`, `DISPENSE`, `HOME`...) và gửi qua callback.
  * `publishTelemetry()`: Đóng gói nhiệt độ, độ ẩm, RSSI gửi lên Web mỗi 3 giây.
  * `publishStatus()`: Gửi bản tin `ONLINE` khi kết nối thành công.

---

### BƯỚC 4: TẦNG MÁY TRẠNG THÁI (FINITE STATE MACHINE - FSM)

#### 📁 `fsm/fsm_controller.h` và `.cpp`
* **Mục đích:** Đây là **"bộ não"** quyết định hộp thuốc đang làm gì, ngăn ngừa xung đột.
* **7 Trạng thái y tế an toàn:**
  1. `FSM_STATE_HOMING_SEARCH`: Đang xoay rà tìm mốc Slot 0.
  2. `FSM_STATE_SAFE_HOME_IDLE`: Khay đóng kín an toàn tại Slot 0, sẵn sàng.
  3. `FSM_STATE_ALERTING_DUE`: Báo động còi/màn hình nhắc bệnh nhân (khay vẫn đóng kín).
  4. `FSM_STATE_ROTATING_TO_DOSE`: Người dùng bấm nút $\rightarrow$ Khay mới xoay ra ngăn thuốc.
  5. `FSM_STATE_WAITING_TAKEN`: Mở ngăn thuốc, phát loa hướng dẫn, đếm ngược 5 phút.
  6. `FSM_STATE_ROTATING_TO_HOME`: Người dùng lấy thuốc xong bấm nút $\rightarrow$ Tự đóng về Slot 0.
  7. `FSM_STATE_ERROR_JAMMED`: Kẹt cơ, dừng khẩn cấp và báo động.
* **Hàm quan trọng:**
  * `transitionTo(newState)`: Chuyển đổi trạng thái an toàn (khoá nút khi motor quay, dừng loa...).
  * `update()`: Quét logic đếm ngược thời gian và cập nhật giao diện.

---

### BƯỚC 5: FILE ĐIỀU PHỐI CHÍNH `main.cpp`

* **Mục đích:** Khởi tạo tất cả Driver/Service ở hàm `setup()`, sau đó giao việc cho 2 nhân CPU chạy song song bằng FreeRTOS:
  * **CORE 0 (Tác vụ mạng & Audio):**
    ```cpp
    void networkAudioTask(void* parameter) {
        while (true) {
            network.update();
            mqttService.update();
            audioPlayer.loop(); // Bơm âm thanh I2S
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    ```
  * **CORE 1 (Tác vụ cơ khí & Giao diện):**
    ```cpp
    void controlUITask(void* parameter) {
        while (true) {
            encoder.update();
            dhtSensor.update();
            scheduler.tick();
            fsm.update(); // Cập nhật màn hình & động cơ
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    ```

---

## 3. SƠ ĐỒ GỌI HÀM KHI CÓ LỆNH XOAY KHAY TỪ WEB (CALL FLOW MAP)

Khi bạn bấm nút **"Xoay về Ngăn 2"** trên Web Dashboard, luồng lệnh chạy xuyên suốt như sau:

```
[Trình duyệt Web Dashboard]
       │ (1) Bấm nút Ngăn 2 -> Gửi HTTP POST /api/v1/device/move/2
       ▼
[Backend FastAPI (main.py)]
       │ (2) Tạo JSON: {"cmd":"MOVE", "target_slot": 2}
       │     Bắn MQTT Topic: pillbox/ESP32_PILLBOX_01/cmd/move
       ▼
[Mosquitto Broker (Cổng 1883)]
       │ (3) Chuyển tiếp gói tin qua Wi-Fi tới ESP32
       ▼
[ESP32: mqttService (Core 0)]
       │ (4) Hàm handleIncomingMessage() nhận gói tin
       │ (5) Hàm parseAndDispatchCommand() phát hiện CMD_TYPE_MOVE
       │     Gọi Callback: mqttService.setCommandCallback
       ▼
[ESP32: main.cpp (Core 0)]
       │ (6) Nhận lệnh CMD_TYPE_MOVE với slot = 2
       │ (7) Gọi: motor.rotateToSlot(2)   <--- ĐỘNG CƠ BƯỚC XOAY
       │ (8) Gọi: motor.deenergize()      <--- NGẮT ĐIỆN CHỐNG NÓNG IC
       │ (9) Gọi: mqttService.publishDispenseReached(2)
       ▼
[Mosquitto Broker -> Backend -> Web Dashboard]
       │ (10) Web nhận được event SLOT_REACHED -> Khay 3D trên Web xoay đồng bộ!
```
