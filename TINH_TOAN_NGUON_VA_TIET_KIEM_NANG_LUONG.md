# BÁO CÁO TÍNH TOÁN NGUỒN ĐIỆN & KẾ HOẠCH NÂNG CẤP CHẾ ĐỘ NGỦ TIẾT KIỆN NĂNG LƯỢNG (SMARTMED PILLBOX)

> **Dự án:** Hộp thuốc thông minh SmartMed (ESP32-S3-WROOM-1 N16R8)  
> **Phiên bản tài liệu:** `1.0.0` | **Ngày lập:** 13/10/2026  
> **Mục tiêu:** Tính toán công suất tiêu thụ thực tế, định cỡ dung lượng nguồn/pin, và xây dựng cơ chế ngủ thông minh **tiết kiệm tới 85% năng lượng nhưng giữ nguyên 100% tính năng IoT & Y tế (Không trễ lệnh, không mất kết nối Web MQTT)**.

---

## PHẦN 1: BẢNG TÍNH TOÁN TIÊU THỤ NĂNG LƯỢNG HIỆN TẠI

Dựa trên việc bóc tách mã nguồn thực tế của Firmware (`firmware/src/` và `firmware/include/config.h`):

### 1.1. Bảng định mức dòng điện và thời gian hoạt động tổng hợp

| Ngoại vi / Linh kiện | Điện áp cấp | Dòng tĩnh (Idle / Chờ) | Dòng hoạt động (Active) | Thời gian hoạt động cụ thể trong ngày (24h) | Điện năng tiêu hao trong 24h (mAh/ngày) |
|---|:---:|:---:|:---:|:---:|:---:|
| **ESP32-S3 N16R8**<br>*(CPU 240MHz, Wi-Fi STA, MQTT)* | 3.3V | **95 mA** | Đỉnh **250 mA** (khi phát RF MQTT) | • Chạy liên tục: **24 giờ / 24 giờ (100%)**<br>• Phát RF Telemetry: **2.9 phút tích lũy/ngày** (10s gửi 1 lần) | $\approx \mathbf{2.400\text{ mAh}}$ |
| **Màn hình ST7789 2.4"**<br>*(Đèn nền BLK luôn bật mức MAX)* | 3.3V / 5V | **60 mA** | **60 mA** | • Sáng liên tục: **24 giờ / 24 giờ (1.440 phút/ngày)**<br>• Hiển thị đồng hồ, nhiệt độ, độ ẩm 24/7 | $\approx \mathbf{1.440\text{ mAh}}$ |
| **Cảm biến quang TCRT5000**<br>*(Dò mốc Slot 0 Safe Home)* | 3.3V / 5V | **12 mA** | **12 mA** | • Cấp điện LED hồng ngoại: **24 giờ / 24 giờ**<br>• Thời gian thực tế đọc cảm biến: **chỉ ~2 – 5 giây/ngày** | $\approx \mathbf{288\text{ mAh}}$ |
| **Cảm biến DHT11**<br>*(Nhiệt độ & độ ẩm phòng)* | 3.3V | **0.1 mA** | **1.2 mA** (khi lấy mẫu) | • Chu kỳ đọc: **2.5 giây/lần** (34.560 lần/ngày)<br>• Thời gian tích lũy lấy mẫu: **~11.5 phút/ngày** | $\approx \mathbf{3\text{ mAh}}$ |
| **Mạch Loa MAX98357A + Loa 3W**<br>*(Giọng nói nhắc uống thuốc)* | 5V | **2 mA** | **200 – 300 mA** (âm lượng 85%) | • **Kịch bản 4 ngăn/buổi:** **18 – 24 lần phát/ngày**<br>&nbsp;&nbsp;(Mỗi buổi 4 ngăn phát 6 câu: 1 câu nhắc + 4 câu mở từng ngăn + 1 câu cảm ơn; nhân 3-4 buổi/ngày)<br>• **Thời gian mỗi lần phát:** **3 – 5 giây/câu**<br>• **Tổng thời gian phát loa cả ngày:** **~1.0 – 2.0 phút/ngày** | $\approx \mathbf{6.5\text{ mAh}}$ *(Tải đỉnh ngắn)* |
| **Động cơ 28BYJ-48 + ULN2003**<br>*(Xoay mở & đóng khay thuốc)* | 5V | **0 mA**<br>*(Đã ngắt cuộn dây)* | **260 mA** (khi quay bước) | • **Kịch bản 4 ngăn/buổi:** **18 – 24 lần quay/ngày**<br>&nbsp;&nbsp;(Mỗi buổi 4 ngăn quay 8 lần: 4 lần mở + 4 lần đóng về Home; nhân 2-3 buổi 4 ngăn)<br>• **Thời gian mỗi lần quay:** **0.65 – 1.4 giây/lần** (xoay 1-2 ngăn)<br>• **Tổng thời gian quay cả ngày:** **~12 – 18 giây/ngày**<br>• **Thời gian giữ điện cuộn dây:** **0 giây** (ngắt ngay sau khi dừng) | $\approx \mathbf{1.5\text{ mAh}}$ *(Tải đỉnh ngắn)* |

