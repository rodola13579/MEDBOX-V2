#pragma once

/**
 * ============================================================================
 * MEDBOX PILL DISPENSER — I2S AUDIO SUBSYSTEM HEADER
 * IC giải mã âm thanh: MAX98357A qua I2S (BCLK=GPIO 15, LRC=GPIO 16, DIN=GPIO 17)
 * Bộ nhớ lưu trữ: LittleFS Flash 8.8MB (/data/audio/)
 * Chạy nền trên Core 0 đa nhân (FreeRTOS) không ảnh hưởng tới màn hình và động cơ
 * ============================================================================
 */

#include <Arduino.h>
#include "config.h"

// Mức độ ưu tiên âm thanh (Priority Queue)
enum AudioPriority {
    AUDIO_PRIO_NONE    = 0,
    AUDIO_PRIO_ALARM   = 1, // alarm_jammed (Khẩn cấp cao nhất, ngắt mọi âm thanh)
    AUDIO_PRIO_MEDICAL = 2, // alert_due, missed_timeout (Y tế cao)
    AUDIO_PRIO_INFO    = 3, // taken_success, refill, early_take, dispense (Thông tin)
    AUDIO_PRIO_UI      = 4  // intro, click (Giao diện người dùng, không ngắt lời nói)
};

// Khởi tạo phân hệ âm thanh I2S và gắn kết phân vùng LittleFS
bool audioInit();

// Phát một file MP3 với mức ưu tiên chỉ định
bool audioPlay(const char* filepath, AudioPriority prio = AUDIO_PRIO_INFO);

// Dừng phát âm thanh ngay lập tức (thread-safe giữa các nhân CPU)
void audioStop();

// Kiểm tra xem âm thanh có đang phát hay không
bool audioIsPlaying();

// Cài đặt âm lượng phần mềm (0.0f đến 2.0f, mặc định 0.85f)
void audioSetVolume(float volume);

// Vòng lặp giải mã âm thanh MP3 stream (được gọi tự động bởi FreeRTOS Task trên Core 0)
bool audioLoop();

// ----------------------------------------------------------------------------
// CÁC HÀM TIỆN ÍCH PHÁT GIỌNG NÓI & ÂM BÁO THEO TỪNG CHẾ ĐỘ & TRẠNG THÁI
// ----------------------------------------------------------------------------

// Nhóm 1: Chu trình cữ thuốc & Uống sớm (FSM Y tế)
void audioPlayAlertDue();
void audioPlayAlertRemind();
void audioPlayDispenseSlot(int slot);           // 1..7: "Đang mở ngăn số X..." (Đích danh, không fallback generic)
void audioPlayDispenseNext();                   // "Lấy ngăn tiếp theo..."
void audioPlayTakenSuccess();
void audioPlayMissedTimeout();
void audioPlayEarlyTake();                      // Fallback tương thích cũ
void audioPlayEarlySession(int sessionIdx);     // 0: Sáng, 1: Trưa, 2: Chiều, 3: Tối
void audioPlayEarlyNoDose();

// Nhóm 2: Menu & Nạp / Dọn thuốc theo ngăn
void audioPlayTaskEnter(int taskNum);           // 1: Sched, 2: Refill, 3: Early, 4: Hist
void audioPlaySchedSaved();
void audioPlayRefillSlot(int slot);             // 1..7: "Đang mở ngăn số X để nạp thuốc..."
void audioPlayRefillStart();                    // Fallback tương thích cũ
void audioPlayRefillDone();
void audioPlayRefillCancel();
void audioPlayClearSlot(int slot);              // 1..7: "Đang mở ngăn số X, xin hãy lấy hết thuốc ra ngoài."
void audioPlayClearDone();                      // "Xác nhận xóa, dữ liệu đã được dọn, khay trở về vị trí an toàn."
void audioPlayCalibEnter();

// Nhóm 3: Cảnh báo an toàn y tế & môi trường
void audioPlayWarnLowStock();
void audioPlayWarnEmptyStock();
void audioPlayWarnTempHigh();
void audioPlayWarnHumHigh();
void audioPlayAlarmJammed();

// Nhóm 4: Mạng IoT & Hệ thống
void audioPlayIntro();
void audioPlayWifiConnected();
void audioPlayWifiDisconnected();
void audioPlayResetSuccess();

// Nhóm 5: Hiệu ứng âm thanh giao diện máy móc tự nhiên (Tactile UI)
void audioPlayClick();                          // 18ms micro-mechanical click, zero latency
void audioPlayBeepOk();                         // Soft dual-tone chime
void audioPlayBeepErr();                        // Soft low double-pulse
void audioPlayBeepCancel();                     // Soft downward micro-glide
