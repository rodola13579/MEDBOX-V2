/**
 * ============================================================================
 * MEDBOX PILL DISPENSER — DHT11 ENVIRONMENT SENSOR IMPLEMENTATION
 * Đọc nhiệt độ và độ ẩm phòng thực tế từ cảm biến DHT11 trên GPIO 41
 * ============================================================================
 */

#include "dht_sensor.h"
#include "audio_player.h"
#include <DHT.h>

// Đối tượng phần cứng DHT11
static DHT dht(PIN_DHT_DATA, DHT11);

// Thời điểm đọc cảm biến gần nhất
static unsigned long lastDhtReadMs = 0;
static bool dhtReady = false;
static unsigned long lastLogMs = 0;
static unsigned long lastTempWarnMs = 0;
static unsigned long lastHumWarnMs = 0;

void dhtSensorInit() {
    Serial.printf("[DHT11] Khoi tao cam bien nhiet do & do am tren GPIO %d...\n", PIN_DHT_DATA);
    pinMode(PIN_DHT_DATA, INPUT_PULLUP);
    dht.begin();
    lastDhtReadMs = millis(); // Cho cảm biến ổn định điện áp 1-2 giây sau khi cấp nguồn
}

bool dhtSensorIsReady() {
    return dhtReady;
}

bool dhtSensorForceRead() {
    float t = dht.readTemperature();
    float h = dht.readHumidity();

    if (isnan(t) || isnan(h) || t < -20.0f || t > 70.0f || h < 0.0f || h > 100.0f) {
        if (millis() - lastLogMs > 10000) {
            lastLogMs = millis();
            Serial.printf("[DHT11 WARN] Chua doc duoc du lieu tu DHT11 tren GPIO %d (Kiem tra day tin hieu & VCC/GND)\n", PIN_DHT_DATA);
        }
        return false;
    }

    // Đọc thành công dữ liệu môi trường thực tế
    bool changed = (abs(t - liveTemp) >= 0.2f || abs(h - liveHum) >= 1.0f || !dhtReady);
    liveTemp = t;
    liveHum  = h;
    dhtReady = true;

    if (changed) {
        homeDirty = true;
        Serial.printf("[DHT11 REALTIME] Nhiet do: %.1f oC | Do am: %.0f %%\n", liveTemp, liveHum);
    }

    // Cảnh báo nhiệt độ & độ ẩm bất thường (Ngưỡng: Temp > 32°C, Hum > 75%)
    // Cooldown 15 phút (900.000 ms), chỉ cảnh báo sau khi thiết bị đã khởi động ổn định 60 giây
    unsigned long now = millis();
    if (now > 60000UL) {
        if (liveTemp > 32.0f && (now - lastTempWarnMs > 900000UL || lastTempWarnMs == 0)) {
            lastTempWarnMs = now;
            Serial.println("[DHT11 WARN] Nhiet do bao quan thuoc vuot nguong (> 32 oC)! Phat am canh bao...");
            audioPlayWarnTempHigh();
        } else if (liveHum > 75.0f && (now - lastHumWarnMs > 900000UL || lastHumWarnMs == 0)) {
            lastHumWarnMs = now;
            Serial.println("[DHT11 WARN] Do am moi truong vuot nguong (> 75 %)! Phat am canh bao...");
            audioPlayWarnHumHigh();
        }
    }

    return true;
}

void dhtSensorUpdate() {
    // DHT11 yêu cầu thời gian lấy mẫu tối thiểu ~2 giây
    if (millis() - lastDhtReadMs >= 2500) {
        lastDhtReadMs = millis();
        dhtSensorForceRead();
    }
}