---

### 1.2. Phân tích chi tiết thời gian và chu kỳ hoạt động cụ thể của từng phần cứng

#### 1. Vi điều khiển trung tâm ESP32-S3 (Wi-Fi & MQTT Client)
* **Thời gian hoạt động:** Chạy liên tục **24 giờ / 24 giờ (1.440 phút = 86.400 giây/ngày)**, CPU 2 nhân 240MHz không nghỉ.
* **Chi tiết chu kỳ truyền thông mạng:**
  * **Duy trì Socket MQTT:** Giữ kết nối TCP liên tục 24/7 với Mosquitto Broker (`mqttClient.loop()`), sẵn sàng nhận lệnh tức thời từ Web Dashboard.
  * **Chu kỳ phát Telemetry:** Cứ **10 giây một lần** (`TELEMETRY_INTERVAL_MS = 10000`).
    $$\text{Số lần phát trong ngày} = \frac{86.400\text{ giây}}{10\text{ giây}} = 8.640\text{ lần/ngày}$$
  * Mỗi lần phát sóng Wi-Fi (RF burst) kéo dài khoảng **15ms – 25ms**.
  * **Tổng thời gian phát sóng RF tích lũy trong ngày:**
    $$8.640 \times 0.02\text{ giây} \approx \mathbf{173\text{ giây } (\sim 2.9\text{ phút/ngày})}$$
  * Trong $23\text{ giờ } 57\text{ phút}$ còn lại, chip ở trạng thái chờ (Rx/Idle), dòng điện duy trì ở mức $\sim 95\text{mA}$.

#### 2. Màn hình màu TFT 2.4" ST7789 (Kèm Đèn nền Backlight)
* **Thời gian hoạt động:** Sáng liên tục **24 giờ / 24 giờ (100% thời gian trong ngày)**.
* **Chi tiết vận hành:** Chân điều khiển đèn nền `PIN_TFT_BLK` (GPIO 21) luôn ở mức `HIGH` với dòng kích cực đại `GPIO_DRIVE_CAP_3` (hoặc nối thẳng nguồn 3.3V). Màn hình luôn hiển thị đồng hồ thời gian thực (cập nhật mỗi phút hoặc khi có thay đổi trạng thái).
* **Tác động tiêu thụ:** Đèn nền là bộ phận ngốn điện thụ động lớn thứ hai trong hệ thống (sau CPU Wi-Fi), tiêu tốn liên tục **$60\text{mA} \times 24\text{h} = 1.440\text{ mAh/ngày}$** dù người dùng không nhìn vào màn hình.

#### 3. Động cơ bước 28BYJ-48 (5V) & Mạch đệm ULN2003 (Kịch bản 4 ngăn quay mỗi buổi)
* **Thời gian hoạt động thực tế:** Khoảng **12 – 18 giây trong cả ngày!**
* **Chi tiết từng lượt quay:**
  * Tốc độ xung bước: $1200\,\mu\text{s/bước}$ (`MOTOR_STEP_DELAY_US = 1200`), khay chia 8 ngăn $\rightarrow$ 512 bước/ngăn.
  * **Thời gian xoay 1 ngăn:**
    $$t_{\text{ngăn}} = 512 \times 1.2\text{ms} \approx \mathbf{0.65\text{ giây}}$$
  * Nhờ thuật toán tìm đường ngắn nhất vòng tròn ($\le 4$ ngăn), thời gian quay tối đa giữa 2 ngăn xa nhất bất kỳ là $\approx 4 \times 0.65\text{s} = \mathbf{2.6\text{ giây}}$.
