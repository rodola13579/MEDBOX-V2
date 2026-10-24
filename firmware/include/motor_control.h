#pragma once

/**
 * ============================================================================
 * MEDBOX PILL DISPENSER — MOTOR & HOMING SUBSYSTEM HEADER
 * Động cơ bước: 28BYJ-48 5V + Driver ULN2003 (IN1..IN4)
 * Cảm biến quang: TCRT5000 (Dò mốc Slot 0 Safe Home)
 * ============================================================================
 */

#include <Arduino.h>
#include "config.h"
#include "app_state.h"

// ----------------------------------------------------------------------------
// CÁC HÀM ĐIỀU KHIỂN ĐỘNG CƠ & CẢM BIẾN
// ----------------------------------------------------------------------------

// Khởi tạo các chân GPIO cho động cơ bước & cảm biến quang
void motorInit();

// Ngắt điện hoàn toàn 4 cuộn dây động cơ (Chống nóng IC ULN2003 & Motor)
void motorDeenergize();

// Phát xung quay tương đối theo số bước (steps > 0: quay tới, steps < 0: quay lui)
void motorStepRelative(int steps, int delayUs = MOTOR_STEP_DELAY_US);

// Xoay khay đến Ngăn chỉ định (0..7) theo đường tròn ngắn nhất
void motorRotateToSlot(int targetSlot);

// Thuật toán 2 giai đoạn tự động dò tìm mốc Ngăn 0 bằng cảm biến TCRT5000
bool autoHoming();

// Kiểm tra trạng thái trực tiếp của cảm biến TCRT5000 (true = Đang ở mốc phản quang)
bool isSensorAtHome();
