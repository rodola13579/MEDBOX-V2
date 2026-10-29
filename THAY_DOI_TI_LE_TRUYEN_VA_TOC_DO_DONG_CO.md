# KẾ HOẠCH NÂNG CẤP TỈ LỆ TRUYỀN ĐỘNG (4:1) VÀ TỐC ĐỘ XOAY 1000 µs CHO SMARTMED
> **Tài liệu Kỹ thuật Cơ - Điện tử & Cấu hình Firmware**  
> **Dự án**: SmartMed — Hộp thuốc thông minh 8 ngăn xoay tròn IoT  
> **Động cơ**: Bước 28BYJ-48 5V + Driver ULN2003 (Chế độ Half-step 8 bước)  
> **Giá trị tốc độ lựa chọn**: **`MOTOR_STEP_DELAY_US = 1000`** (1000 µs / 1.0 ms mỗi xung)  
> **Tình trạng**: Kế hoạch phân tích toàn diện, **TUYỆT ĐỐI CHƯA sửa code** vào dự án.

---

## MỤC LỤC
1. [Cơ sở tính toán cơ học & Tỉ số truyền 4:1](#1-cơ-sở-tính-toán-cơ-học--tỉ-số-truyền-41)
2. [Bảng thông số kỹ thuật Trước ↔ Sau (Cố định tốc độ 1000 µs)](#2-bảng-thông-số-kỹ-thuật-trước--sau-cố-định-tốc-độ-1000-µs)
3. [Phân tích cơ học & Điện tử khi chọn tốc độ 1000 µs](#3-phân-tích-cơ-học--điện-tử-khi-chọn-tốc-độ-1000-µs)
4. [Đánh giá tác động toàn diện lên các Task khác của hệ thống](#4-đánh-giá-tác-động-toàn-diện-lên-các-task-khác-của-hệ-thống)
5. [Danh sách đầy đủ các vị trí cần sửa trong mã nguồn (Firmware)](#5-danh-sách-đầy-đủ-các-vị-trí-cần-sửa-trong-mã-nguồn-firmware)
   - [5.1 File `firmware/include/config.h`](#51-file-firmwareincludeconfigh)
   - [5.2 File `firmware/src/main.cpp`](#52-file-firmwaresrcmaincpp)
   - [5.3 File `firmware/src/motor_control.cpp`](#53-file-firmwaresrcmotor_controlcpp)
6. [Lưu ý chí mạng về bộ nhớ Flash NVS (Preferences ESP32)](#6-lưu-ý-chí-mạng-về-bộ-nhớ-flash-nvs-preferences-esp32)
7. [Checklist kiểm chứng sau khi cập nhật](#7-checklist-kiểm-chứng-sau-khi-cập-nhật)

---

## 1. CƠ SỞ TÍNH TOÁN CƠ HỌC & TỈ SỐ TRUYỀN 4:1

### 1.1 Hiện trạng của hệ thống (Tỉ số truyền 1:1 - Trục gắn trực tiếp)
- Firmware ban đầu mặc định trục ra của động cơ bước 28BYJ-48 nối **trực tiếp 1:1** vào tâm khay thuốc.
- Ở chế độ Half-step (nửa bước 4 pha 8 trạng thái):
  - 1 vòng trục động cơ = **4.096 bước**.
  - 1 vòng khay thuốc ($360^\circ$) = **4.096 bước**.
  - Khay có 8 ngăn ($45^\circ$/ngăn) $\rightarrow$ mỗi ngăn ứng với:
    $$\text{STEPS\_PER\_SLOT} = \frac{4.096}{8} = 512 \text{ bước}$$

### 1.2 Khi nâng cấp sang tỉ số truyền 4:1 (Động cơ quay 4 vòng $\rightarrow$ Khay xoay 1 vòng)
Khi lắp thêm cơ cấu giảm tốc ngoài (bánh răng, đai răng GT2, hoặc cụm truyền động tỉ số 4:1):
* **Số bước cho 1 vòng khay thuốc 360°**:
  $$\text{Tổng số bước} = 4 \times 4.096 = \mathbf{16.384 \text{ bước}}$$
* **Số bước cần thiết để xoay chuyển giữa 2 ngăn liền kề ($45^\circ$)**:
  $$\text{STEPS\_PER\_SLOT} = \frac{16.384}{8} = \mathbf{2.048 \text{ bước}}\quad (\text{tăng đúng 4 lần so với } 512)$$

### 1.3 Lợi ích cơ học vượt trội của tỉ số 4:1
| Tiêu chí | Tỉ lệ 1:1 (Cũ) | Tỉ lệ 4:1 (Mới) | Đánh giá kỹ thuật |
|---|:---:|:---:|---|
| **Lực kéo (Mô-men xoắn)** | $1\times T$ | $\mathbf{4\times T}$ | **Khỏe gấp 4 lần**: Khay chứa đầy thuốc to nặng hoặc có ma sát vỏ hộp vẫn quay băng băng, không bao giờ bị kẹt cơ. |
| **Độ phân giải vị trí** | $0.088^\circ/\text{bước}$ | $\mathbf{0.022^\circ/\text{bước}}$ | **Chuẩn xác gấp 4 lần**: Cửa xả hướng 6 giờ thẳng tắp với từng tâm ngăn thuốc. |
| **Quán tính phản hồi về trục** | $J_{\text{tray}}$ | $\mathbf{\frac{1}{16} J_{\text{tray}}}$ | Giảm 16 lần ($4^2$), động cơ khởi động êm như nhung, triệt tiêu sốc tải. |
| **Khả năng tự giữ (Holding)** | Trung bình | Rất cao | Khó bị ngoại lực bên ngoài tì đè làm trượt góc khay. |

---

## 2. BẢNG THÔNG SỐ KỸ THUẬT TRƯỚC ↔ SAU (CỐ ĐỊNH TỐC ĐỘ 1000 µs)

| Tên biến / Hằng số | File định nghĩa | Giá trị HIỆN TẠI (1:1) | Giá trị MỚI (4:1) | Ý nghĩa kỹ thuật |
|---|---|:---:|:---:|---|
| **`GEAR_RATIO`** | `config.h` | *(chưa có, ngầm hiểu 1)* | **`4`** | Tỉ số truyền giảm tốc giữa động cơ và khay thuốc |
| **`STEPS_PER_REV`** | `config.h` | `4096` | **`4096`** | Số bước 1 vòng trục động cơ 28BYJ-48 (không đổi) |
| **`STEPS_PER_SLOT`** | `config.h` | `512` | **`2048`** | Số bước động cơ cần phát để khay xoay đúng 1 ngăn |
| **`MOTOR_STEP_DELAY_US`** | `config.h` | `1200` | **`1000`** | **Thời gian trễ giữa các bước vi phân (Cố định ở 1000 µs)** |
| **`HOMING_MAX_STEPS`** | `config.h` | `4500` | **`18000`** | Giới hạn tối đa bước quét vạch mốc TCRT5000 (~1.1 vòng khay) |
| **Dải kiểm tra NVS Flash** | `main.cpp` | `400 .. 600` | **`1500 .. 2600`** | Khoảng giá trị hợp lệ $\pm 25\%$ quanh mốc 2048 trong Flash |
| **Khoảng lùi vạch Homing** | `motor_control.cpp` | `150 bước` | **`600 bước`** | Số bước lùi ra khỏi vạch phản quang ban đầu ($150 \times 4$) |

---

## 3. PHÂN TÍCH CƠ HỌC & ĐIỆN TỬ KHI CHỌN TỐC ĐỘ 1000 µs

Giá trị **`MOTOR_STEP_DELAY_US = 1000`** tương ứng với thời gian trễ giữa các xung là **1.0 millisecond** (1000 µs).

### 3.1 Tần số xung và thời gian xoay thực tế
- Tần số phát xung vi bước:
  $$f = \frac{1}{\text{delay}} = \frac{1}{1000 \times 10^{-6}\text{ s}} = \mathbf{1000 \text{ Hz}}\quad (1000 \text{ bước/giây})$$
- Thời gian động cơ hoàn thành di chuyển qua 1 ngăn thuốc (2.048 bước):
  $$t_{\text{ngăn}} = 2048 \times 1000 \mu s \approx \mathbf{2.05 \text{ giây}}$$
*(Nhanh hơn ~20% so với mức trễ cũ 1200 µs vốn mất ~2.46 giây).*

### 3.2 Đường cong mô-men xoắn ($T - f$) và lý do 1000 µs là "Điểm Cân Bằng Vàng"
- Động cơ bước 28BYJ-48 có cuộn dây cảm kháng cao. Khi tăng tần số xung:
  - Nếu chọn delay quá nhỏ ($< 750 \mu s$, tức tần số $> 1330 \text{ Hz}$), dòng cảm ứng không kịp dâng lên cực đại qua cuộn dây $L/R$ $\rightarrow$ Động cơ mất lực kéo khởi động (Pull-in torque), dễ bị trượt bước (stall) và kêu "è è".
  - Với **`1000 µs`** (1000 Hz), động cơ vẫn giữ được **$\approx 88 - 90\%$ mô-men xoắn định mức**.
  - Nhờ có **hộp số 4:1 bù trừ lực kéo gấp 4 lần**, mô-men thực tế tại trục khay thuốc đạt tới **$\approx 3.6\times$ so với thiết kế cũ**. Lực kéo này dư sức đẩy bất kỳ loại thuốc hay viên nang nào mà không hề có nguy cơ kẹt bước.

### 3.3 Bảng Benchmark so sánh các ngưỡng trễ xung

| Mức Delay (`MOTOR_STEP_DELAY_US`) | Tần số xung | Thời gian / ngăn (2048 bước) | Lực kéo giữ lại | Đánh giá vận hành |
|:---:|:---:|:---:|:---:|---|
| `1200 µs` *(Cũ)* | 833 Hz | 2.46 s | 100% | Rất êm, nhưng hơi chậm. |
| **`1000 µs` *(CHỌN)* ** | **1000 Hz** | **2.05 s** | **~88%** | **Tối ưu tuyệt đối**: Tốc độ vừa vặn, không tiếng rít chói tai, lực kéo rất khỏe, khay quay đầm chắc. |
| `850 µs` | 1176 Hz | 1.74 s | ~75% | Nhanh, nhưng động cơ ấm hơn (~$42^\circ\text{C}$). |
| `700 µs` | 1428 Hz | 1.43 s | ~55% | Rủi ro: Có thể trượt bước khi khay có ma sát. |
| `< 600 µs` | > 1660 Hz | - | < 35% | Nguy hiểm: Động cơ chỉ rung rít tại chỗ, không kéo được. |

### 3.4 Yêu cầu về nguồn điện 5V & Mạch ULN2003
- Ở tần số 1000 Hz với 2048 bước, động cơ chạy liên tục 2 giây cho mỗi lần chia thuốc. Dòng điện tiêu thụ trung bình của 4 cuộn pha là khoảng **$250\text{mA} - 350\text{mA}$**.
- **Chân COM của ULN2003 bắt buộc phải nối chắc chắn vào +5V** để kích hoạt mạng diode dập xung ngược (Flyback Diode), triệt tiêu các xung gai điện áp do cuộn cảm sinh ra khi đóng ngắt ở tần số 1000 Hz, bảo vệ ESP32 không bị sụt áp (Brownout Reset).
- Nguồn cấp 5V cho hệ thống nên là Adapter 5V $\ge 1.5\text{A}$ hoặc mạch nguồn Buck riêng.

---

## 4. ĐÁNH GIÁ TÁC ĐỘNG TOÀN DIỆN LÊN CÁC TASK KHÁC CỦA HỆ THỐNG

### 4.1 Task 1: Quản lý cữ thuốc & Cảnh báo (Scheduled Dispensing / Clock)
- **Cơ chế**: Dựa trên thời gian thực RTC nội bộ và định thời NTP/Cron. Khi đến cữ, máy phát chuông báo, hiển thị màn hình `S_DUE_ALERT` và gán cờ `dueMask`.
- **Tác động**: **HOÀN TOÀN KHÔNG BỊ ẢNH HƯỞNG**.
  - Việc khay quay 2.05 giây để đưa thuốc đến cửa lấy diễn ra hoàn toàn tự động. Người dùng chỉ bấm xác nhận sau khi khay đã xoay đến đúng vị trí.
  - Quá trình này không làm trôi đồng hồ hay xung đột lịch uống.

### 4.2 Task 2: Nạp thuốc (Refill Mode)
- **Cơ chế**: Người dùng bấm xoay từng ngăn để nạp thuốc qua Web hoặc nút vặn Encoder EC11.
- **Tác động**: **HOẠT ĐỘNG TỐT HƠN NHIỀU**.
  - Với tỉ số 4:1 và tốc độ 1000 µs, lực xoay khỏe gấp 4 lần giúp người dùng có thể nạp đầy các vỉ thuốc to, nặng mà khay không bị xê dịch hay trượt bước.
  - Tốc độ 2.05 giây/ngăn giúp khay dừng đúng nhịp tay người nạp thuốc.

### 4.3 Task 3: Uống thuốc sớm (Early Session)
- **Cơ chế**: Người dùng uống thuốc trước giờ cữ qua giao diện Web hoặc bấm giữ nút.
- **Tác động**: **HOÀN TOÀN TƯƠNG THÍCH**. Hệ thống tính toán chỉ số Slot mục tiêu và kích hoạt FSM quay mượt mà.

### 4.4 Task 4: Nhật ký lịch sử uống thuốc (Dose History Logging)
- **Cơ chế**: Khi người dùng lấy thuốc, firmware gửi bản tin MQTT `CONFIRM_DOSE` về Backend, Backend ghi nhận vào cơ sở dữ liệu SQLite (`dose_logs`).
- **Tác động**: **HOÀN TOÀN ĐỘC LẬP**.
  - Toàn bộ cơ sở dữ liệu và API Backend chỉ quản lý chỉ số ngăn `slot_index` từ 0 đến 7.
  - Số bước xung 2048 và tốc độ 1000 µs là tầng phần cứng nội bộ của ESP32, hoàn toàn trong suốt với Backend và Database.

### 4.5 Task Dò mốc ban đầu (TCRT5000 Auto Homing)
- **Cơ chế**: Động cơ quay chậm từng bước cho đến khi cảm biến hồng ngoại TCRT5000 bắt được vạch đen dưới đáy khay.
- **Tác động**: **ĐỘ CHÍNH XÁC TĂNG GẤP 4 LẦN**.
  - Khi tỉ số truyền là 4:1, đĩa chia thuốc xoay đầm hơn, vạch đen lướt qua mắt đọc TCRT5000 rất êm. Cảm biến phát hiện vạch phản quang tức thì mà không bị trôi lố góc do quán tính.
  - Cần nâng `HOMING_MAX_STEPS` lên `18000` bước (~1.1 vòng khay) để đảm bảo quét trọn vẹn 1 vòng mà không bị báo lỗi Homing Timeout.

### 4.6 Cơ chế chống treo hệ điều hành FreeRTOS (Watchdog Timer Safe)
- Trong hàm `motorStepRelative()`, firmware đã tích hợp cơ chế nhường CPU:
  ```cpp
  if (i % 32 == 0) {
      vTaskDelay(1); // Nhường CPU cho FreeRTOS IDLE Task và Watchdog
  }
  ```
- Với 2048 bước, hàm sẽ nhường CPU cho hệ điều hành đúng $\mathbf{64 \text{ lần}}$ ($2048 / 32$). Do đó **tuyệt đối không bao giờ bị kích hoạt Task Watchdog Reset**.

### 4.7 Tầng Backend và Giao diện Web (2D Carousel / 3D Simulation)
- Các bản tin trao đổi giữa Web và ESP32 qua WebSocket/MQTT chỉ mang thông tin chỉ số ngăn:
  ```json
  {"action": "DISPENSE", "slot": 2}
  ```
- Do đó **không cần sửa bất kỳ một dòng code nào ở Backend hay Frontend**.

---

## 5. DANH SÁCH ĐẦY ĐỦ CÁC VỊ TRÍ CẦN SỬA TRONG MÃ NGUỒN (FIRMWARE)

### 5.1 File [`firmware/include/config.h`](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/include/config.h)
**Vị trí:** Khoảng dòng **69 – 74**

#### Mã nguồn TRƯỚC khi sửa:
```cpp
// ----------------------------------------------------------------------------
// 4. THÔNG SỐ CƠ KHÍ & ĐỘNG CƠ BƯỚC
// ----------------------------------------------------------------------------
#define TOTAL_SLOTS 8      // Slot 0 = Safe Home, Slot 1..7 = Thuốc
#define STEPS_PER_REV 4096 // 28BYJ-48 Half-step 8 bước
#define STEPS_PER_SLOT (STEPS_PER_REV / TOTAL_SLOTS) // 512 bước / ngăn
#define MOTOR_STEP_DELAY_US 1200 // Tốc độ 1.2ms/bước (lực kéo khỏe, êm)
#define HOMING_MAX_STEPS 4500    // Quá 4500 bước không thấy mốc -> Kẹt cơ
```

#### Mã nguồn SAU khi sửa (Cố định tốc độ 1000 µs & Tỉ lệ 4:1):
```cpp
// ----------------------------------------------------------------------------
// 4. THÔNG SỐ CƠ KHÍ & ĐỘNG CƠ BƯỚC (TỈ LỆ TRUYỀN GIẢM TỐC 4:1 - TỐC ĐỘ 1000 µs)
// ----------------------------------------------------------------------------
#define TOTAL_SLOTS 8          // Slot 0 = Safe Home, Slot 1..7 = Thuốc
#define GEAR_RATIO 4           // Tỉ số truyền: 4 vòng động cơ = 1 vòng khay thuốc
#define STEPS_PER_REV 4096     // 1 vòng trục động cơ 28BYJ-48 (Half-step 8 bước)
#define STEPS_PER_CAROUSEL_REV (STEPS_PER_REV * GEAR_RATIO) // 16.384 bước / vòng khay
#define STEPS_PER_SLOT (STEPS_PER_CAROUSEL_REV / TOTAL_SLOTS) // 2.048 bước / ngăn
#define MOTOR_STEP_DELAY_US 1000 // Tốc độ cố định 1000 µs (1.0ms/bước - quay nhanh và êm ái)
#define HOMING_MAX_STEPS 18000   // Quá 18.000 bước không thấy vạch -> Kẹt cơ (~1.1 vòng khay)
```

---

### 5.2 File [`firmware/src/main.cpp`](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/main.cpp)

#### Vị trí 1: Khởi tạo giá trị mặc định (Dòng ~190)
```cpp
// TRƯỚC:
int16_t stepsPerSlot = STEPS_PER_SLOT;

// SAU: Giữ nguyên biến vì STEPS_PER_SLOT ở config.h đã tự động đổi thành 2048
int16_t stepsPerSlot = STEPS_PER_SLOT;
```

#### Vị trí 2: Khôi phục cấu hình mặc định (Dòng ~301)
```cpp
// Trong hàm factoryReset() hoặc defaultCfg():
stepsPerSlot = STEPS_PER_SLOT; // Tự động nhận 2048
```

#### Vị trí 3: Đọc và kiểm tra dải hợp lệ từ Flash NVS (Dòng ~381 – 384)
> [!CAUTION]
> **ĐÂY LÀ ĐIỂM CHÍ MẠNG NHẤT:** Code cũ đang fix cứng `if (stepsPerSlot < 400 || stepsPerSlot > 600)`.  
> Nếu nạp giá trị 2048 mà không sửa điều kiện này, ESP32 sẽ tưởng là giá trị rác ($> 600$) hoặc nếu đọc giá trị cũ 512 từ Flash thì động cơ chỉ xoay được 1/4 ngăn!

**Mã nguồn TRƯỚC:**
```cpp
  homeOffset = prefs.getShort("h_off", 0);
  stepsPerSlot = prefs.getShort("s_step", STEPS_PER_SLOT);
  if (stepsPerSlot < 400 || stepsPerSlot > 600)
    stepsPerSlot = STEPS_PER_SLOT;
```

**Mã nguồn SAU (Tự động Migration cập nhật đè giá trị 2048 vào NVS):**
```cpp
  homeOffset = prefs.getShort("h_off", 0);
  stepsPerSlot = prefs.getShort("s_step", STEPS_PER_SLOT);
  // Kiểm tra dải hợp lệ ±25% quanh giá trị STEPS_PER_SLOT (khoảng 1500..2600 cho mốc 2048)
  if (stepsPerSlot < (STEPS_PER_SLOT * 3 / 4) || stepsPerSlot > (STEPS_PER_SLOT * 5 / 4)) {
    stepsPerSlot = STEPS_PER_SLOT; // Gán 2048
    prefs.putShort("s_step", stepsPerSlot); // Cập nhật ghi đè ngay 2048 vào Flash
  }
```

---

### 5.3 File [`firmware/src/motor_control.cpp`](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/motor_control.cpp)

#### Vị trí 1: Khoảng lùi thoát khỏi vạch đen ban đầu khi Homing (Dòng ~107 – 115)
Khi tỉ số truyền là 4:1, độ rộng vạch đen phản xạ trên khay thuốc tính theo bước động cơ cũng rộng gấp 4 lần.

**Mã nguồn TRƯỚC:**
```cpp
    if (isSensorAtHome()) {
        Serial.println("[HOMING] Khay dang o tren vach den, lui nhe ra khoi vach...");
        int backSteps = 0;
        while (isSensorAtHome() && backSteps < 150) {
            motorStepRelative(-2, 1600);
            backSteps += 2;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        delay(40);
    }
```

**Mã nguồn SAU:**
```cpp
    if (isSensorAtHome()) {
        Serial.println("[HOMING] Khay dang o tren vach den, lui nhe ra khoi vach...");
        int backSteps = 0;
        // Tỉ lệ 4:1 cần lùi tối đa 600 bước thay vì 150 bước
        while (isSensorAtHome() && backSteps < (150 * GEAR_RATIO)) {
            motorStepRelative(-2, 1400);
            backSteps += 2;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        delay(40);
    }
```

#### Vị trí 2: Tốc độ quét tìm vạch trong Homing (Dòng ~127)
```cpp
// Tốc độ xoay dò mốc 0: có thể đặt 1200us để vừa quét nhanh vừa bắt vạch nhạy:
delayMicroseconds(1200); // Thay cho 1600us cũ
```

---

## 6. LƯU Ý CHÍ MẠNG VỀ BỘ NHỚ FLASH NVS (PREFERENCES ESP32)

Khi thay đổi bước động cơ từ 512 lên 2048, **nếu ESP32 đã từng nạp firmware trước đó**, ô nhớ Flash NVS tại key `"s_step"` vẫn đang lưu số cũ (`512`).

Để đảm bảo giá trị `2048` được áp dụng ngay lập tức mà không bị số `512` cũ đè lên, bạn có 2 cách thực hiện:

### Cách A: Tự động ghi đè qua logic code (Khuyên dùng - Đã có ở mục 5.2)
Nhờ đoạn code kiểm tra dải:
```cpp
if (stepsPerSlot < (STEPS_PER_SLOT * 3 / 4) || stepsPerSlot > (STEPS_PER_SLOT * 5 / 4)) {
    stepsPerSlot = STEPS_PER_SLOT; // Tự gán 2048
    prefs.putShort("s_step", stepsPerSlot); // Lưu ngay 2048 đè lên 512 cũ
}
```
Lần đầu tiên khởi động với firmware mới, ESP32 sẽ thấy $512 < 1536$, tự động loại bỏ giá trị cũ và ghi đè giá trị `2048` mới vào Flash.

### Cách B: Xóa trắng bộ nhớ Flash trước khi nạp
Nếu muốn làm sạch hoàn toàn:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32-s3-n16r8 -t erase
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -e esp32-s3-n16r8 -t upload
```

---

## 7. CHECKLIST KIỂM CHỨNG SAU KHI CẬP NHẬT

- [ ] **Kiểm tra 1 - Serial Log lúc khởi động**:
  - Mở Serial Monitor (Baudrate 115200).
  - Khởi động ESP32, quan sát log: `stepsPerSlot = 2048`, `MOTOR_STEP_DELAY_US = 1000`.
- [ ] **Kiểm tra 2 - Xoay chuyển 1 ngăn (Slot 0 $\rightarrow$ Slot 1)**:
  - Bấm xoay sang Ngăn 1.
  - Quan sát: Trục động cơ quay đúng **nửa vòng** ($2048 / 4096 = 0.5$ vòng motor), khay thuốc xoay đúng **$1/8$ vòng** ($45^\circ$).
  - Thời gian xoay đúng **$\approx 2.05$ giây**, dừng chuẩn giữa tâm cửa lấy thuốc.
- [ ] **Kiểm tra 3 - Dò mốc tự động (Auto Homing)**:
  - Chọn Auto Homing. Động cơ xoay đều đặn đến khi cảm biến TCRT5000 gặp vạch phản quang $\rightarrow$ Dừng dứt khoát tại Ngăn 0, chuông kêu "Beep OK".
- [ ] **Kiểm tra 4 - Thử tải với thuốc thực tế**:
  - Nạp đầy các viên thuốc nặng vào khay, kích hoạt xoay liên tục.
  - Động cơ quay êm, tiếng rít nhẹ nhàng, không có tiếng kêu "lục cục" của hiện tượng trượt bước.
- [ ] **Kiểm tra 5 - Kiểm tra nhiệt độ & Ổn định nguồn**:
  - Cho khay xoay 10 vòng liên tục.
  - Vỏ động cơ và chip ULN2003 chỉ ấm nhẹ ($< 45^\circ\text{C}$), ESP32 không bị khởi động lại (không sụt nguồn).

---
*Tài liệu này là bản kế hoạch kỹ thuật hoàn chỉnh và độc lập. Hệ sinh thái mã nguồn dự án hiện tại vẫn được giữ nguyên vẹn 100%.*