# HƯỚNG DẪN 04: KẾ HOẠCH THỰC CHIẾN XÂY DỰNG FIRMWARE THEO TỪNG TÁC VỤ

> **Phương châm:** "Xây xong phân hệ nào — Nạp kiểm thử trọn vẹn tác vụ phân hệ đó — Gọn gàng, dễ hiểu, không rối rắm".  
> Bạn luôn biết mình đang ở bước nào, cần tạo/sửa file gì và nạp xong sẽ thấy gì trên mạch thật.

---

## 📊 BẢNG THEO DÕI TIẾN ĐỘ THỰC HIỆN (CHECKLIST TỔNG)

Đánh dấu `[x]` vào từng bước khi bạn hoàn thành:

```markdown
- [x] GIAI ĐOẠN 0: Khởi tạo Nền tảng Dự án (Cấu hình chip ESP32-S3 N16R8, Flash 16MB, PSRAM 8MB)
- [x] GIAI ĐOẠN 1: Phân hệ Màn hình Hiển thị ST7789 (Giao diện ngang 320x240, Medbox, 4 Task Menu, Thẻ y tế)
- [x] GIAI ĐOẠN 2: Phân hệ Động cơ & Khay xoay (Half-step, De-energize ngắt điện, TCRT5000 Auto-Homing Slot 0)
- [x] GIAI ĐOẠN 3: Phân hệ Ngoại vi Tương tác (Núm xoay EC11 + Nút K0 + Cảm biến nhiệt ẩm thực tế DHT11)
- [x] GIAI ĐOẠN 5: Phân vùng Flash & Phân hệ Âm thanh I2S (LittleFS 8.8MB + Loa phát MP3 Tiếng Việt)
- [ ] GIAI ĐOẠN 4: Phân hệ Mạng & Truyền thông MQTT (Wi-Fi, NTP UTC+7, Mosquitto Broker trên PC)
- [ ] GIAI ĐOẠN 6: Hợp nhất Toàn diện vào FSM 7 Trạng thái & FreeRTOS Đa nhân (Hoàn tất hệ thống)
```

---

## 🛠️ CHI TIẾT TỪNG GIAI ĐOẠN THỰC CHIẾN

---

### 🟢 GIAI ĐOẠN 0: KHỞI TẠO NỀN TẢNG DỰ ÁN MỚI
* **Mục tiêu:** Tạo bộ khung cấu hình chuẩn trong thư mục `firmware/` để PlatformIO nhận đúng chip ESP32-S3 N16R8, cấp phát đúng 16MB Flash và 8MB PSRAM.
* **Các file cần tạo trong `firmware/`:**
  1. `platformio.ini`: Chỉ thị biên dịch, thư viện `lib_deps`, cờ build màn hình ST7789 và cổng Type-C CDC.
  2. `partitions_16mb.csv`: Bảng phân vùng bộ nhớ Flash (OTA Dual App 3.5MB + LittleFS 8.8MB).
  3. `include/config.h`: Chứa định nghĩa sơ đồ chân GPIO an toàn (tránh tuyệt đối GPIO 26..37).
  4. `src/main.cpp`: In thông số bo mạch ra Serial Monitor.
* **Thao tác nạp:** Bấm nút **Upload (➔)** trên VS Code $\rightarrow$ Bấm nút **Serial Monitor (🔌)**.
* **Dấu hiệu PASS kiểm thử:**  
  Serial in ra `Flash Size: 16 MB` và `[PASS] PSRAM khoi tao thanh cong! Tong dung luong: 8192 KB (~8MB)`.

---

### 🟢 GIAI ĐOẠN 1: PHÂN HỆ MÀN HÌNH HIỂN THỊ (DISPLAY UI SUBSYSTEM)
* **Mục tiêu:** Xây dựng toàn bộ giao diện đồ họa y tế chuyên nghiệp lên màn hình IPS ST7789 2.4", tận dụng PSRAM để tạo **Sprite 153KB Full-Screen (Double Buffering)** giúp triệt tiêu hoàn toàn hiện tượng nhấp nháy màn hình.
* **Các file tạo mới:**
  1. `src/drivers/display_ui.h` & `src/drivers/display_ui.cpp`: Lớp đối tượng `DisplayUI` quản lý đồ họa.
  2. `src/main.cpp`: Code điều khiển chạy demo tuần tự qua các màn hình.
* **5 Tác vụ màn hình cần hoàn thành:**
  * **Tác vụ 1.1 — Màn hình Khởi động (Boot Screen):** Thanh phần trăm `Loading... 0% -> 100%`.
  * **Tác vụ 1.2 — Màn hình Dashboard chính:** Đồng hồ số to rõ, thẻ hiển thị cữ thuốc tiếp theo, nhiệt độ độ ẩm, icon Wi-Fi/MQTT và biểu tượng khay thuốc.
  * **Tác vụ 1.3 — Màn hình Cảnh báo cữ thuốc (Alert Due):** Viền đỏ chớp nháy, chuông báo, đếm ngược 15 phút.
  * **Tác vụ 1.4 — Màn hình Hướng dẫn uống thuốc (Dose Info):** Tên thuốc, liều lượng, số viên, thông báo khay đang mở.
  * **Tác vụ 1.5 — Màn hình Báo lỗi kẹt cơ (Error Jammed):** Cảnh báo màu đỏ cam, hướng dẫn thao tác gỡ kẹt.
