/**
 * ============================================================================
 * MEDBOX PILL DISPENSER — MOTOR & HOMING SUBSYSTEM IMPLEMENTATION
 * Động cơ bước: 28BYJ-48 5V + Driver ULN2003 (IN1..IN4)
 * Cảm biến quang: TCRT5000 (Dò mốc Slot 0 Safe Home)
 * ============================================================================
 */

#include "motor_control.h"
#include "audio_player.h"

// ----------------------------------------------------------------------------
// 1. CẤU HÌNH PHẦN CỨNG & DÃY XUNG HALF-STEP 8 TRẠNG THÁI
// ----------------------------------------------------------------------------
const int motorPins[4] = {PIN_MOTOR_IN1, PIN_MOTOR_IN2, PIN_MOTOR_IN3, PIN_MOTOR_IN4};

// Dãy xung 8 bước (Half-step) cho lực kéo khỏe, chuyển động êm và độ phân giải 4096 bước/vòng
const uint8_t halfSteps[8] = {
    0x01, // IN1
    0x03, // IN1 + IN2
    0x02, // IN2
    0x06, // IN2 + IN3
    0x04, // IN3
    0x0C, // IN3 + IN4
    0x08, // IN4
    0x09  // IN4 + IN1
};

// ----------------------------------------------------------------------------
// 2. KHỞI TẠO & BẢO VỆ NGẮT ĐIỆN CUỘN DÂY
// ----------------------------------------------------------------------------
void motorInit() {
    for (int i = 0; i < 4; i++) {
        pinMode(motorPins[i], OUTPUT);
        digitalWrite(motorPins[i], LOW);
    }
    pinMode(PIN_TCRT_SENSOR, INPUT_PULLUP);
    motorDeenergize();
}

// Ngắt điện toàn bộ 4 cuộn dây để IC ULN2003 và Động cơ luôn mát lạnh
void motorDeenergize() {
    for (int i = 0; i < 4; i++) {
        digitalWrite(motorPins[i], LOW);
    }
}

bool isSensorAtHome() {
    return (digitalRead(PIN_TCRT_SENSOR) == TCRT_BLACK_LINE);
}

// ----------------------------------------------------------------------------
// 3. PHÁT XUNG QUAY TƯƠNG ĐỐI
// ----------------------------------------------------------------------------
void motorStepRelative(int steps, int delayUs) {
    if (steps == 0) return;

    static int stepIdx = 0;
    int dir = (steps > 0) ? 1 : -1;
    int total = abs(steps);

    for (int s = 0; s < total; s++) {
        stepIdx = (stepIdx + dir + 8) % 8;
        uint8_t pattern = halfSteps[stepIdx];
        for (int p = 0; p < 4; p++) {
            digitalWrite(motorPins[p], (pattern & (1 << p)) ? HIGH : LOW);
        }
        delayMicroseconds(delayUs);
        // Nhường quyền 1 tick cho FreeRTOS mỗi 32 bước (~50ms) để nuôi Watchdog và tránh nghẽn CPU
        if ((s & 0x1F) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    // Luôn ngắt điện ngay sau khi hoàn tất số bước yêu cầu
    motorDeenergize();
}

// ----------------------------------------------------------------------------
// 4. XOAY KHAY THEO ĐƯỜNG NGẮN NHẤT ĐẾN NGĂN CHỈ ĐỊNH (0..7)
// ----------------------------------------------------------------------------
void motorRotateToSlot(int targetSlot) {
    if (targetSlot < 0 || targetSlot >= TOTAL_SLOTS) return;

    int diff = targetSlot - currentMotorSlot;
    // Tối ưu hóa đường tròn: xoay chiều ngắn nhất (-3..+4 ngăn)
    if (diff > 4)  diff -= 8;
    if (diff < -4) diff += 8;

    int steps = diff * stepsPerSlot;
    Serial.printf("[MOTOR] Xoay tu Slot %d -> Slot %d (%+d ngan, %+d buoc)\n", 
                  currentMotorSlot, targetSlot, diff, steps);

    motorStepRelative(steps, MOTOR_STEP_DELAY_US);
    currentMotorSlot = targetSlot;
}

// ----------------------------------------------------------------------------
// 5. THUẬT TOÁN TỰ ĐỘNG DÒ MỐC NGĂN 0: GẶP VẠCH ĐEN LÀ DỪNG NGAY TẠI ĐÓ & TỰ ĐỘNG NHẬN NGĂN 0
// ----------------------------------------------------------------------------
bool autoHoming() {
    Serial.println("[HOMING] Bat dau Auto hieu chuan Ngan 0: Quay den khi gap vach den thi dung...");

    // 1. Neu khay dang de san len vach den, lui nhe ra khoi vach de bat dung mep dau
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

    // 2. Quay toi tung nua buoc (half-step), kiem tra cam bien lien tuc
    int stepsTaken = 0;
    static int stepIdx = 0;

    while (stepsTaken < HOMING_MAX_STEPS) {
        stepIdx = (stepIdx + 1) % 8;
        uint8_t pattern = halfSteps[stepIdx];
        for (int p = 0; p < 4; p++) {
            digitalWrite(motorPins[p], (pattern & (1 << p)) ? HIGH : LOW);
        }
        delayMicroseconds(1600);
        stepsTaken++;

        // Nhường quyền 1 tick cho FreeRTOS mỗi 32 bước để nuôi Watchdog
        if ((stepsTaken & 0x1F) == 0) {
            vTaskDelay(pdMS_TO_TICKS(1));
        }

        // Kiem tra cam bien TCRT5000: GAP VACH DEN
        if (isSensorAtHome()) {
            // Loc nhieu xung dien (debounce 400us)
            delayMicroseconds(400);
            if (isSensorAtHome()) {
                // GAP VACH DEN LA DUNG NGAY LAP TUC!
                motorDeenergize();
                currentMotorSlot = 0;
                homeOffset = 0;
                jogDelta = 0;
                saveCfg(); // TỰ ĐỘNG LƯU VĨNH VIỄN VÀO FLASH NVS
                Serial.printf("[HOMING SUCCESS] Gap vach den tai buoc %d -> DUNG NGAY & TU DONG NHAN NGAN 0!\n", stepsTaken);
                return true;
            }
        }
    }

    // Neu vuot qua so buoc toi da ma van chua gap vach -> Bao dong ket co
    motorDeenergize();
    Serial.println("[HOMING ERROR] Qua 4500 buoc khong thay vach den TCRT5000! Kiem tra cam bien.");
    return false;
}


