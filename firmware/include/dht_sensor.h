#pragma once

/**
 * ============================================================================
 * MEDBOX PILL DISPENSER — DHT11 ENVIRONMENT SENSOR SUBSYSTEM
 * Cảm biến nhiệt độ & độ ẩm phòng DHT11 trên GPIO 41
 * ============================================================================
 */

#include <Arduino.h>
#include "config.h"
#include "app_state.h"

// Khởi tạo cảm biến DHT11 (bật INPUT_PULLUP, dht.begin)
void dhtSensorInit();

// Cập nhật định kỳ không gây đứng máy (Non-blocking chu kỳ 2.5 giây)
void dhtSensorUpdate();

// Kiểm tra xem đã có dữ liệu đo hợp lệ từ cảm biến hay chưa
bool dhtSensorIsReady();

// Buộc đọc dữ liệu cảm biến ngay lập tức
bool dhtSensorForceRead();