* **Kịch bản hoạt động khi nâng lên 4 ngăn quay mỗi buổi:**
  * Để đảm bảo an toàn y tế tuyệt đối, tránh nhầm lẫn thuốc cho người cao tuổi, cửa xoay chỉ để lộ **1 ngăn thuốc duy nhất tại 1 thời điểm**. Do đó khi 1 buổi có **4 loại thuốc cần uống**:
    1. Động cơ xoay: $\text{Slot 0} \rightarrow \text{Ngăn 1}$ ($\sim 0.7\text{s}$) $\rightarrow$ Bệnh nhân lấy thuốc $\rightarrow$ Xoay về $\text{Slot 0}$ ($\sim 0.7\text{s}$).
    2. Động cơ xoay: $\text{Slot 0} \rightarrow \text{Ngăn 2}$ ($\sim 0.7\text{s}$) $\rightarrow$ Bệnh nhân lấy thuốc $\rightarrow$ Xoay về $\text{Slot 0}$ ($\sim 0.7\text{s}$).
    3. Động cơ xoay: $\text{Slot 0} \rightarrow \text{Ngăn 3}$ ($\sim 0.7\text{s}$) $\rightarrow$ Bệnh nhân lấy thuốc $\rightarrow$ Xoay về $\text{Slot 0}$ ($\sim 0.7\text{s}$).
    4. Động cơ xoay: $\text{Slot 0} \rightarrow \text{Ngăn 4}$ ($\sim 0.7\text{s}$) $\rightarrow$ Bệnh nhân lấy thuốc $\rightarrow$ Xoay về $\text{Slot 0}$ ($\sim 0.7\text{s}$).
  * **Số lần quay trong 1 buổi (cữ thuốc 4 ngăn):**
    $$4\text{ ngăn} \times 2\text{ lần quay (mở + đóng)} = \mathbf{8\text{ lần quay / buổi}}$$
  * **Thời gian quay trong 1 buổi (4 ngăn):**
    $$8\text{ lần quay} \times 0.7\text{ giây} \approx \mathbf{5.6\text{ giây / buổi}}$$
  * **Tổng tính cho cả ngày (Ví dụ: Buổi sáng 4 ngăn, Trưa 2 ngăn, Chiều 1 ngăn, Tối 2 ngăn = 9 ngăn thuốc):**
    $$\text{Tổng số lần quay/ngày} = 8\text{ (Sáng)} + 4\text{ (Trưa)} + 2\text{ (Chiều)} + 4\text{ (Tối)} = \mathbf{18\text{ lần quay/ngày}}$$
    $$\text{Tổng thời gian động cơ quay cả ngày} \approx 18 \times 0.7\text{s} \approx \mathbf{12.6\text{ giây/ngày}}$$
  * *Trường hợp tải cực đại (Cả 3 buổi đều uống 4 ngăn = 12 cữ uống):* $3 \times 8 = 24\text{ lần quay} \rightarrow$ **Tổng thời gian quay $\approx 16.8\text{ giây/ngày}$**.
* **Trạng thái giữ cuộn dây sau khi quay:**
  * **0 GIÂY (TUYỆT ĐỐI KHÔNG CẤP ĐIỆN GIỮ).** Ngay sau mỗi lần quay mở ngăn hoặc quay về Safe Home, hàm `motorDeenergize()` lập tức kéo 4 chân `IN1..IN4` về mức `LOW`. Do động cơ có hộp số giảm tốc 1:64 tự hãm cơ học chắc chắn, dòng điện cuộn dây bằng **0 mA** trong suốt $23\text{ giờ } 59\text{ phút } 45\text{ giây}$ còn lại của ngày.