* **Thao tác nạp:** Bấm **Upload (➔)** trên VS Code.
* **Dấu hiệu PASS kiểm thử:**  
  Màn hình sáng đẹp, màu sắc tương phản cao, tự động chuyển cảnh qua lại giữa các giao diện mượt mà 60 khung hình/giây (FPS) mà **mắt nhìn không thấy bất kỳ vệt quét hay chớp giật nào**.

---

### 🟢 GIAI ĐOẠN 2: PHÂN HỆ ĐỘNG CƠ & KHAY XOAY (MOTOR & MECHANISM)
* **Mục tiêu:** Xây dựng module điều khiển cơ khí chính xác: Động cơ bước xoay từng ngăn $45^\circ$, tự động ngắt điện cuộn dây chống nóng, và tự động dò mốc Slot 0 bằng cảm biến quang.
* **Các file tạo mới:**
  1. `src/drivers/motor_driver.h` & `src/drivers/motor_driver.cpp`: Điều khiển động cơ 28BYJ-48 qua driver ULN2003 (GPIO 4, 5, 6, 7).
  2. `src/drivers/homing_sensor.h` & `src/drivers/homing_sensor.cpp`: Đọc cảm biến quang TCRT5000 (GPIO 1).
  3. `src/main.cpp`: Kịch bản kiểm thử chuyển động khay.
* **3 Tác vụ cơ khí cần hoàn thành:**
  * **Tác vụ 2.1 — Xoay theo ngăn:** Xoay 512 bước ($45^\circ$ tương ứng 1 ngăn), xoay 4096 bước ($360^\circ$ tương ứng 1 vòng tròn), tự tính đường đi ngắn nhất giữa các ngăn.
  * **Tác vụ 2.2 — Ngắt điện bảo vệ (De-energize):** Ngắt điện toàn bộ 4 chân GPIO ngay khi dừng quay $\rightarrow$ Động cơ và IC ULN2003 **nguội mát**, không sinh nhiệt gây hỏng thuốc.
  * **Tác vụ 2.3 — Tự động dò mốc (Auto-Homing Slot 0):** Động cơ nhích từng bước rà tìm vạch phản xạ của TCRT5000, dừng chuẩn xác tại Slot 0 Safe Home. Nếu quá 4500 bước không thấy vạch thì kích hoạt cờ kẹt cơ `JAMMED`.
* **Thao tác nạp:** Bấm **Upload (➔)** trên VS Code.
* **Dấu hiệu PASS kiểm thử:**  
  Khay xoay mượt mà, dừng đúng vị trí; chức năng Homing đưa khay về đúng Slot 0; sờ tay vào động cơ và IC thấy nguội ngắt khi đứng yên.

---

### 🟢 GIAI ĐOẠN 3: PHÂN HỆ NGOẠI VI TƯƠNG TÁC (EC11 + K0 + DHT11)
* **Mục tiêu:** Xây dựng cụm điều khiển vật lý bằng tay và cảm biến môi trường, sau đó **ghép nối trực tiếp vào Màn hình** để điều khiển thực tế.
* **Các file tạo mới:**
  1. `src/drivers/rotary_encoder.h` & `src/drivers/rotary_encoder.cpp`: Đọc núm xoay EC11 (GPIO 38, 39) bằng ngắt phần cứng, khử rung nút nhấn PUSH (GPIO 40) và nút phụ K0 (GPIO 2).
  2. `src/drivers/dht_sensor.h` & `src/drivers/dht_sensor.cpp`: Đọc cảm biến nhiệt độ & độ ẩm DHT11 (GPIO 41).
  3. `src/main.cpp`: Liên kết tương tác giữa Núm xoay $\leftrightarrow$ Màn hình $\leftrightarrow$ Cảm biến.
* **Các tác vụ cần hoàn thành:**
  * **Tác vụ 3.1 — Tương tác Menu bằng núm xoay:** Vặn núm xoay sang phải/trái để cuộn chuyển đổi qua lại giữa các màn hình (Dashboard $\leftrightarrow$ Lịch thuốc $\leftrightarrow$ Cài đặt).
  * **Tác vụ 3.2 — Thao tác nút bấm:** Bấm nút PUSH để xác nhận / mở thẻ cữ thuốc; Bấm nút K0 để thoát về màn hình Dashboard.
  * **Tác vụ 3.3 — Hiển thị nhiệt ẩm thật:** Cảm biến DHT11 đọc nhiệt độ, độ ẩm phòng thực tế và cập nhật trực tiếp lên góc màn hình TFT.
* **Thao tác nạp:** Bấm **Upload (➔)** trên VS Code.
* **Dấu hiệu PASS kiểm thử:**  
  Dùng tay vặn núm và bấm nút trên cụm module JP1 thấy màn hình chuyển đổi tức thì; nhiệt độ và độ ẩm phòng hiển thị chính xác.

