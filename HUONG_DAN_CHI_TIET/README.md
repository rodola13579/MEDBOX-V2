# BỘ TÀI LIỆU HƯỚNG DẪN DỰ ÁN SMARTMED

Thư mục này chứa **3 tài liệu hướng dẫn chi tiết** giúp bạn hiểu sâu bản chất dự án, tự tin code lại từ đầu và vận hành hệ thống một cách nhẹ nhàng nhất:

---

### 📚 Danh mục tài liệu:

| Tài liệu | Nội dung chính |
| :--- | :--- |
| **[01_KIEN_TRUC_VA_HUONG_DAN_CODE_TUNG_FILE.md](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/HUONG_DAN_CHI_TIET/01_KIEN_TRUC_VA_HUONG_DAN_CODE_TUNG_FILE.md)** | • Thứ tự code từng file từ đầu (từ cấu hình $\rightarrow$ Driver $\rightarrow$ Service $\rightarrow$ FSM $\rightarrow$ Main).<br>• Giải thích mục đích của từng file.<br>• Các hàm quan trọng nhất và sơ đồ gọi hàm liên thông (Call Flow Map) khi nhận lệnh từ Web. |
| **[02_HUONG_DAN_BIEN_DICH_VA_NAP_PLATFORMIO.md](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/HUONG_DAN_CHI_TIET/02_HUONG_DAN_BIEN_DICH_VA_NAP_PLATFORMIO.md)** | • Ý nghĩa file `platformio.ini` và bảng phân vùng Flash 16MB `partitions_16mb.csv`.<br>• Quy trình nạp 2 bước chuẩn (nạp LittleFS âm thanh & nạp Firmware).<br>• Giải phẫu từng câu lệnh nạp trên PowerShell.<br>• Bảng tra cứu và khắc phục các lỗi nạp thường gặp. |
| **[03_QUY_TRINH_KHOI_DONG_MOI_KHI_BAT_MAY.md](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/HUONG_DAN_CHI_TIET/03_QUY_TRINH_KHOI_DONG_MOI_KHI_BAT_MAY.md)** | • Bức tranh toàn cảnh 4 thành phần (Mosquitto, Backend, Web, ESP32).<br>• Hướng dẫn cách dùng file 1-Click `Chay_SmartMed.bat` trên Desktop.<br>• Cách cài tự động 100% cùng Windows (Zero thao tác).<br>• Checklist kiểm tra nhanh khi gặp sự cố. |
| **[04_KE_HOACH_THUC_CHIEN_FIRMWARE_TUNG_TAC_VU.md](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/HUONG_DAN_CHI_TIET/04_KE_HOACH_THUC_CHIEN_FIRMWARE_TUNG_TAC_VU.md)** | • **Lộ trình thực chiến 6 giai đoạn tác vụ:** Màn hình $\rightarrow$ Động cơ $\rightarrow$ Ngoại vi $\rightarrow$ Mạng/MQTT $\rightarrow$ Flash/Âm thanh $\rightarrow$ Hợp nhất FSM.<br>• Bảng Checklist theo dõi tiến độ rõ ràng, xong phân hệ nào nạp kiểm thử ngay phân hệ đó. |
| **[DE_XUAT_TOI_UU_LOGIC_FIRMWARE.md](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/DE_XUAT_TOI_UU_LOGIC_FIRMWARE.md)** | • **Đánh giá & Tối ưu hóa chuyên sâu Logic Firmware:** Phân tích 5 điểm nghẽn FSM, màn hình cảnh báo cữ thuốc, an toàn nạp thuốc, hành trình xoay động cơ và xung đột scheduler với Backend. |

---

### 🚀 Tiện ích đi kèm:
* **[Chay_SmartMed.bat](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/Chay_SmartMed.bat)**: Nhấp đúp chuột vào file này để tự động kiểm tra Mosquitto, khởi động Backend và mở Web Dashboard trên trình duyệt.