* **Điện năng tiêu thụ của động cơ trong 24h:**
  $$Q_{\text{motor}} = 260\text{mA} \times \left(\frac{16.8\text{s}}{3600\text{s}}\right)\text{h} \approx \mathbf{1.2 – 1.5\text{ mAh/ngày}}$$
  *(Kể cả khi tăng lên 4 ngăn/buổi, điện năng của động cơ vẫn siêu nhỏ vì chỉ chạy chưa tới 20 giây/ngày)*.

#### 4. Phân hệ Âm thanh I2S MAX98357A & Loa 3W (Kịch bản 4 ngăn/buổi)
* **Thời gian hoạt động thực tế:** Dao động từ **1.0 phút đến 2.0 phút mỗi ngày**.
* **Chi tiết chu trình phát âm thanh trong 1 buổi uống 4 ngăn:**
  * 1 câu giọng nói nhắc cữ đầu buổi (`alert_due.mp3`): $\approx \mathbf{4\text{ giây}}$.
  * 4 câu giọng nói hướng dẫn mở từng ngăn (`dispense_slot.mp3`): $4 \times 3\text{s} = \mathbf{12\text{ giây}}$.
  * 1 câu cảm ơn khi người bệnh hoàn thành đủ 4 ngăn (`taken_success.mp3`): $\approx \mathbf{3\text{ giây}}$.
  $$\text{Tổng thời gian phát loa cho 1 buổi 4 ngăn} = 4\text{s} + 12\text{s} + 3\text{s} = \mathbf{19\text{ giây / buổi}}$$
  $$\text{Số lần phát câu thoại trong 1 buổi 4 ngăn} = 1 + 4 + 1 = \mathbf{6\text{ lần phát / buổi}}$$
* **Tổng tính cho cả ngày (3 – 4 buổi uống thuốc):**
  * Số lần phát câu thoại: **18 – 24 lần phát/ngày**.
  * Tổng thời gian phát loa tích lũy: **$\approx 60\text{ giây} – 90\text{ giây/ngày}$** (nếu người bệnh uống ngay) hoặc **$\approx 2 – 3\text{ phút/ngày}$** (nếu có buổi bận việc phải nhắc lại).