---

### 🟢 GIAI ĐOẠN 4: PHÂN HỆ MẠNG & TRUYỀN THÔNG MQTT
* **Mục tiêu:** Kết nối Wi-Fi, lấy giờ chuẩn Internet qua NTP và bắt tay truyền dữ liệu với Mosquitto Broker trên máy tính.
* **Các file tạo mới:**
  1. `include/mqtt_topics.h`: Định nghĩa 12 kênh MQTT chuẩn với Backend.
  2. `src/services/network_service.h` & `.cpp`: Quản lý Wi-Fi non-blocking (tự kết nối lại khi rớt mạng).
  3. `src/services/mqtt_service.h` & `.cpp`: Kết nối Mosquitto, giải mã lệnh JSON (`MOVE`, `DISPENSE`, `HOME`...) và gửi báo cáo trạng thái (`telemetry`).
  4. `src/main.cpp`: Tích hợp mạng và cơ chế nhận lệnh điều khiển xoay khay.
* **Các tác vụ cần hoàn thành:**
  * **Tác vụ 4.1 — Đồng bộ đồng hồ NTP:** Lấy giờ chuẩn Việt Nam (UTC+7) hiển thị lên đồng hồ màn hình.
  * **Tác vụ 4.2 — Xuất bản Telemetry:** Bắn nhiệt độ, độ ẩm, RSSI lên broker mỗi 3 giây.
  * **Tác vụ 4.3 — Nhận lệnh xoay khay từ Broker:** Khi nhận gói tin JSON từ máy tính yêu cầu xoay về Ngăn $X$, động cơ lập tức xoay về ngăn đó và màn hình hiển thị cập nhật.
* **Thao tác nạp:** Bấm **Upload (➔)** trên VS Code.
* **Dấu hiệu PASS kiểm thử:**  
  Trên Web Dashboard hoặc MQTTX thấy ESP32 hiện đèn xanh `ONLINE`; bấm nút trên Web thấy khay thuốc thật ngoài đời tự xoay.

---

### 🟢 GIAI ĐOẠN 5: PHÂN VÙNG FLASH & PHÂN HỆ ÂM THANH I2S
* **Mục tiêu:** Nạp 10 file giọng nói tiếng Việt MP3 vào bộ nhớ Flash LittleFS và giải mã xuất ra loa I2S MAX98357A.
* **Các file tạo mới:**
  1. `data/audio/`: Chứa 10 file MP3 tiếng Việt (`alert_due.mp3`, `click.mp3`, `intro.mp3`...).
  2. `src/drivers/audio_player.h` & `src/drivers/audio_player.cpp`: Driver I2S giải mã âm thanh với bộ đệm trên PSRAM chống giật tiếng.
  3. `src/main.cpp`: Ghép âm thanh vào các sự kiện tương tác.
* **Thao tác nạp:**
  * **Bước A (Nạp file âm thanh vào Flash):** Chạy lệnh:
    ```powershell
    & "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t uploadfs -e esp32-s3-n16r8
    ```
  * **Bước B (Nạp code kiểm thử):** Bấm **Upload (➔)** trên VS Code.
* **Dấu hiệu PASS kiểm thử:**  
  Khi vặn núm xoay loa phát tiếng `click`, khi chuyển cảnh phát âm thanh chào mừng, loa kêu to rõ ràng, không bị rè hoặc đứt quãng.

---

### 🟢 GIAI ĐOẠN 6: HỢP NHẤT FSM 7 TRẠNG THÁI & FREERTOS ĐA NHÂN
* **Mục tiêu:** Lắp ráp toàn bộ các phân hệ đã kiểm thử thành cỗ máy SmartMed hoàn chỉnh theo chuẩn y tế an toàn.
* **Các file tạo mới:**
  1. `src/services/storage_service.h` & `.cpp`: Lưu lịch uống vào NVS Flash (chống mất điện).
  2. `src/services/scheduler_service.h` & `.cpp`: Quét lịch RTC so khớp cữ thuốc.
  3. `src/fsm/fsm_controller.h` & `.cpp`: Máy trạng thái 7 state điều phối toàn bộ chu trình cấp thuốc 2 giai đoạn an toàn.
  4. `src/main.cpp`: Chia 2 Task FreeRTOS chạy độc lập trên 2 nhân CPU:
     * **Core 0:** Tác vụ mạng (Wi-Fi, MQTT) + Giải mã âm thanh MP3 I2S.
     * **Core 1:** Tác vụ cơ khí + Giao diện TFT + Đọc núm xoay & FSM logic.
* **Thao tác nạp:** Bấm **Upload (➔)** trên VS Code.
* **Dấu hiệu PASS kiểm thử:**  
  Toàn bộ hệ thống chạy khép kín từ Khởi động $\rightarrow$ Auto-Homing $\rightarrow$ Đón nhận cữ thuốc $\rightarrow$ Cảnh báo chuông + đếm ngược tại Slot 0 $\rightarrow$ Nhấn nút mới xoay mở ngăn $\rightarrow$ Uống xong bấm nút tự quay về Slot 0 đóng kín an toàn.
