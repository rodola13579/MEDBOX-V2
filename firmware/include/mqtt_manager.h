#pragma once

/**
 * ============================================================================
 * SMARTMED PILL DISPENSER — MQTT MANAGER MODULE
 * Nền tảng: ESP32-S3-WROOM-1 N16R8 | Thư viện: PubSubClient + ArduinoJson
 * ============================================================================
 * Quản lý kết nối Wi-Fi & MQTT Client an toàn, tin cậy:
 *   1. Hoàn toàn Non-blocking, không bao giờ gây nghẽn Watchdog WDT.
 *   2. Để Core 0 độc quyền cho phân hệ Loa I2S (không bị gián đoạn âm thanh).
 *   3. Tiếp nhận và xử lý lệnh từ Backend: sync_schedules, dispense, home...
 *   4. Phát sự kiện phần cứng lên Backend: slot_reached, taken, missed,
 * telemetry.
 *   5. Đồng bộ 2 chiều dữ liệu giữa Flash NVS và Database Backend.
 */

#include <Arduino.h>

// Khởi tạo và cập nhật vòng lặp MQTT
void mqttManagerInit();
void mqttManagerUpdate();
bool mqttIsConnected();

// Các hàm phát sự kiện phần cứng lên Backend
void mqttPublishSlotReached(uint8_t slot);
void mqttPublishHomeReached();
void mqttPublishTaken(uint8_t slot, uint8_t dose = 1);
void mqttPublishEarlyTaken(uint8_t h, uint8_t m, uint8_t mask);
void mqttPublishMissed(uint8_t slot);
void mqttPublishDueAlert(uint8_t dueMask);
void mqttPublishScheduleCreated(uint8_t h, uint8_t m, uint8_t mask,
                                uint8_t activeMask = 0xFF);
void mqttPublishAllSchedules();
void mqttPublishAllSlots();
void mqttPublishRefillReady(uint8_t slot);
void mqttPublishRefillDone(uint8_t slot, uint8_t qty, uint8_t added);
void mqttPublishSlotCleared(uint8_t slot);
void mqttPublishError(const char *code, const char *msg);
void mqttPublishTelemetry();
void mqttPublishStatus();
