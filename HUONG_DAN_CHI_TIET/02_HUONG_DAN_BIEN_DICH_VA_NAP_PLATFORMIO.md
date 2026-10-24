# HƯỚNG DẪN 02: BIÊN DỊCH VÀ NẠP PHẦN MỀM BẰNG PLATFORMIO

> **Mục tiêu tài liệu:** Giúp bạn làm chủ công cụ PlatformIO, hiểu rõ ý nghĩa từng file cấu hình phần cứng, biết cách chạy các lệnh nạp và tự tin xử lý khi gặp lỗi.

---

## 1. CẤU HÌNH PHẦN CỨNG PLATFORMIO

Dự án sử dụng vi điều khiển cao cấp **ESP32-S3-WROOM-1 N16R8** (16MB Flash, 8MB PSRAM Octal). Để PlatformIO nhận đúng phần cứng, có 2 file cấu hình cốt lõi:

### 📄 File 1: `platformio.ini`
File này chứa các chỉ thị biên dịch cho trình biên dịch GCC:

```ini
[env:esp32-s3-n16r8]
platform = espressif32 @ ^6.5.0
board = esp32-s3-devkitc-1
framework = arduino

; 1. Xung nhịp CPU 240MHz tối đa
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L

; 2. Cấu hình bộ nhớ Flash 16MB & PSRAM 8MB Octal SPI (OPI)
board_build.flash_mode = qio
board_build.arduino.memory_type = qio_opi
board_build.flash_size = 16MB
board_upload.flash_size = 16MB

; 3. Chỉ định bảng phân vùng tùy chỉnh & Hệ thống tệp LittleFS
board_build.partitions = partitions_16mb.csv
board_build.filesystem = littlefs

; 4. Compiler Build Flags quan trọng
build_flags = 
    -DBOARD_HAS_PSRAM                ; Kích hoạt 8MB PSRAM ngoài
    -DARDUINO_USB_CDC_ON_BOOT=1      ; Native USB Serial CDC
    -DARDUINO_USB_MODE=1
    -DUSE_FSPI_PORT=1                ; Ép TFT_eSPI dùng FSPI (tránh crash SPI trên S3)
    -DUSER_SETUP_LOADED=1            ; Bật cấu hình chân TFT trong platformio.ini
    -DST7789_DRIVER=1                ; Driver màn hình ST7789 2.4" IPS
    -DTFT_WIDTH=240
    -DTFT_HEIGHT=320
    -DTFT_MOSI=11                    ; Chân 4 (SDA) trên module JP1
    -DTFT_SCLK=12                    ; Chân 3 (SCL) trên module JP1
    -DTFT_CS=10                      ; Chân 7 (CS) trên module JP1
    -DTFT_DC=9                       ; Chân 6 (DC) trên module JP1
    -DTFT_RST=8                      ; Chân 5 (RES) trên module JP1
    -DTFT_MISO=-1                    ; Màn hình ST7789 write-only, không dùng MISO
    -DSPI_FREQUENCY=40000000         ; Tốc độ SPI 40MHz mượt mà 60fps
```

---

### 📄 File 2: Bảng phân vùng bộ nhớ Flash `partitions_16mb.csv`

Flash 16MB (tương đương 16.777.216 bytes) được chia thành các "ngăn kéo" độc lập:

```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x5000,   ; 20KB: Lưu lịch uống thuốc, Wi-Fi
otadata,  data, ota,     0xe000,   0x2000,   ; 8KB: Dữ liệu nạp qua mạng OTA
app0,     app,  ota_0,   0x10000,  0x380000, ; 3.5MB: Chứa mã nguồn Firmware chính
app1,     app,  ota_1,   0x390000, 0x380000, ; 3.5MB: Dự phòng cho cập nhật OTA
littlefs, data, spiffs,  0x710000, 0x8E0000, ; ~8.8MB: Chứa 10 file âm thanh MP3
coredump, data, coredump,0xFF0000, 0x10000,  ; 64KB: Ghi log khi hệ thống bị sập
```
* **Lưu ý đặc biệt:** Tên phân vùng là `littlefs` (offset `0x710000`). Đây chính là nơi lưu các file âm thanh tiếng Việt.

---

## 2. QUY TRÌNH NẠP 2 BƯỚC BẮT BUỘC

Khi dựng mới hoặc nạp lại toàn bộ chip, bạn thực hiện đúng 2 bước sau:

```
┌──────────────────────────────────────────────────────────┐
│  BƯỚC 1: Nạp 10 file âm thanh MP3 vào Flash LittleFS      │
│  (Chỉ cần làm 1 lần, hoặc khi có thay đổi file nhạc)    │
└────────────────────────────┬─────────────────────────────┘
                             │
┌────────────────────────────▼─────────────────────────────┐
│  BƯỚC 2: Biên dịch mã nguồn Firmware & Nạp vào Flash     │
│  (Làm mỗi khi bạn sửa code C/C++)                        │
└──────────────────────────────────────────────────────────┘
```

---

### 🛠️ BƯỚC 1: Nạp phân vùng âm thanh LittleFS (8.8MB)

Cắm cáp USB-C nối máy tính với cổng **USB** của bo mạch ESP32-S3. Mở Terminal PowerShell tại thư mục:
`c:\Users\User\OneDrive - The University of Technology\Desktop\new\firmware\esp32_pillbox`

