# KẾ HOẠCH NÂNG CẤP HIỂN THỊ LỊCH SỬ UỐNG THUỐC (SMARTMED)
## ĐỒNG BỘ WEB DASHBOARD & ESP32 TFT — TRA CỨU THEO TỪNG NGÀY
### CHỈ PHÂN LOẠI 3 TRẠNG THÁI: ĐÚNG CỮ, UỐNG SỚM, BỎ LỠ CỮ (KHÔNG CÓ KẸT KHAY)
### NGUYÊN TẮC: CÔ LẬP HOÀN TOÀN — KHÔNG ẢNH HƯỞNG ĐẾN CÁC TASK KHÁC TRONG DỰ ÁN

---

> **Mã kế hoạch:** `SMARTMED-PLAN-HIST-2026-V3`  
> **Định hướng cốt lõi:**  
> 1. 🎯 **CHỈ HIỂN THỊ & TRA CỨU THEO TỪNG NGÀY (SINGLE-DAY FOCUSED VIEW):**  
>    - Bỏ hoàn toàn các tùy chọn gom dồn nhiều ngày (như "7 ngày", "Tất cả") để bảng gọn gàng, súc tích và dễ theo dõi.  
>    - Mặc định khi mở giao diện: Hiển thị ngay toàn bộ các cữ trong ngày **Hôm nay**.  
>    - Bộ điều hướng ngày thông minh: `[◀ Ngày trước]` `[📅 Chọn ngày bất kỳ]` `[Ngày sau ▶]`, kèm 2 nút tắt `[Hôm nay]` và `[Hôm qua]`.  
> 2. 🏷️ **CHỈ PHÂN LOẠI ĐÚNG 3 TRẠNG THÁI Y TẾ (ĐÃ LOẠI BỎ HOÀN TOÀN HIỂN THỊ KẸT KHAY):**  
>    - 🟢 **Uống đúng cữ (Theo cữ)**  
>    - 🔵 **Uống thuốc sớm (Lấy sớm)**  
>    - 🟡 **Bỏ lỡ cữ (Bỏ lỡ)**  
>    *(Sự cố kẹt khay là cảnh báo phần cứng riêng biệt, thuộc thẩm quyền của Tab Chẩn đoán và Chuông cảnh báo, không đưa vào danh sách kiểm toán lịch sử uống thuốc của bệnh nhân).*  
> **Nguyên tắc an toàn tối cao:** 🛡️ **ZERO REGRESSION** — Tuyệt đối không thay đổi schema cơ sở dữ liệu cốt lõi, không đụng chạm đến cơ chế FSM cấp thuốc tuần tự nhiều ngăn (Task 06), động cơ xoay, 2D tray, bộ điều khiển tác vụ hay các task FSM khác.

---