* **Điện năng tiêu hao của loa trong 24h:**
  $$Q_{\text{audio}} = 250\text{mA} \times \left(\frac{90\text{s}}{3600\text{s}}\right)\text{h} \approx \mathbf{6.25\text{ mAh/ngày}}$$
  *(Chủ yếu ăn dòng đỉnh ngắn hạn khi phát câu thoại, còn lại ở chế đ---

### 1.3. Tổng kết công suất hệ thống hiện tại (Khi chưa tối ưu)

1. **Dòng tiêu thụ ở chế độ chờ (Standby Current):**
   $$I_{\text{idle}} = 95\text{mA} + 60\text{mA} + 12\text{mA} + 2\text{mA} \approx \mathbf{170\text{ mA} – 180\text{ mA}}$$
   *(Quy đổi công suất ở nguồn 5V: $P_{\text{idle}} \approx 5\text{V} \times 0.175\text{A} \approx \mathbf{0.875\text{ W} – 0.9\text{ W}}$)*

2. **Dòng cực đại tức thời (Peak Current):**
   Khi động cơ quay đồng thời loa phát giọng nói và Wi-Fi truyền bản tin MQTT:
   $$I_{\text{peak}} = 250\text{mA (ESP32 RF)} + 260\text{mA (Motor)} + 250\text{mA (Loa)} + 60\text{mA (LCD)} \approx \mathbf{820\text{ mA} – 900\text{ mA}}$$

3. **Tổng dung lượng điện tiêu hao trong 24 giờ:**
   $$Q_{24h} \approx 175\text{mA} \times 24\text{h} + Q_{\text{active}} \approx \mathbf{4.250\text{ mAh} – 4.350\text{ mAh/ngày}}$$

4. **Thời lượng hoạt động với các phương án nguồn:**
   - **Dùng 1 cell pin 18650 (dung lượng 2.600 mAh):** Chỉ chạy được $\approx \mathbf{14 – 15\text{ giờ}}$.
   - **Dùng 2 cell pin 18650 song song (dung lượng 5.200 mAh):** Chạy được khoảng $\approx \mathbf{28 – 30\text{ giờ}}$.
   - **Cắm củ sạc nguồn Adapter 5V:** Yêu cầu tối thiểu củ sạc **5V / 2A** (để gánh dòng đỉnh $\approx 1\text{A}$ mà không gây sụt áp làm reset ESP32).

---

## PHẦN 2: BÀI TOÁN "CƠ CHẾ NGỦ TIẾT KIỆM NĂNG LƯỢNG NHƯNG KHÔNG ẢNH HƯỞNG CHỨC NĂNG"

### 2.1. Phân tích ràng buộc kỹ thuật: Tại sao KHÔNG THỂ dùng Deep Sleep truyền thống?
Nếu cho ESP32 vào **Deep Sleep**:
* ❌ Bộ thu phát sóng Wi-Fi bị cắt điện hoàn toàn $\rightarrow$ **Đứt kết nối MQTT Broker**. Khi người dùng trên Web nhấn "Xoay thử khay", "Nạp thuốc", Web sẽ báo thiết bị Offline và lệnh không được thực thi.
* ❌ RAM nội bị xóa (chỉ giữ RTC RAM), khi tỉnh dậy chip phải khởi động lại (Reboot) từ đầu $\rightarrow$ Mất từ 2 – 4 giây để kết nối lại Wi-Fi/MQTT.
* ❌ Trải nghiệm sử dụng bị gián đoạn, không đạt tiêu chuẩn thiết bị Y tế IoT thời gian thực.

### 2.2. Giải pháp: Kiến trúc Quản lý Nguồn Phân tầng (Hierarchical Power Saving)
Thay vì Deep Sleep, chúng ta áp dụng **mô hình ngủ phân tầng 4 cấp độ** được khuyến nghị bởi Espressif cho các thiết bị Smart IoT:

```
┌──────────────────────────────────────────────────────────────────────────────────┐
│              KIẾN TRÚC TIẾT KIỆM ĐIỆN PHÂN TẦNG (SMARTMED POWER SAVING)          │
├──────────────────────────────────────────────────────────────────────────────────┤
│                                                                                  │
│  [CẤP 1: MÀN HÌNH]  Sau 30s không thao tác:                                      │
│                     - Hạ độ sáng đèn nền xuống 15% (Dimming PWM).                │
│                     - Sau 60s: Tắt hẳn đèn nền + đưa ST7789 vào Sleep In (0x10). │
│                     - Tiết kiệm ngay: 50mA – 55mA.                               │
│                                                                                  │
│  [CẤP 2: WI-FI]     Bật Wi-Fi 802.11 Modem-Sleep (DTIM):                         │
│                     - Giữ nguyên kết nối TCP MQTT Broker 24/7 (Không ngắt mạng). │
│                     - Radio RF tự ngủ giữa các chu kỳ Beacon của Router.         │
│                     - Router đệm lệnh từ Web; ESP32 nhận lệnh trễ < 100ms.       │
│                     - Tiết kiệm: Giảm dòng ESP32 từ 95mA xuống còn 18mA – 22mA.  │
│                                                                                  │
│  [CẤP 3: CẢM BIẾN]  Quản lý nguồn TCRT5000 có điều kiện (Gated Power):           │
│                     - Khi đứng yên ở Slot 0 Safe Home: Tắt nguồn cảm biến.       │
│                     - Chỉ bật trước khi động cơ bắt đầu quay dò mốc Home.        │
│                     - Tiết kiệm: 12mA liên tục.                                  │
│                                                                                  │
│  [CẤP 4: THỨC DẬY]  Thức dậy ngay lập tức (Zero Latency) trong các trường hợp:  │
│                     1. Có gói tin MQTT từ Web (Lệnh điều khiển, nạp thuốc).      │
│                     2. Xoay núm EC11 hoặc bấm nút K0 / PUSH (Ngắt GPIO).        │
│                     3. Đến giờ uống thuốc (Timer nội bộ / RTC).                  │
│                                                                                  │
└──────────────────────────────────────────────────────────────────────────────────┘
```

---

## PHẦN 3: KẾ HOẠCH TRIỂN KHAI MÃ NGUỒN CHI TIẾT

### Giai đoạn 1: Tối ưu hóa Màn hình ST7789 (Auto-Dimming & Auto-Sleep)
* **Mục tiêu:** Tự động tắt đèn nền sau khi người dùng không chạm vào máy trong 60 giây.
* **Cách thức:**
  1. Tạo biến theo dõi hoạt động: `unsigned long lastUserActivityMs = millis();`
  2. Bất cứ khi nào có sự kiện: xoay EC11, bấm nút K0, nhận lệnh MQTT từ Web, hoặc đến cữ thuốc $\rightarrow$ Cập nhật `lastUserActivityMs = millis()`.
  3. Trong `display_ui.cpp`:
     - Nếu `(now - lastUserActivityMs > 30000)`: Chuyển độ sáng về mức mờ 20% qua LEDC PWM trên chân `PIN_TFT_BLK`.
     - Nếu `(now - lastUserActivityMs > 60000)`:
       - `digitalWrite(PIN_TFT_BLK, LOW);` (Tắt hẳn đèn nền LED).
       - Gửi lệnh SPI `0x10` (Enter Sleep Mode) xuống IC ST7789 để tắt mạch dao động hiển thị bên trong màn hình.
     - Khi có tương tác lại: Gửi lệnh SPI `0x11` (Sleep Out), bật lại đèn nền `HIGH` trong chưa đầy 10 mili-giây.
* **Hiệu quả:** Giảm ngay **55 mA** dòng chờ liên tục.

---

### Giai đoạn 2: Kích hoạt Wi-Fi Modem-Sleep (802.11 DTIM)
* **Mục tiêu:** Giữ nguyên kết nối MQTT 24/7 với Web nhưng giảm 75% công suất tiêu thụ của ESP32.
* **Cách thức:**
  - Trong [mqtt_manager.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/mqtt_manager.cpp):
    ```cpp
    WiFi.setSleep(WIFI_PS_MIN_MODEM); // hoặc WIFI_PS_MAX_MODEM
    ```
  - **Cơ chế hoạt động:**
    - ESP32 thông báo với Wi-Fi Router rằng nó đang ở chế độ tiết kiệm năng lượng.
    - Wi-Fi Router sẽ lưu tạm thời các gói tin gửi từ Web (MQTT lệnh `cmd/move`, `cmd/refill`...) vào hàng đợi.
    - Cứ mỗi chu kỳ phát Beacon của Router (mỗi 100ms), ESP32 bật bộ thu trong 2ms để kiểm tra xem có tin nhắn gửi tới không. Nếu có tin nhắn, nó thức dậy xử lý ngay lập tức!
  - **Kết quả:** Giao diện Web điều khiển hoàn toàn bình thường, không hề cảm nhận thấy độ trễ (độ trễ tối đa chỉ từ 50ms – 100ms), nhưng dòng tiêu thụ của ESP32 giảm từ **95mA xuống còn ~20mA**.
 
---

### Giai đoạn 3: Điều khiển nguồn Cảm biến quang TCRT5000 (Gated Power)
* **Mục tiêu:** Triệt tiêu dòng tiêu thụ vô ích 12mA của LED hồng ngoại khi máy đang đứng yên.
* **Cách thức:**
  - Chân VCC của cảm biến TCRT5000 được cấp qua 1 chân GPIO (hoặc qua 1 transistor PNP/MOSFET nhỏ đóng cắt).
  - Bình thường khi khay thuốc đang đóng ở Slot 0 (Safe Home), chân này ở mức `LOW` (0mA).
  - Chỉ khi hàm `autoHoming()` hoặc `motorRotateToSlot()` được gọi, firmware mới bật chân này lên mức `HIGH` trước 5ms để cảm biến hoạt động, và ngắt ngay khi hoàn thành lệnh xoay.
* **Hiệu quả:** Tiết kiệm thêm **12 mA** liên tục 24/7.

---

### Giai đoạn 4: Đánh thức hệ thống an toàn (Medical Fail-Safe Wakeup)
Đảm bảo 100% tính năng y tế không bao giờ bị trễ cữ thuốc:
1. **Đánh thức bởi Cữ thuốc:** Hàm `tickSchedule()` chạy dựa trên RTC/millis() nội bộ. Khi phút hiện tại trùng với cữ thuốc $\rightarrow$ Hệ thống lập tức bừng tỉnh: bật màn hình ST7789 sáng rõ, còi loa hú, phát giọng nói hướng dẫn, và đẩy bản tin cảnh báo lên Web.
2. **Đánh thức bởi Nút vật lý:** Núm xoay EC11 và nút K0 sử dụng ngắt phần cứng (`attachInterrupt`). Ngay cái chạm tay hoặc xoay nhẹ đầu tiên của người già $\rightarrow$ Màn hình sáng lên ngay tức thì.
3. **Đánh thức bởi Web:** Khi bác sĩ/người thân nhấn nút "Nạp thuốc ngăn 1" hoặc "Xoay khay" trên Web $\rightarrow$ Gói MQTT TCP đánh thức bộ phận xử lý của ESP32 $\rightarrow$ Bật sáng màn hình và quay động cơ ngay lập tức.

---

## PHẦN 4: SO SÁNH HIỆU QUẢ TRƯỚC VÀ SAU KHI NÂNG CẤP

| Thông số so sánh | Trước khi tối ưu (Hiện tại) | Sau khi kích hoạt Cơ chế ngủ thông minh | Tỷ lệ cải thiện |
|---|:---:|:---:|:---:|
| **Dòng chờ chế độ Standby** | **~175 mA** | **~22 – 25 mA** | **Giảm ~87%** điện năng |
| **Công suất tiêu thụ tĩnh** | ~0.88 W | ~0.11 W | **Tiết kiệm 8 lần** |
| **Điện năng tiêu hao trong 24h** | **~4.300 mAh** | **~550 – 600 mAh** | Tiết kiệm vượt trội |
| **Thời lượng pin 1 cell 18650 (3.000 mAh)** | ~16 giờ | **~4.5 – 5 ngày** | **Tăng gấp 7 lần** |
| **Thời lượng pack pin 2 cell (6.000 mAh)** | ~1.3 ngày | **~10 – 11 ngày** | Đủ tiêu chuẩn thiết bị di động |
| **Khả năng nhận lệnh từ Web** | Tức thì (0ms) | Tức thì (~50ms – 100ms) | **Không ảnh hưởng trải nghiệm** |
| **Độ chính xác cữ thuốc Y tế** | 100% | 100% (Khóa an toàn bằng RTC) | **Giữ nguyên 100% an toàn** | hoạt Wi-Fi Modem-Sleep (802.11 DTIM)
* **Mục tiêu:** Giữ nguyên kết nối MQTT 24/7 với Web nhưng giảm 75% công suất tiêu thụ của ESP32.
* **Cách thức:**
  - Trong [mqtt_manager.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/mqtt_manager.cpp):
    ```cpp
    WiFi.setSleep(WIFI_PS_MIN_MODEM); // hoặc WIFI_PS_MAX_MODEM
    ```
  - **Cơ chế hoạt động:**
    - ESP32 thông báo với Wi-Fi Router rằng nó đang ở chế độ tiết kiệm năng lượng.
    - Wi-Fi Router sẽ lưu tạm thời các gói tin gửi từ Web (MQTT lệnh `cmd/move`, `cmd/refill`...) vào hàng đợi.
    - Cứ mỗi chu kỳ phát Beacon của Router (mỗi 100ms), ESP32 bật bộ thu trong 2ms để kiểm tra xem có tin nhắn gửi tới không. Nếu có tin nhắn, nó thức dậy xử lý ngay lập tức!
  - **Kết quả:** Giao diện Web điều khiển hoàn toàn bình thường, không hề cảm nhận thấy độ trễ (độ trễ tối đa chỉ từ 50ms – 100ms), nhưng dòng tiêu thụ của ESP32 giảm từ **95mA xuống còn ~20mA**.

---

### Giai đoạn 3: Điều khiển nguồn Cảm biến quang TCRT5000 (Gated Power)
* **Mục tiêu:** Triệt tiêu dòng tiêu thụ vô ích 12mA của LED hồng ngoại khi máy đang đứng yên.
* **Cách thức:**
  - Chân VCC của cảm biến TCRT5000 được cấp qua 1 chân GPIO (hoặc qua 1 transistor PNP/MOSFET nhỏ đóng cắt).
  - Bình thường khi khay thuốc đang đóng ở Slot 0 (Safe Home), chân này ở mức `LOW` (0mA).
  - Chỉ khi hàm `autoHoming()` hoặc `motorRotateToSlot()` được gọi, firmware mới bật chân này lên mức `HIGH` trước 5ms để cảm biến hoạt động, và ngắt ngay khi hoàn thành lệnh xoay.
* **Hiệu quả:** Tiết kiệm thêm **12 mA** liên tục 24/7.

---

### Giai đoạn 4: Đánh thức hệ thống an toàn (Medical Fail-Safe Wakeup)
Đảm bảo 100% tính năng y tế không bao giờ bị trễ cữ thuốc:
1. **Đánh thức bởi Cữ thuốc:** Hàm `tickSchedule()` chạy dựa trên RTC/millis() nội bộ. Khi phút hiện tại trùng với cữ thuốc $\rightarrow$ Hệ thống lập tức bừng tỉnh: bật màn hình ST7789 sáng rõ, còi loa hú, phát giọng nói hướng dẫn, và đẩy bản tin cảnh báo lên Web.
2. **Đánh thức bởi Nút vật lý:** Núm xoay EC11 và nút K0 sử dụng ngắt phần cứng (`attachInterrupt`). Ngay cái chạm tay hoặc xoay nhẹ đầu tiên của người già $\rightarrow$ Màn hình sáng lên ngay tức thì.
3. **Đánh thức bởi Web:** Khi bác sĩ/người thân nhấn nút "Nạp thuốc ngăn 1" hoặc "Xoay khay" trên Web $\rightarrow$ Gói MQTT TCP đánh thức bộ phận xử lý của ESP32 $\rightarrow$ Bật sáng màn hình và quay động cơ ngay lập tức.

---

## PHẦN 4: SO SÁNH HIỆU QUẢ TRƯỚC VÀ SAU KHI NÂNG CẤP

| Thông số so sánh | Trước khi tối ưu (Hiện tại) | Sau khi kích hoạt Cơ chế ngủ thông minh | Tỷ lệ cải thiện |
|---|:---:|:---:|:---:|
| **Dòng chờ chế độ Standby** | **~175 mA** | **~22 – 25 mA** | **Giảm ~87%** điện năng |
| **Công suất tiêu thụ tĩnh** | ~0.88 W | ~0.11 W | **Tiết kiệm 8 lần** |
| **Điện năng tiêu hao trong 24h** | **~4.300 mAh** | **~550 – 600 mAh** | Tiết kiệm vượt trội |
| **Thời lượng pin 1 cell 18650 (3.000 mAh)** | ~16 giờ | **~4.5 – 5 ngày** | **Tăng gấp 7 lần** |
| **Thời lượng pack pin 2 cell (6.000 mAh)** | ~1.3 ngày | **~10 – 11 ngày** | Đủ tiêu chuẩn thiết bị di động |
| **Khả năng nhận lệnh từ Web** | Tức thì (0ms) | Tức thì (~50ms – 100ms) | **Không ảnh hưởng trải nghiệm** |
| **Độ chính xác cữ thuốc Y tế** | 100% | 100% (Khóa an toàn bằng RTC) | **Giữ nguyên 100% an toàn** |

---

## PHẦN 5: KẾT LUẬN & ĐỀ XUẤT CẤP NGUỒN

1. **Về phần cứng nguồn:**
   - **Chế độ cắm điện cố định (Khuyên dùng):** Sử dụng củ sạc điện thoại **5V / 2A** cắm cổng USB-C. Cung cấp dòng đỉnh thoải mái cho Loa + Động cơ + Wi-Fi hoạt động đồng thời.
   - **Chế độ chạy Pin sạc dự phòng:** Nếu lắp thêm mạch sạc pin Li-ion 1S (TP4056 hoặc IP5306) kết hợp 1 cell pin 18650 (3000mAh), sau khi áp dụng cơ chế ngủ ở trên, thiết bị hoàn toàn có thể hoạt động liên tục **gần 1 tuần** mà không cần cắm điện!

---
*Tài liệu được biên soạn phục vụ công tác thiết kế phần cứng, định cỡ nguồn và tối ưu hóa firmware cho hệ thống SmartMed 3.4.0.*
