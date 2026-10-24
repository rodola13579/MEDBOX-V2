#pragma once

#include <Arduino.h>
#include "app_state.h"

// ----------------------------------------------------------------------------
// ĐỊNH NGHĨA CÁC TRẠNG THÁI FSM Y TẾ 2 GIAI ĐOẠN AN TOÀN
// ----------------------------------------------------------------------------
enum FsmState {
    FSM_HOMING_SEARCH = 0, // Khởi động: quay tìm vạch Safe Home Slot 0
    FSM_SAFE_HOME_IDLE = 1, // Đang ở Slot 0, che kín 100%, chờ sự kiện
    FSM_ALERTING_DUE = 2, // Giai đoạn 1: Chuông reo, chờ bệnh nhân bấm xác nhận
    FSM_ROTATING_TO_DOSE = 3, // Đang xoay khay ra ngăn thuốc cần uống
    FSM_WAITING_TAKEN = 4, // Giai đoạn 2: Khay mở tại ngăn, chờ lấy thuốc xong
    FSM_ROTATING_TO_HOME = 5, // Đã lấy xong hoặc timeout: xoay về Slot 0
    FSM_ERROR_JAMMED = 6, // Kẹt cơ / lỗi phần cứng: ngắt điện motor & báo động
    FSM_REFILL_WAITING = 7 // Nạp thuốc: Khay mở tại ngăn chỉ định, chờ nạp xong (Timeout 5 phút)
};

// Khởi tạo FSM
void fsmInit();

// Cập nhật FSM trong vòng lặp chính loop()
void fsmUpdate();

// Truy vấn trạng thái FSM hiện tại
FsmState fsmGetState();
const char* fsmGetStateString();
uint32_t fsmGetRemainingTimeoutSec();
int fsmGetActiveSlot();

// Chuyển đổi trạng thái FSM
void fsmTransitionTo(FsmState newState);

// Các sự kiện kích hoạt từ Người dùng & Lịch trình
void fsmTriggerDueAlert(uint8_t slotMask);          // Đến giờ uống thuốc theo lịch
void fsmOnUserConfirmDose();                         // Bệnh nhân bấm K0 / PUSH khi cữ thuốc reo
void fsmOnUserTakenDone();                           // Bệnh nhân bấm K0 sau khi đã lấy xong thuốc
void fsmTriggerManualDispense(int slot);             // Lấy thuốc sớm theo yêu cầu (Task 3)
void fsmTriggerEarlySession(int schedIndex);         // Lấy thuốc sớm theo buổi (Task 3 mới)
void fsmTriggerRefillStart(int slot);                // Mở ngăn để nạp thuốc (Task 2)
void fsmTriggerRefillDone();                         // Nạp thuốc xong, khóa an toàn về Ngăn 0
void fsmTriggerRefillCancel();                       // Hủy nạp thuốc, khóa an toàn về Ngăn 0 & trở về S_HOME
void fsmTriggerRecalibrate();                        // Yêu cầu Homing lại từ đầu
