# HƯỚNG DẪN 03: QUY TRÌNH KHỞI ĐỘNG HỆ THỐNG MỖI KHI BẬT MÁY TÍNH

> **Mục tiêu tài liệu:** Giúp bạn xóa bỏ hoàn toàn sự rối rắm và mệt mỏi mỗi khi bật máy tính. Chỉ cần nắm rõ quy trình 4 thành phần hoặc sử dụng nút bấm 1-Click tự động hóa.

---

## 1. SƠ ĐỒ HOẠT ĐỘNG TOÀN DIỆN CỦA HỆ THỐNG

Hệ sinh thái SmartMed gồm **4 mắt xích** kết nối chặt chẽ với nhau:

```
┌────────────────────────┐      ┌────────────────────────┐
│  1. MOSQUITTO BROKER   │◄────►│   2. FASTAPI BACKEND   │
│  (Cổng 1883 - Đã ngầm) │      │  (Cổng 8000 - Python)  │
└───────────▲────────────┘      └───────────▲────────────┘
            │                               │
            │ (Wi-Fi MQTT)                  │ (WebSocket / HTTP)
            ▼                               ▼
┌────────────────────────┐      ┌────────────────────────┐
│    3. PHẦN CỨNG ESP32  │      │  4. WEB DASHBOARD      │
│ (Cắm cáp nguồn USB-C)  │      │  (http://localhost:8000│
└────────────────────────┘      └────────────────────────┘
```

---

## 2. THỰC TẾ TRÊN MÁY TÍNH CỦA BẠN (ĐIỀU CẦN BIẾT ĐỂ KHÔNG BỊ RỐI)

Nhiều người nghĩ rằng mỗi lần bật máy tính đều phải gõ lệnh bật cả 4 thứ, nhưng thực tế:

1. **Mosquitto MQTT Broker:** 
   * **ĐÃ TỰ ĐỘNG CHẠY CÙNG WINDOWS!**
   * Bạn **KHÔNG CẦN** gõ lệnh bật nó. Mỗi khi bạn bấm nút nguồn mở máy tính, Windows đã tự động chạy ngầm dịch vụ Mosquitto trên cổng `1883`.
2. **Phần cứng ESP32:** 
   * **CHỈ CẦN CẮM NGUỒN!**
   * Sau khi nạp code xong, mỗi lần bật máy bạn chỉ cần cắm cáp USB vào máy tính (hoặc củ sạc điện thoại), ESP32 sẽ tự kết nối Wi-Fi và tự kết nối vào MQTT Broker.
3. **Thứ duy nhất bạn cần bật là: Backend Python (Web Server).**

---

## 3. CÁCH 1: KHỞI ĐỘNG 1-CLICK BẰNG FILE SHORTCUT (KHUYÊN DÙNG) 🚀

Tôi đã tạo sẵn cho bạn một file kịch bản tự động mang tên **`Chay_SmartMed.bat`**.

### Cách tạo và sử dụng:
1. File này nằm tại:
   `c:\Users\User\OneDrive - The University of Technology\Desktop\new\Chay_SmartMed.bat`
2. **Mỗi lần mở máy tính làm việc:**
   * Bạn chỉ cần **nhấp đúp chuột vào file `Chay_SmartMed.bat`**.
3. **File này sẽ tự động làm 3 việc:**
   * ✅ Kiểm tra xem Mosquitto Broker cổng 1883 đã sẵn sàng chưa.
   * ✅ Khởi động Backend Python FastAPI ngầm.
   * ✅ Tự động mở trình duyệt Chrome/Edge truy cập thẳng vào **`http://localhost:8000`**.

Bạn không cần mở terminal, không cần gõ bất kỳ câu lệnh nào!

---

## 4. CÁCH 2: TỰ ĐỘNG HÓA 100% CÙNG WINDOWS (ZERO THAO TÁC) ⚡

Nếu bạn muốn ngay khi bật máy tính lên, hệ thống tự động chạy sẵn toàn bộ mà bạn không cần nhấp chuột vào bất cứ đâu:

### Các bước thực hiện (chỉ làm 1 lần duy nhất):
1. Bấm tổ hợp phím **`Windows + R`** trên bàn phím để mở hộp thoại Run.
2. Gõ chữ: **`shell:startup`** rồi nhấn **Enter**.
   *(Một thư mục mang tên Startup của Windows sẽ mở ra).*
3. Nhấp chuột phải vào file **`Chay_SmartMed.bat`** $\rightarrow$ chọn **Show more options** $\rightarrow$ **Create shortcut** (Tạo lối tắt).
4. Kéo biểu tượng lối tắt (Shortcut) vừa tạo vào thư mục **Startup**.

👉 **Kể từ giờ:** Mỗi khi bạn mở máy tính, Windows sẽ tự khởi động Backend và tự mở Web Dashboard lên cho bạn. Bạn chỉ việc cắm nguồn ESP32 là xong!

---

## 5. CÁCH 3: QUY TRÌNH BẬT THỦ CÔNG BẰNG TAY (KHI CẦN XEM LOG BACKEND) 🛠️

Nếu bạn muốn tự tay bật từng bước để nhìn thấy log Backend in ra terminal:

### Bước 3.1: Kiểm tra nhanh Mosquitto Broker
Mở PowerShell và chạy:
```powershell
netstat -ano | findstr 1883
```
* Nếu thấy dòng `TCP 0.0.0.0:1883 ... LISTENING` nghĩa là Broker đang chạy tốt.

### Bước 3.2: Bật Backend FastAPI
Mở Terminal và gõ:
```powershell
cd "c:\Users\User\OneDrive - The University of Technology\Desktop\new\backend"
python main.py
```
* Màn hình sẽ hiện:
  ```text
  INFO: Application startup complete.
  INFO: Uvicorn running on http://0.0.0.0:8000
  INFO: MQTT kết nối thành công. Đang lắng nghe events...
  ```

### Bước 3.3: Mở Web Dashboard
Mở trình duyệt Google Chrome hoặc Edge và truy cập:
👉 **`http://localhost:8000`**

### Bước 3.4: Bật nguồn ESP32
Cắm cáp USB vào ESP32. Sau khoảng 3 giây:
* Trên màn hình Web Dashboard: Huy hiệu chuyển sang **Trực tuyến (Xanh lá)**.
* Nhiệt độ và độ ẩm phòng hiển thị trực tiếp.

---

## 6. CHECKLIST KIỂM TRA NHANH KHI CÓ SỰ CỐ

Nếu mở Web Dashboard lên mà thấy báo **Ngoại tuyến (Offline)**, hãy kiểm tra lần lượt 3 điểm sau:

| Kiểm tra | Dấu hiệu lỗi | Cách khắc phục |
| :--- | :--- | :--- |
| **1. Backend** | Không mở được trang `http://localhost:8000` | Chạy lại file `Chay_SmartMed.bat` hoặc lệnh `python main.py` |
| **2. Mosquitto** | Gõ `netstat -ano \| findstr 1883` không thấy gì | Mở PowerShell (Admin) gõ `Start-Service mosquitto` |
| **3. Cáp USB ESP32** | Đèn nguồn trên ESP32 không sáng | Rút cáp cắm lại hoặc đổi cổng USB khác trên máy tính |