Chạy lệnh:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t uploadfs -e esp32-s3-n16r8
```

* **Lệnh này làm gì?**
  PlatformIO sẽ quét thư mục `data/audio/`, đóng gói 10 file MP3 (`intro.mp3`, `alert_due.mp3`, `click.mp3`...) thành file nhị phân `littlefs.bin` và ghi thẳng vào địa chỉ `0x00710000` của chip Flash.
* Khi thành công sẽ hiện:
  ```text
  Wrote 9306112 bytes at 0x00710000...
  ========================= [SUCCESS] =========================
  ```

---

### 🛠️ BƯỚC 2: Nạp mã nguồn Firmware và mở Serial Monitor

Chạy lệnh:
```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload -t monitor -e esp32-s3-n16r8
```

* **Lệnh này làm gì?**
  1. Biên dịch toàn bộ file `.cpp` trong thư mục `src/`.
  2. Nạp file mã máy `firmware.bin` vào phân vùng `app0` (`0x10000`).
  3. Tự động kết nối vào cổng COM (115200 baud) để mở cửa sổ Serial Monitor cho bạn xem log trực tiếp.

---

## 3. GIẢI MÃ CẤU TRÚC LỆNH PLATFORMIO

Để bạn hiểu tại sao câu lệnh lại viết như vậy:

```powershell
& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload -t monitor -e esp32-s3-n16r8
│  └───────────────────────────────────────────────┘  │  └───────┘ └───────┘  └─────────────┘
│                     (1)                             │     (2)       (3)          (4)
└─ Ký tự chạy PowerShell                              └─ Chỉ thị Run dự án
```

1. **`"$env:USERPROFILE\...\pio.exe"`**: Đường dẫn tuyệt đối đến file thực thi PlatformIO trong máy bạn (dùng cách này để không bao giờ bị lỗi "pio is not recognized").
2. **`-t upload` (Target 1):** Yêu cầu nạp code vào chip sau khi build xong.
3. **`-t monitor` (Target 2):** Yêu cầu mở màn hình xem log Serial ngay sau khi nạp xong. Bạn có thể ghép nhiều `-t` cùng lúc.
4. **`-e esp32-s3-n16r8` (Environment):** Chọn đúng cấu hình chip khai báo trong `platformio.ini`.

---

## 4. TỔNG HỢP CÁC LỆNH PLATFORMIO THƯỜNG DÙNG

| Mục đích | Câu lệnh PowerShell |
| :--- | :--- |
| **Nạp code + Mở log** | `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload -t monitor -e esp32-s3-n16r8` |
| **Chỉ nạp code (Không mở log)** | `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t upload -e esp32-s3-n16r8` |
| **Chỉ nạp file âm thanh MP3** | `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t uploadfs -e esp32-s3-n16r8` |
| **Chỉ kiểm tra lỗi code (Không cắm chip)** | `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -e esp32-s3-n16r8` |
| **Chỉ mở màn hình log Serial** | `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" device monitor -e esp32-s3-n16r8` |
| **Xóa sạch bản build cũ (Clean)** | `& "$env:USERPROFILE\.platformio\penv\Scripts\pio.exe" run -t clean -e esp32-s3-n16r8` |
| **Thoát khỏi màn hình Monitor** | Nhấn tổ hợp phím **`Ctrl + C`** |

---

## 5. BẢNG TRA CỨU LỖI THƯỜNG GẶP VÀ CÁCH XỬ LÝ (CHEAT SHEET)

### ❌ Lỗi 1: `Guru Meditation Error: StoreProhibited (EXCVADDR: 0x00000010)`
* **Nguyên nhân:** Thư viện màn hình `TFT_eSPI` bị xung đột bus SPI trên ESP32-S3 dẫn đến tính toán sai địa chỉ thanh ghi thành NULL (0x0).
* **Cách khắc phục:** Đảm bảo trong `platformio.ini` có cờ `-DUSE_FSPI_PORT=1` và `-DTFT_MISO=-1`. Tuyệt đối không dùng `-DUSE_HSPI_PORT=1`.

### ❌ Lỗi 2: `LittleFS: partition "spiffs" could not be found ... Error: 261`
* **Nguyên nhân:** Hàm `LittleFS.begin()` mặc định đi tìm phân vùng tên `"spiffs"` nhưng file `partitions_16mb.csv` lại đặt tên là `"littlefs"`.
* **Cách khắc phục:** Trong `audio_player.cpp`, truyền rõ tên phân vùng:
  `LittleFS.begin(false, "/littlefs", 10, "littlefs")`.

### ❌ Lỗi 3: `could not open port 'COM...': PermissionError(13, 'Access is denied.')`
* **Nguyên nhân:** Cổng COM đang bị một chương trình khác chiếm giữ (thường do cửa sổ Serial Monitor cũ chưa tắt).
* **Cách khắc phục:** Vào cửa sổ Terminal đang mở Monitor cũ và nhấn **`Ctrl + C`** để ngắt kết nối trước khi nạp lại.

### ❌ Lỗi 4: `Preferences.cpp: nvs_get_str len fail: slot_X NOT_FOUND`
* **Nguyên nhân:** Đây **KHÔNG PHẢI LỖI**. Đây là thông báo NVS Flash kiểm tra lịch thuốc cũ; do chip mới chưa có lịch cài sẵn nên firmware tự nạp cấu hình mặc định.