## 📑 MỤC LỤC
1. [Mục tiêu cốt lõi & Quy tắc 3 trạng thái cữ uống](#1-mục-tiêu-cốt-lõi--quy-tắc-3-trạng-thái-cữ-uống)
2. [Thiết kế hiển thị trên Web Dashboard (Tab 4: Tra cứu theo từng ngày)](#2-thiết-kế-hiển-thị-trên-web-dashboard-tab-4-tra-cứu-theo-từng-ngày)
   - [2.1. Thanh điều hướng Ngày thông minh (Date Navigation Bar)](#21-thanh-điều-hướng-ngày-thông-minh-date-navigation-bar)
   - [2.2. 4 Thẻ KPI tuân thủ y tế của Ngày đang chọn](#22-4-thẻ-kpi-tuân-thủ-y-tế-của-ngày-đang-chọn)
   - [2.3. Bảng dữ liệu y tế (Data Table) phân theo 4 Buổi trong ngày](#23-bảng-dữ-liệu-y-tế-data-table-phân-theo-4-buổi-trong-ngày)
   - [2.4. Chế độ Dòng thời gian trong ngày (Daily Timeline View)](#24-chế-độ-dòng-thời-gian-trong-ngày-daily-timeline-view)
3. [Thiết kế hiển thị trên màn hình TFT 2.4" ESP32 (S_HIST)](#3-thiết-kế-hiển-thị-trên-màn-hình-tft-24-esp32-s_hist)
   - [3.1. Đặc tả hạn chế phần cứng & Quy cách hiển thị 1 dòng](#31-đặc-tả-hạn-chế-phần-cứng--quy-cách-hiển-thị-1-dòng)
   - [3.2. Cú pháp hiển thị và màu sắc ngữ nghĩa y tế RGB565](#32-cú-pháp-hiển-thị-và-màu-sắc-ngữ-nghĩa-y-tế-rgb565)
   - [3.3. Cấu trúc dữ liệu tối ưu trong Flash NVS (struct Hist)](#33-cấu-trúc-dữ-liệu-tối-ưu-trong-flash-nvs-struct-hist)
4. [Thiết kế Backend API & Cơ sở dữ liệu SQLite](#4-thiết-kế-backend-api--cơ-sở-dữ-liệu-sqlite)
   - [4.1. Giữ nguyên Schema bảng dose_logs](#41-giữ-nguyên-schema-bảng-dose_logs)
   - [4.2. Tối ưu hàm truy vấn lọc theo ngày (dose_log_repo.py)](#42-tối-ưu-hàm-truy-vấn-lọc-theo-ngày-dose_log_repopy)
   - [4.3. API Endpoint GET /api/v1/dispense/history?date=YYYY-MM-DD](#43-api-endpoint-get-apiv1dispensehistorydateyyyy-mm-dd)
5. [Ma trận phân tích cam kết: KHÔNG ẢNH HƯỞNG CÁC TASK KHÁC](#5-ma-trận-phân-tích-cam-kết-không-ảnh-hưởng-các-task-khác)
6. [Kế hoạch triển khai từng bước & Kịch bản kiểm thử nghiệm thu](#6-kế-hoạch-triển-khai-từng-bước--kịch-bản-kiểm-thử-nghiệm-thu)

---

## 1. MỤC TIÊU CỐT LÕI & QUY TẮC 3 TRẠNG THÁI CỮ UỐNG

Toàn bộ hệ thống (Web và ESP32) chỉ quản lý và hiển thị đúng **3 trạng thái y tế chuẩn**:

| Trạng thái hiển thị | Mã kỹ thuật (`status` / `trigger_source`) | Điều kiện phát sinh | Màu sắc nhận diện |
| :--- | :--- | :--- | :--- |
| 🟢 **Uống đúng cữ (Theo cữ)** | `status = 'TAKEN'`<br>`trigger_source = 'AUTO_SCHEDULE'` hoặc `'LOCAL_BUTTON'` | Người dùng lấy thuốc khi chuông báo cữ reo đúng lịch hoặc trong cửa sổ cữ hẹn ($\pm 30$ phút). | **Xanh lá cây** (`#10b981` / `0x07E0`) |
| 🔵 **Uống thuốc sớm (Lấy sớm)** | `status = 'EARLY_TAKEN'` hoặc `trigger_source = 'EARLY_MANUAL'` | Người dùng chủ động uống trước giờ hẹn qua tính năng *"Uống thuốc sớm"* (trên Web Console hoặc trên Menu ESP32 "Lấy thuốc theo buổi"). | **Xanh dương / Cyan y tế** (`#0284c7` / `0x07FF`) |
| 🟡 **Bỏ lỡ cữ (Bỏ lỡ)** | `status = 'MISSED'` | Chuông cữ reo hết thời gian chờ (15 phút) mà người bệnh không đến bấm lấy thuốc, hoặc cữ đã qua trong ngày mà chưa được uống. | **Vàng cam / Đỏ cảnh báo** (`#f59e0b` / `0xFD20` hoặc `0xF800`) |

*(Loại bỏ hoàn toàn trạng thái "Kẹt khay" khỏi giao diện lịch sử uống thuốc).*

---

## 2. THIẾT KẾ HIỂN THỊ TRÊN WEB DASHBOARD (TAB 4: TRA CỨU THEO TỪNG NGÀY)

### 2.1. Thanh điều hướng Ngày thông minh (Date Navigation Bar)

Giao diện tập trung hiển thị trọn vẹn dữ liệu của **MỘT ngày được chọn**:

```text
┌────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│  [◀ Ngày trước]    📅 [ Thứ Tư, 28/10/2026 ▼ ]    [Ngày sau ▶]     |  [Hôm nay]  [Hôm qua]   | [📥 Xuất CSV] │
└────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

1. **Bộ chọn ngày trực quan (`<input type="date">`):** Cho phép nhấp chọn bất kỳ ngày nào trong lịch.
2. **Nút tiến / lùi 1 ngày:** 
   - `[◀ Ngày trước]`: Lùi về 1 ngày (ví dụ từ 28/10 về 27/10).
   - `[Ngày sau ▶]`: Tiến tới 1 ngày tiếp theo (tự động khóa vô hiệu hóa nếu đã là ngày hôm nay).
3. **Nút truy cập nhanh 1-Click:**
   - `[Hôm nay]`: Chuyển ngay về ngày hiện tại.
   - `[Hôm qua]`: Chuyển về ngày hôm qua để đối chiếu.
4. **Bộ lọc trạng thái tinh gọn (Chỉ 3 nhóm):** `Tất cả` | `🟢 Theo cữ` | `🔵 Lấy sớm` | `🟡 Bỏ lỡ`.
5. **Nút chuyển đổi chế độ xem:** `[📊 Dạng Bảng]` $\longleftrightarrow$ `[⏱️ Dòng thời gian]`.

---

### 2.2. 4 Thẻ KPI tuân thủ y tế của Ngày đang chọn

Bốn thẻ chỉ số trên cùng phản ánh chính xác kết quả tuân thủ của **duy nhất ngày đang được chọn**:
1. **🎯 Tỷ Lệ Tuân Thủ Trong Ngày (%):** $\frac{\text{Số cữ đã uống (Đúng cữ + Uống sớm)}}{\text{Tổng số cữ có lịch trong ngày}} \times 100\%$.
2. **📦 Tổng Số Cữ Trong Ngày:** Tổng số cữ được lên lịch trong ngày này (ví dụ: 4 cữ).
3. **🟢 Cữ Đã Uống:** Số cữ bệnh nhân đã hoàn tất uống thuốc (Đúng cữ + Lấy sớm).
4. **🟡 Cữ Bỏ Lỡ:** Số cữ bị bỏ lỡ chưa uống trong ngày này.

---

### 2.3. Bảng dữ liệu y tế (Data Table) phân theo 4 Buổi trong ngày

Bảng dữ liệu chỉ hiển thị các cữ của **ngày đang chọn**, được phân nhóm tự nhiên theo 4 Buổi (Sáng, Trưa, Chiều, Tối):

```text
📅 NHẬT KÝ UỐNG THUỐC — THỨ TƯ, 28/10/2026 (Tổng: 4 cữ)

┌────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│ 🌅 BUỔI SÁNG (05:00 – 10:59)                                                                   (2 cữ)  │
├──────────────┬───────────────────────────────┬──────────────────────────┬──────────────┬───────────────┤
│ ⏰ Giờ Hẹn   │ 💊 Ngăn & Tên Thuốc           │ ⏱️ Thời Gian Lấy Thực Tế │ 🏷️ Trạng Thái │ 🎯 Vị Trí Khay│
├──────────────┼───────────────────────────────┼──────────────────────────┼──────────────┼───────────────┤
│ 08:00 (#24)  │ [Ngăn 1] Paracetamol (1 viên) │ 08:02:15 (Đúng giờ +2p)  │ 🟢 Theo cữ   │ [Xem trên 2D] │
│ 08:00 (#25)  │ [Ngăn 2] Vitamin C (1 viên)   │ 07:15:30 (Sớm 45 phút)   │ 🔵 Lấy sớm   │ [Xem trên 2D] │
├────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ ☀️ BUỔI TRƯA (11:00 – 13:59)                                                                   (1 cữ)  │
├──────────────┼───────────────────────────────┼──────────────────────────┼──────────────┼───────────────┤
│ 12:30 (#26)  │ [Ngăn 3] Men vi sinh (2 viên) │ Không lấy (Hết 15p chờ)  │ 🟡 Bỏ lỡ     │ [Xem trên 2D] │
├────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│ 🌙 BUỔI TỐI (18:00 – 22:59)                                                                    (1 cữ)  │
├──────────────┼───────────────────────────────┼──────────────────────────┼──────────────┼───────────────┤
│ 20:00 (#27)  │ [Ngăn 4] Amlodipine (1 viên)  │ 20:00:10 (Đúng giờ)      │ 🟢 Theo cữ   │ [Xem trên 2D] │
└──────────────┴───────────────────────────────┴──────────────────────────┴──────────────┴───────────────┘
```

#### Trình tự thông tin trên mỗi dòng cữ:
1. **⏰ Giờ Hẹn:** Giờ theo lịch cữ (`08:00`, `12:30`, `20:00`) kèm mã ID bản ghi (`#24`).
2. **💊 Ngăn & Tên Thuốc:** Huy hiệu màu nhận diện từng ngăn (`Ngăn 1..7`) + Tên thuốc + Liều dùng (số viên).
3. **⏱️ Thời Gian Lấy Thực Tế:** Giờ thực tế người dùng bấm nút xác nhận, kèm chú thích độ lệch:
   - *Đúng giờ* ($\pm 5$ phút)
   - *Sớm X phút* (nếu lấy thuốc sớm)
   - *Trễ Y phút* (nếu lấy thuốc muộn)
   - *Không lấy* (nếu bỏ lỡ)
4. **🏷️ Trạng Thái Cữ (Chỉ 3 loại):** `🟢 Theo cữ` | `🔵 Lấy sớm` | `🟡 Bỏ lỡ`.
5. **🎯 Vị Trí Khay:** Nút liên kết chuyển nhanh sang Tab 1 để xoay mô hình 2D về đúng ngăn đó.

---

### 2.4. Chế độ Dòng thời gian trong ngày (Daily Timeline View)
- Khi người dùng bấm chuyển sang chế độ **Dòng thời gian**, toàn bộ các cữ trong ngày được sắp xếp theo trục thời gian dọc từ buổi sáng đến buổi tối.
- Mỗi cữ là 1 Card thông tin trực quan chỉ với 3 trạng thái rõ ràng, rất tiện xem trên điện thoại hoặc máy tính bảng.

---

## 3. THIẾT KẾ HIỂN THỊ TRÊN MÀN HÌNH TFT 2.4" ESP32 (S_HIST)

### 3.1. Đặc tả hạn chế phần cứng & Quy cách hiển thị 1 dòng
- Màn hình màu ST7789 độ phân giải `240 x 320 px`.
- Mỗi dòng hiển thị vừa vặn **24 ký tự** (TextSize = 1, Font 2 phần cứng, không bị tràn màn hình hay vỡ font).
- Núm xoay EC11: Xoay để cuộn xem các cữ; Nút K0: Thoát về Menu.

### 3.2. Cú pháp hiển thị và màu sắc ngữ nghĩa y tế RGB565

Mỗi dòng hiển thị súc tích 1 cữ theo đúng cú pháp yêu cầu (chỉ gồm 3 trạng thái):
```text
[Thứ] [Mốc lịch buổi] Ngan [Số] [Hành động & Giờ thực tế]
```

#### 3 trường hợp hiển thị cụ thể trên TFT:

1. **Uống đúng cữ:**
   - Cú pháp: `T2 Sang(08:00) Ng1 Dung cu 08:02`
   - Màu chữ: **Xanh lá cây** `C_OK (0x07E0)`
   - Ý nghĩa: Thứ Hai, cữ Sáng 08:00, Ngăn 1, Đã uống đúng cữ lúc 08:02.

2. **Uống thuốc sớm:**
   - Cú pháp: `T2 Sang(08:00) Ng2 Lay som 07:15`
   - Màu chữ: **Xanh Cyan y tế** `C_ACC (0x07FF)`
   - Ý nghĩa: Thứ Hai, cữ Sáng 08:00, Ngăn 2, Đã lấy sớm lúc 07:15.

3. **Bỏ lỡ cữ:**
   - Cú pháp: `T3 Trua(11:30) Ng3 Bo lo`
   - Màu chữ: **Đỏ báo động** `C_RED (0xF800)`
   - Ý nghĩa: Thứ Ba, cữ Trưa 11:30, Ngăn 3, Đã bỏ lỡ không uống.

---

### 3.3. Cấu trúc dữ liệu tối ưu trong Flash NVS (`struct Hist`)

```cpp
// Cấu trúc lịch sử mở rộng tối ưu (chỉ chiếm đúng 7 bytes / cữ):
struct Hist {
  uint8_t day;       // Thứ trong tuần (0: T2 .. 6: CN)
  uint16_t min;      // Phút thực tế khi thao tác (0..1439 = hour * 60 + minute)
  uint8_t slot;      // Ngăn thuốc (0..6 tương ứng Ngăn 1..7)
  uint8_t type;      // 0: H_TAKEN (Đúng cữ), 1: H_MISS (Bỏ lỡ), 2: H_MANUAL (Uống sớm)
  uint8_t session;   // 0: Sáng, 1: Trưa, 2: Chiều, 3: Tối (0xFF nếu không có)
  uint8_t sched_h;   // Giờ cữ hẹn mục tiêu (0xFF nếu không có)
  uint8_t sched_m;   // Phút cữ hẹn mục tiêu (0xFF nếu không có)
};
```
* **Dung lượng lưu trữ:** Lưu tối đa 20 cữ gần nhất $\times$ 7 bytes $\approx$ **140 bytes** trong Flash NVS.
* Tuyệt đối không tốn RAM và bảo đảm ESP32 chạy ổn định 24/7.

---

## 4. THIẾT KẾ BACKEND API & CƠ SỞ DỮ LIỆU SQLITE

### 4.1. Giữ nguyên Schema bảng `dose_logs`
Bảng `dose_logs` trong `backend/database/schema.sql` được **giữ nguyên 100%**:
- Không sửa tên cột, không xóa cột, không đổi khóa ngoại.
- Dữ liệu cữ uống được ghi nhận thông qua:
  - `status`: `TAKEN`, `EARLY_TAKEN`, `MISSED`.
  - `trigger_source`: `AUTO_SCHEDULE`, `EARLY_MANUAL`, `LOCAL_BUTTON`.
  - `scheduled_time`: Mốc giờ dự kiến của cữ.
  - `confirmed_time`: Thời điểm thực tế người dùng bấm xác nhận.

---

### 4.2. Tối ưu hàm truy vấn lọc theo ngày (`dose_log_repo.py`)
Hàm `get_history()` chỉ cần bổ sung tham số `date_filter`:
```python
async def get_history(
    db: aiosqlite.Connection,
    device_id: str,
    date_filter: str | None = None,  # Định dạng 'YYYY-MM-DD'
    status_filter: str | None = None,
    limit: int = 50,
) -> list[dict]:
    """
    Truy vấn lịch sử cữ uống theo ngày đã chọn.
    Tự động LEFT JOIN với bảng slots và medicines để trả về tên thuốc và liều lượng.
    """
```
- Khi `date_filter` được truyền vào (ví dụ `2026-10-28`):
  Câu lệnh SQL thêm điều kiện lọc:
  ```sql
  AND (DATE(dl.scheduled_time) = :date OR DATE(dl.confirmed_time) = :date OR DATE(dl.created_at) = :date)
  ```
- **Lợi ích:** Chỉ truy vấn đúng các cữ của ngày đó, dữ liệu trả về cực kỳ gọn nhẹ (chỉ khoảng 3–8 bản ghi/ngày), tốc độ tải trang gần như tức thời (< 10ms).

---

### 4.3. API Endpoint `GET /api/v1/dispense/history?date=YYYY-MM-DD`
- Endpoint nhận tham số `date: str | None = None`.
- Nếu Frontend không truyền `date`, mặc định Backend sẽ lấy ngày hiện tại (Hôm nay).

---

## 5. MA TRẬN PHÂN TÍCH CAM KẾT: KHÔNG ẢNH HƯỞNG CÁC TASK KHÁC

| Task hiện hữu trong dự án | Nội dung nghiệp vụ | Mức độ tác động | Lý do an toàn tuyệt đối |
| :--- | :--- | :---: | :--- |
| **Task 06: Dispensing Multi-slot Queue** | Logic cấp thuốc liên tiếp nhiều ngăn trong 1 cữ vừa được sửa | 🟢 **0% (Không ảnh hưởng)** | Task 06 chỉ điều khiển motor xoay lần lượt các ngăn. Lịch sử chỉ **đọc dữ liệu sau khi cữ kết thúc**, không can thiệp vào quá trình xoay khay hay chuyển tiếp ngăn. |
| **Task 07: WebSocket Hub** | Phát sóng thời gian thực cho Web clients | 🟢 **0% (Không ảnh hưởng)** | Lắng nghe event `WS_TAKEN` để tự động làm mới bảng lịch sử của ngày hôm nay nếu người dùng đang mở xem ngày hôm nay. |
| **Task 08: Device Control & Diagnostics** | Bảng chẩn đoán phần cứng, test motor | 🟢 **0% (Không ảnh hưởng)** | Nằm ở Tab 5 hoàn toàn độc lập. Cảnh báo kẹt khay nằm riêng tại đây. |
| **Task 09: Scheduler & Watchdog 15 Phút** | Quét cữ tự động và đếm ngược chuông 15p `ALERTING_DUE` | 🟢 **0% (Không ảnh hưởng)** | Khi hết 15 phút, watchdog ghi nhận `status = 'MISSED'`. Tab Lịch sử chỉ đọc trạng thái này để hiển thị nhãn "Bỏ lỡ". |
| **Task 11: Quản lý Thuốc & Lịch 4 Buổi** | Cài đặt lịch uống theo 4 Buổi | 🟢 **0% (Không ảnh hưởng)** | Nằm ở Tab 2. Logic phân 4 Buổi (Sáng/Trưa/Chiều/Tối) được tái sử dụng đồng nhất. |
| **Task 12: Khay Xoay 2D Hướng 6 Giờ** | Mô hình đồ họa 2D khay thuốc tại trang Tổng quan | 🟢 **0% (Không ảnh hưởng)** | Nằm ở Tab 1. Xem lịch sử không tác động đến vị trí hay trạng thái của mô hình 2D. |
| **Task 14/15: Firmware FSM & Ngoại vi** | Máy trạng thái FSM (Motor, TCRT5000, Loa I2S, Nút bấm) | 🟢 **0% (Không ảnh hưởng)** | Chỉ chỉnh sửa màn hình con `S_HIST` (`drawHistRow`) và hàm lưu `addHist()`. Toàn bộ FSM vận hành, điều khiển động cơ và chuông báo giữ nguyên 100%. |

---

## 6. KẾ HOẠCH TRIỂN KHAI TỪNG BƯỚC & KỊCH BẢN KIỂM THỬ NGHIỆM THU

### Bước 1: Backend & Database Query (Khoảng 15 phút)
1. Cập nhật `backend/schemas/dispense.py`: Thêm `medicine_name`, `medicine_dosage_unit`, `schedule_time`, `schedule_label` vào `DoseLogResponse`.
2. Cập nhật `backend/database/repositories/dose_log_repo.py`: Viết câu lệnh `SELECT` có `LEFT JOIN` và hỗ trợ lọc theo tham số `date_filter`.
3. Cập nhật `backend/api/dispensing.py`: Tiếp nhận `date: Annotated[str | None, Query(...)]`.
4. Chạy `pytest backend/tests/audit_test.py` xác nhận 100% pass.

### Bước 2: Web Frontend (Khoảng 20 phút)
1. Cập nhật `frontend/index.html`: Xây dựng thanh điều hướng ngày `[◀] [📅 Chọn ngày] [▶]` và hai nút tắt `[Hôm nay]`, `[Hôm qua]`. Bộ lọc chỉ gồm 3 chip: `Tất cả`, `🟢 Theo cữ`, `🔵 Lấy sớm`, `🟡 Bỏ lỡ`.
2. Cập nhật `frontend/css/style.css`: Thêm kiểu dáng thanh điều hướng ngày, badge Theo cữ / Lấy sớm / Bỏ lỡ, và phân nhóm 4 Buổi trong ngày.
3. Cập nhật `frontend/js/app.js`:
   - Quản lý `currentSelectedDate` (mặc định là `today`).
   - Hàm `loadHistoryData(date)` gọi API lấy cữ của đúng ngày đó.
   - Gom cữ theo 4 Buổi và hiển thị đầy đủ 4 cột thông tin.
   - Tính toán 4 chỉ số KPI cho riêng ngày được chọn.

### Bước 3: Firmware ESP32 TFT (Khoảng 15 phút)
1. Cập nhật `firmware/include/app_state.h`: Mở rộng `struct Hist` (thêm `session`, `sched_h`, `sched_m`).
2. Cập nhật `firmware/src/main.cpp`: Lưu thông tin buổi và giờ hẹn khi gọi `addHist()`.
3. Cập nhật `firmware/src/display_ui.cpp`: Hàm `drawHistRow()` định dạng chỉ 3 trạng thái:
   `[Thứ] [Buổi(Giờ hẹn)] Ngan [Số] [Đúng cữ HH:MM / Lấy sớm HH:MM / Bỏ lỡ]`.

### Bước 4: Kiểm thử nghiệm thu (Checklist)
- [ ] Chọn ngày Hôm nay $\to$ Hiển thị chính xác các cữ phát sinh trong ngày hôm nay.
- [ ] Bấm `[◀ Ngày trước]` $\to$ Bảng chuyển sang hiển thị đúng các cữ của ngày hôm qua.
- [ ] Bấm `[Ngày sau ▶]` $\to$ Bảng quay trở lại ngày hôm nay.
- [ ] Kiểm thử hiển thị cữ Uống đúng giờ $\to$ Nhãn `🟢 Theo cữ`, TFT hiển thị `Dung cu`.
- [ ] Kiểm thử hiển thị cữ Uống sớm $\to$ Nhãn `🔵 Lấy sớm`, TFT hiển thị `Lay som`.
- [ ] Kiểm thử hiển thị cữ Bỏ lỡ $\to$ Nhãn `🟡 Bỏ lỡ`, TFT hiển thị `Bo lo`.
- [ ] Xác nhận không còn nhãn, chip lọc hay dòng trạng thái "Kẹt khay" nào trong Tab Lịch sử.
- [ ] Kiểm tra các tab khác: Khay 2D xoay bình thường, tính năng cấp thuốc nhiều ngăn (Multi-slot) chạy trơn tru không lỗi.

---

> 📌 **Trạng thái tài liệu:** Kế hoạch đã được hoàn thiện đúng theo 2 tiêu chí của bạn: **CHỈ TRA THEO TỪNG NGÀY** và **BỎ HOÀN TOÀN HIỂN THỊ KẸT KHAY**.
