#pragma once

/**
 * ============================================================================
 * SMARTMED PILL DISPENSER — CONFIGURATION HEADER
 * Nền tảng: ESP32-S3-WROOM-1 N16R8 (16MB Flash Octal, 8MB PSRAM Octal)
 * ============================================================================
 * CẢNH BÁO PHẦN CỨNG: CẤM TUYỆT ĐỐI DÙNG GPIO 26..37 (Bus Flash/PSRAM Octal)
 */

#include <Arduino.h>

// ----------------------------------------------------------------------------
// 1. ĐỊNH DANH HỆ THỐNG
// ----------------------------------------------------------------------------
#define DEVICE_ID "ESP32_PILLBOX_01"
#define FIRMWARE_VERSION "3.0.0"
#define HARDWARE_MODEL "SMARTMED_S3_N16R8"

// ----------------------------------------------------------------------------
// 2. SƠ ĐỒ CHÂN NGOẠI VI (HEADER 12 CHÂN JP1: MÀN HÌNH + EC11 + NÚT K0)
// ----------------------------------------------------------------------------
// Màn hình màu ST7789 2.4" SPI (Độ phân giải 240x320)
#define PIN_TFT_MOSI 11 // Chân 4 (SDA) trên JP1
#define PIN_TFT_SCLK 12 // Chân 3 (SCL) trên JP1
#define PIN_TFT_CS 8    // 10      // Chân 7 (CS) trên JP1
#define PIN_TFT_DC 9    // Chân 6 (DC) trên JP1
#define PIN_TFT_RST 10  // 8       // Chân 5 (RES) trên JP1
#define PIN_TFT_BLK                                                            \
  48 // Chân 8 (BLK) trên JP1 -> Nối GPIO 48

// Điều khiển đèn nền màn hình & Chế độ ngủ TFT
#define TFT_BLK_PWM_CH        0        // Kênh LEDC PWM 0
#define TFT_BLK_PWM_FREQ      5000     // Tần số PWM 5kHz (chống chớp nháy)
#define TFT_BLK_PWM_RES       8        // Độ phân giải 8-bit (0..255)
#define TFT_BRIGHTNESS_MAX    255      // Độ sáng 100% khi hoạt động
#define TFT_BRIGHTNESS_OFF    0        // Tắt hẳn đèn nền khi ngủ (0%)
#define TFT_SLEEP_TIMEOUT_MS  60000UL  // 60 giây không tương tác -> Ngủ màn hình
// Núm xoay vô cực EC11 & Nút bấm
#define PIN_EC11_CLK 38 // Chân 9 (A) trên JP1
#define PIN_EC11_DT 39  // Chân 10 (B) trên JP1
#define PIN_EC11_SW 40  // Chân 11 (PUSH) trên JP1
#define PIN_BTN_K0 2    // Chân 12 (KEY0/K0) trên JP1

// ----------------------------------------------------------------------------
// 3. SƠ ĐỒ CHÂN CÁC NGOẠI VI KHÁC
// ----------------------------------------------------------------------------
// Động cơ bước 28BYJ-48 + Driver ULN2003
#define PIN_MOTOR_IN1 7 // 4
#define PIN_MOTOR_IN2 6 // 5
#define PIN_MOTOR_IN3 5 // 6
#define PIN_MOTOR_IN4 4 // 7

// Cảm biến quang TCRT5000 dò mốc Slot 0 (Digital Out)
#define PIN_TCRT_SENSOR 47   // 1       // Chân DO nối GPIO 1
#define TCRT_BLACK_LINE HIGH // VẠCH ĐEN = Mức HIGH (Ngừng khi gặp vạch đen)

// Mạch Loa I2S MAX98357A
#define PIN_I2S_BCLK 16 // 15
#define PIN_I2S_LRC 15  // 16
#define PIN_I2S_DIN 17

// Cảm biến nhiệt độ & độ ẩm DHT11
#define PIN_DHT_DATA 1 // 41

// ----------------------------------------------------------------------------
// 4. THÔNG SỐ CƠ KHÍ & ĐỘNG CƠ BƯỚC
// ----------------------------------------------------------------------------
#define TOTAL_SLOTS 8          // Slot 0 = Safe Home, Slot 1..7 = Thuốc
#define GEAR_RATIO 4           // Tỉ số truyền bánh răng: 4 vòng motor = 1 vòng khay thuốc
#define MOTOR_INTERNAL_STEPS 4096 // 1 vòng trục động cơ 28BYJ-48 (Half-step 8 bước)
#define STEPS_PER_CAROUSEL_REV (MOTOR_INTERNAL_STEPS * GEAR_RATIO) // 16.384 bước / vòng khay
#define STEPS_PER_REV STEPS_PER_CAROUSEL_REV // Tổng số bước cho 1 vòng khay hoàn chỉnh
#define STEPS_PER_SLOT (STEPS_PER_REV / TOTAL_SLOTS) // 2.048 bước / ngăn
#define MOTOR_STEP_DELAY_US 1000 // Tốc độ cố định 1000 µs (1.0ms/bước - nhanh và êm ái)
#define HOMING_MAX_STEPS 18000   // Quá 18.000 bước không thấy mốc -> Kẹt cơ (~1.1 vòng khay)

// ----------------------------------------------------------------------------
// 5. MÀU SẮC GIAO DIỆN DARK MODE Y TẾ (RGB565)
// ----------------------------------------------------------------------------
#define COLOR_BG 0x0842         // Xanh đen đậm (#0b101b)
#define COLOR_CARD_BG 0x18E5    // Khối thẻ xám xanh (#192338)
#define COLOR_BORDER 0x2969     // Viền xám (#2a374f)
#define COLOR_PRIMARY 0x07FF    // Cyan y tế sáng (#00e5ff)
#define COLOR_SUCCESS 0x07E0    // Xanh lá an toàn (#00e676)
#define COLOR_WARNING 0xFD20    // Cam cảnh báo (#ff9100)
#define COLOR_DANGER 0xF800     // Đỏ báo động (#ff1744)
#define COLOR_TEXT_WHITE 0xFFFF // Trắng tinh
#define COLOR_TEXT_MUTED 0x8410 // Xám mờ phụ

// ----------------------------------------------------------------------------
// 6. THÔNG SỐ KẾT NỐI MẠNG & MQTT BROKER
// ----------------------------------------------------------------------------
#define DEFAULT_WIFI_SSID "HA"
#define DEFAULT_WIFI_PASS "ha150574"
#define DEFAULT_MQTT_HOST "192.168.1.174" // IP máy tính chạy Mosquitto Broker
#define DEFAULT_MQTT_PORT 1883
#define MQTT_KEEP_ALIVE_SEC 30
#define TELEMETRY_INTERVAL_MS 10000

// [Đồng bộ thời gian NTP (Múi giờ Việt Nam UTC+7)]
#define NTP_SERVER_PRIMARY "pool.ntp.org"
#define NTP_SERVER_SECONDARY "time.google.com"
#define GMT_OFFSET_SEC (7 * 3600) // UTC+7 (25200s)
#define DAYLIGHT_OFFSET_SEC 0
