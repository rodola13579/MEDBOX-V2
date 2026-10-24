#include <Arduino.h>

// Định nghĩa chân UART nối với module trên ESP32-C3
// Bạn có thể đổi sang cặp GPIO khác (VD: GPIO 4 và 5) nếu chân 20, 21 đang bận
#define RX_PIN 20  // Nối với chân Echo (TX) của AJ-SR04M
#define TX_PIN 21  // Nối với chân Trig (RX) của AJ-SR04M

HardwareSerial mySensor(1); // Sử dụng UART1 của ESP32-C3

void setup() {
  // Khởi tạo Serial Monitor để xem kết quả trên máy tính
  Serial.begin(115200);
  delay(1000);
  
  // Khởi tạo UART giao tiếp với AJ-SR04M ở baudrate mặc định 9600
  mySensor.begin(9600, SERIAL_8N1, RX_PIN, TX_PIN);
  
  Serial.println("--- Bắt đầu test AJ-SR04M / JSN-SR04T (Mode 3 - R19 = 47k) ---");
}

void loop() {
  // 1. Xóa bộ đệm cũ trước khi gửi lệnh mới để tránh đọc dữ liệu tồn đọng
  while (mySensor.available()) {
    mySensor.read();
  }

  // 2. Gửi byte lệnh 0x55 để yêu cầu module đo khoảng cách
  mySensor.write(0x55);
  
  // 3. Chờ module phản hồi đủ 4 byte (tối đa 100ms)
  unsigned long startTime = millis();
  while (mySensor.available() < 4) {
    if (millis() - startTime > 100) {
      Serial.println("Lỗi: Không nhận được phản hồi từ cảm biến (Kiểm tra lại dây RX/TX hoặc mối hàn R19)!");
      delay(500);
      return;
    }
  }

  // 4. Đọc 4 byte dữ liệu trả về
  uint8_t header = mySensor.read();
  if (header == 0xFF) { // Byte đầu tiên luôn là 0xFF
    uint8_t h_data = mySensor.read(); // Byte cao (High byte)
    uint8_t l_data = mySensor.read(); // Byte thấp (Low byte)
    uint8_t sum    = mySensor.read(); // Byte kiểm tra (Checksum)

    // 5. Kiểm tra Checksum: (0xFF + H_DATA + L_DATA) lấy 8 bit thấp
    uint8_t calcSum = (header + h_data + l_data) & 0xFF;

    if (sum == calcSum) {
      // Ghép 2 byte để tính khoảng cách (đơn vị: mm)
      uint16_t distance_mm = (h_data << 8) | l_data;
      float distance_cm = distance_mm / 10.0;

      if (distance_mm == 0) {
        Serial.println("Cảnh báo: Ngoài vùng đo hoặc vật cản quá gần (< 20cm)");
      } else {
        Serial.print("Khoảng cách: ");
        Serial.print(distance_mm);
        Serial.print(" mm (");
        Serial.print(distance_cm, 1);
        Serial.println(" cm)");
      }
    } else {
      Serial.println("Lỗi Checksum: Dữ liệu bị nhiễu!");
    }
  }

  delay(500); // Đợi 500ms trước lần đo tiếp theo
}