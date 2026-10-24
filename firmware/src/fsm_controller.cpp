/**
 * ============================================================================
 * MEDBOX PILL DISPENSER — FINITE STATE MACHINE (FSM) CONTROLLER IMPLEMENTATION
 * Phiên bản: 3.4.0 | Nền tảng: ESP32-S3-WROOM-1 N16R8
 * Điều phối toàn diện chu trình y tế 2 giai đoạn an toàn (Offline-First Architecture)
 * Đồng bộ trạng thái chuẩn mực với Backend FastAPI & Web UI Dashboard
 * ============================================================================
 */

#include "fsm_controller.h"
#include "display_ui.h"
#include "mqtt_manager.h"
#include "motor_control.h"
#include "audio_player.h"

// ----------------------------------------------------------------------------
// 1. BIẾN QUẢN LÝ MÁY TRẠNG THÁI
// ----------------------------------------------------------------------------
static FsmState currentState = FSM_HOMING_SEARCH;
static int activeSlot = 0;              // Ngăn thuốc đang thao tác (1..7)
static bool isManualTake = false;        // Uống sớm thủ công (Task 3) hay theo lịch
static int activeSchedIndex = -1;       // Chỉ số cữ lịch đang uống (-1 nếu không theo cữ)
static unsigned long stateEntryMs = 0;  // Thời điểm bắt đầu trạng thái hiện tại
static unsigned long lastVoiceRemMs = 0;// Thời điểm nhắc loa gần nhất (chu kỳ 2 phút)

// Tên chuỗi trạng thái chuẩn hoá đồng bộ với Backend & Web UI
static const char* FSM_STATE_STRINGS[] = {
    "HOMING_SEARCH",
    "SAFE_HOME_IDLE",
    "ALERTING_DUE",
    "ROTATING_TO_DOSE",
    "WAITING_TAKEN",
    "ROTATING_TO_HOME",
    "ERROR_JAMMED",
    "REFILL_WAITING"
};

// ----------------------------------------------------------------------------
// 2. KHỞI TẠO FSM
// ----------------------------------------------------------------------------
void fsmInit() {
    Serial.println("\n[FSM] ==================================================");
    Serial.println("[FSM] Khoi tao Finite State Machine y te 2 giai doan an toan");
    Serial.println("[FSM] ==================================================");
    
    stateEntryMs = millis();
    // Khởi động bằng việc rà tìm mốc Slot 0 Safe Home
    fsmTransitionTo(FSM_HOMING_SEARCH);
}

FsmState fsmGetState() {
    return currentState;
}

const char* fsmGetStateString() {
    if (currentState <= FSM_REFILL_WAITING) {
        return FSM_STATE_STRINGS[currentState];
    }
    return "UNKNOWN";
}

uint32_t fsmGetRemainingTimeoutSec() {
    unsigned long now = millis();
    unsigned long elapsed = (now - stateEntryMs) / 1000;
    if (currentState == FSM_ALERTING_DUE) {
        return (elapsed < 900) ? (900 - elapsed) : 0;
    } else if (currentState == FSM_WAITING_TAKEN || currentState == FSM_REFILL_WAITING) {
        return (elapsed < 300) ? (300 - elapsed) : 0;
    } else if (currentState == FSM_ROTATING_TO_DOSE) {
        return 300; // Mốc 5 phút sẵn sàng trong thời gian motor đang xoay
    }
    return 0;
}

int fsmGetActiveSlot() {
    return activeSlot;
}

// ----------------------------------------------------------------------------
// 3. CHUYỂN ĐỔI TRẠNG THÁI (STATE TRANSITION)
// ----------------------------------------------------------------------------
void fsmTransitionTo(FsmState newState) {
    FsmState oldState = currentState;
    currentState = newState;
    stateEntryMs = millis();

    Serial.printf("[FSM STATE] %s -> %s\n", 
                  FSM_STATE_STRINGS[oldState], 
                  FSM_STATE_STRINGS[newState]);

    switch (newState) {
        case FSM_HOMING_SEARCH: {
            // Đưa khay về mốc Safe Home bằng cảm biến quang TCRT5000
            bool ok = autoHoming();
            if (ok) {
                fsmTransitionTo(FSM_SAFE_HOME_IDLE);
                audioPlayIntro(); // "Medbox đã sẵn sàng hoạt động"
            } else {
                fsmTransitionTo(FSM_ERROR_JAMMED);
                audioPlayAlarmJammed(); // "Cảnh báo! Khay xoay gặp sự cố kẹt cơ..."
            }
            break;
        }

        case FSM_SAFE_HOME_IDLE: {
            homeDirty = true;
            break;
        }

        case FSM_ALERTING_DUE: {
            homeDirty = false;
            goTo(S_DUE_ALERT);
            // Phát giọng nói nhắc nhở cữ thuốc ngay khi đến giờ
            audioPlayAlertDue();
            lastVoiceRemMs = millis();
            break;
        }

        case FSM_ROTATING_TO_DOSE: {
            homeDirty = false;
            goTo(S_DUE_ALERT);
            Serial.printf("[FSM] Xoay khay ra Ngan %d de benh nhan lay thuoc...\n", activeSlot);
            motorRotateToSlot(activeSlot);
            mqttPublishSlotReached(activeSlot);
            fsmTransitionTo(FSM_WAITING_TAKEN);
            break;
        }

        case FSM_WAITING_TAKEN: {
            homeDirty = false;
            goTo(S_DUE_ALERT);
            // Phát giọng nói hướng dẫn mở đích danh ngăn thuốc
            audioPlayDispenseSlot(activeSlot);
            break;
        }

        case FSM_ROTATING_TO_HOME: {
            homeDirty = false;
            // Cập nhật số lượng và nhật ký y tế cho ngăn vừa uống
            if (activeSlot >= 1 && activeSlot <= N_SLOTS) {
                int sIdx = activeSlot - 1;
                consumeDose(sIdx);
                addHist(sIdx, isManualTake ? H_MANUAL : H_TAKEN);
                dueMask &= ~(1 << sIdx);
                mqttPublishTaken(activeSlot, meds[sIdx].dose ? meds[sIdx].dose : 1);
            }
            saveCfg();

            // Kiểm tra xem cữ thuốc này còn ngăn nào khác cần uống không (Uống đa ngăn theo buổi)
            int nextSlot = 0;
            for (int s = 0; s < N_SLOTS; s++) {
                if ((dueMask >> s) & 1) {
                    nextSlot = s + 1;
                    break;
                }
            }

            if (nextSlot > 0) {
                // Vẫn còn ngăn thuốc khác trong cùng mốc giờ / buổi -> Tiếp tục xoay thẳng sang ngăn tiếp theo, KHÔNG quay về 0!
                activeSlot = nextSlot;
                Serial.printf("[FSM] Con Ngan %d trong cung buoi, tiep tuc xoay thang den Ngan %d...\n", activeSlot, activeSlot);
                audioPlayDispenseNext(); // 🔊 "Lấy ngăn tiếp theo..."
                motorRotateToSlot(activeSlot);
                mqttPublishSlotReached(activeSlot);
                fsmTransitionTo(FSM_WAITING_TAKEN);
            } else {
                // Đã uống hết toàn bộ cữ thuốc -> Quay về 0 và khóa an toàn
                Serial.println("[FSM] Benh nhan da lay het thuoc, quay ve Slot 0 Safe Home...");
                motorRotateToSlot(0);
                mqttPublishHomeReached();
                dueMask = 0;
                if (activeSchedIndex >= 0) {
                    markSchedTakenToday(activeSchedIndex);
                    activeSchedIndex = -1;
                }
                audioPlayTakenSuccess(); // "Xác nhận đã lấy thuốc, cảm ơn bạn đã uống đúng giờ"
                fsmTransitionTo(FSM_SAFE_HOME_IDLE);
                if (screen == S_DUE_ALERT) {
                    goTo(S_HOME);
                }
            }
            break;
        }

        case FSM_REFILL_WAITING: {
            homeDirty = true;
            break;
        }

        case FSM_ERROR_JAMMED: {
            motorDeenergize();
            homeDirty = true;
            Serial.println("[FSM ERROR] Khay bi ket co! Yeu cau kiem tra vat ly.");
            audioPlayAlarmJammed();
            mqttPublishError("JAMMED", "Motor stall detected");
            break;
        }
    }
}

// ----------------------------------------------------------------------------
// 4. VÒNG LẶP CẬP NHẬT ĐỊNH KỲ (FSM UPDATE TRÊN CORE 1)
// ----------------------------------------------------------------------------
void fsmUpdate() {
    unsigned long now = millis();

    switch (currentState) {
        case FSM_SAFE_HOME_IDLE: {
            // Khi ở IDLE: tự động kiểm tra cữ thuốc nếu có mốc đến giờ
            if (dueMask != 0) {
                fsmTriggerDueAlert(dueMask);
            }
            break;
        }

        case FSM_ALERTING_DUE: {
            // 1. Nhắc lại bằng giọng nói mỗi 2 phút (120 giây) nếu chưa bấm xác nhận
            if (now - lastVoiceRemMs >= 120000) {
                lastVoiceRemMs = now;
                Serial.println("[FSM] Nhac nho lai cu thuoc qua loa I2S (chu ky 2 phut)...");
                audioPlayAlertRemind();
            }

            // 2. Xử lý quá hạn 15 phút (900 giây) mà bệnh nhân không lấy thuốc -> Bỏ lỡ
            if (now - stateEntryMs >= 900000) {
                Serial.println("[FSM TIMEOUT] Qua 15 phut khong uong thuoc -> Ghi nhan BO LO (MISSED)");
                for (int s = 0; s < N_SLOTS; s++) {
                    if ((dueMask >> s) & 1) {
                        addHist(s, H_MISS);
                        mqttPublishMissed(s + 1);
                    }
                }
                dueMask = 0;
                audioPlayMissedTimeout(); // "Đã hết thời gian chờ lấy thuốc, cữ thuốc được ghi nhận là bỏ lỡ"
                fsmTransitionTo(FSM_SAFE_HOME_IDLE);
                if (screen == S_DUE_ALERT) {
                    goTo(S_HOME);
                }
            }
            break;
        }

        case FSM_WAITING_TAKEN: {
            // Quá 5 phút (300 giây) mở khay mà bệnh nhân quên bấm K0 -> Tự động đóng khay về Slot 0 an toàn
            if (now - stateEntryMs >= 300000) {
                Serial.println("[FSM TIMEOUT] Qua 5 phut mo khay -> Tu dong khoa ve Slot 0 Safe Home");
                fsmTransitionTo(FSM_ROTATING_TO_HOME);
                if (screen == S_DUE_ALERT) {
                    goTo(S_HOME);
                }
            }
            break;
        }

        case FSM_REFILL_WAITING: {
            // Quá 5 phút (300 giây) nạp thuốc mà người dùng quên nhấn Hoàn tất -> Tự động khóa về Slot 0 an toàn
            if (now - stateEntryMs >= 300000) {
                Serial.println("[FSM TIMEOUT] Qua 5 phut nap thuoc -> Tu dong khoa ve Slot 0 Safe Home");
                fsmTriggerRefillDone();
            }
            break;
        }

        default:
            break;
    }
}

// ----------------------------------------------------------------------------
// 5. CÁC HÀM XỬ LÝ SỰ KIỆN TỪ NGƯỜI DÙNG & LỊCH TRÌNH
// ----------------------------------------------------------------------------

// 1. Kích hoạt cữ thuốc đến giờ
void fsmTriggerDueAlert(uint8_t slotMask) {
    if (currentState == FSM_ALERTING_DUE || currentState == FSM_ROTATING_TO_DOSE || currentState == FSM_WAITING_TAKEN) {
        dueMask |= slotMask;
        Serial.printf("[FSM] Bo sung ngan vao cu dang uong: dueMask = 0x%02X\n", dueMask);
        if (screen == S_DUE_ALERT) {
            goTo(S_DUE_ALERT);
        }
        return;
    }
    if (currentState != FSM_SAFE_HOME_IDLE) return;
    dueMask = slotMask;
    isManualTake = false;
    activeSchedIndex = -1;
    for (int i = 0; i < nSched; i++) {
        if ((sched[i].mask & slotMask) != 0) {
            activeSchedIndex = i;
            break;
        }
    }
    mqttPublishDueAlert(dueMask);
    fsmTransitionTo(FSM_ALERTING_DUE);
}

// 2. Bệnh nhân bấm K0 khi cữ thuốc đang báo động -> Mở ngăn đầu tiên
void fsmOnUserConfirmDose() {
    if (currentState != FSM_ALERTING_DUE) return;

    // Tìm ngăn thuốc đầu tiên trong mốc giờ
    int target = 0;
    for (int s = 0; s < N_SLOTS; s++) {
        if ((dueMask >> s) & 1) {
            target = s + 1;
            break;
        }
    }

    if (target > 0) {
        activeSlot = target;
        isManualTake = false;
        fsmTransitionTo(FSM_ROTATING_TO_DOSE);
    } else {
        fsmTransitionTo(FSM_SAFE_HOME_IDLE);
        goTo(S_HOME);
    }
}

// 3. Bệnh nhân bấm K0 sau khi đã lấy xong thuốc -> Đóng khay về Slot 0
void fsmOnUserTakenDone() {
    if (currentState == FSM_WAITING_TAKEN) {
        fsmTransitionTo(FSM_ROTATING_TO_HOME);
    }
}

// 4. Lấy thuốc sớm theo yêu cầu từng ngăn đơn lẻ
void fsmTriggerManualDispense(int slot) {
    if (currentState != FSM_SAFE_HOME_IDLE) {
        Serial.printf("[FSM] Tu choi manual dispense: FSM dang ban hoac loi (State: %s)\n", fsmGetStateString());
        return;
    }
    if (slot < 1 || slot > N_SLOTS) return;
    activeSlot = slot;
    isManualTake = true;
    fsmTransitionTo(FSM_ROTATING_TO_DOSE);
}

// 4.1. Lấy thuốc sớm theo Buổi (Task 3 mới: mở tuần tự các ngăn trong buổi và đánh dấu đã uống)
void fsmTriggerEarlySession(int schedIndex) {
    if (currentState != FSM_SAFE_HOME_IDLE) {
        Serial.printf("[FSM] Tu choi early session: FSM dang ban hoac loi (State: %s)\n", fsmGetStateString());
        return;
    }
    if (schedIndex < 0 || schedIndex >= nSched) return;
    activeSchedIndex = schedIndex;
    dueMask = sched[schedIndex].mask;
    isManualTake = true;

    // Đánh dấu cữ này đã uống hôm nay ngay lập tức
    markSchedTakenToday(schedIndex);

    // Gửi sự kiện MQTT lên Backend
    mqttPublishEarlyTaken(sched[schedIndex].h, sched[schedIndex].m, sched[schedIndex].mask);

    // Tìm ngăn đầu tiên trong buổi
    int firstSlot = 0;
    for (int s = 0; s < N_SLOTS; s++) {
        if ((dueMask >> s) & 1) {
            firstSlot = s + 1;
            break;
        }
    }

    if (firstSlot > 0) {
        activeSlot = firstSlot;
        Serial.printf("[FSM] Uống thuốc sớm Cữ %d (%02d:%02d): Mở Ngăn %d đầu tiên...\n",
                      schedIndex + 1, sched[schedIndex].h, sched[schedIndex].m, activeSlot);
        fsmTransitionTo(FSM_ROTATING_TO_DOSE);
    } else {
        dueMask = 0;
        fsmTransitionTo(FSM_SAFE_HOME_IDLE);
    }
}

// 5. Mở ngăn để nạp thuốc (Task 2)
void fsmTriggerRefillStart(int slot) {
    if (currentState != FSM_SAFE_HOME_IDLE && currentState != FSM_REFILL_WAITING) {
        Serial.printf("[FSM REFILL] Tu choi mo nap: FSM khong o IDLE/REFILL (State: %s)\n", fsmGetStateString());
        return;
    }
    if (slot < 1 || slot > N_SLOTS) return;
    Serial.printf("[FSM REFILL] Mo Ngan %d de nap thuoc...\n", slot);
    fsmTransitionTo(FSM_REFILL_WAITING);
    motorRotateToSlot(slot);
    mqttPublishRefillReady(slot);
    audioPlayRefillSlot(slot); // 🔊 "Đang mở ngăn số X để nạp thuốc..."
    curSlot = slot - 1;
    char msg[36];
    snprintf(msg, sizeof(msg), "Mo Ngan %d nap thuoc", slot);
    toast(msg);
    goTo(S_REFILL_WEB); // Hiển thị chế độ nạp thuốc qua Web (không chuyển sang S_MED)
}

// 6. Nạp thuốc xong, khóa an toàn về Ngăn 0
void fsmTriggerRefillDone() {
    if (currentState != FSM_REFILL_WAITING) {
        Serial.printf("[FSM REFILL] Bo qua lenh refill done (FSM State hien tai: %s)\n", fsmGetStateString());
        return;
    }
    Serial.println("[FSM REFILL] Nap thuoc xong, khoa khay ve Slot 0 Safe Home...");
    motorRotateToSlot(0);
    mqttPublishHomeReached();
    audioPlayRefillDone(); // "Nạp thuốc thành công, khay đã khóa an toàn"
    toast("Da khoa ve Safe Home");
    fsmTransitionTo(FSM_SAFE_HOME_IDLE);
    goTo(S_HOME);
}

// 6b. Hủy nạp thuốc từ Web, khóa khay an toàn về Ngăn 0 và trở về màn hình S_HOME
void fsmTriggerRefillCancel() {
    if (currentState != FSM_REFILL_WAITING) {
        Serial.printf("[FSM REFILL] Bo qua lenh huy nap (FSM State hien tai: %s)\n", fsmGetStateString());
        return;
    }
    Serial.println("[FSM REFILL] Huy nap thuoc -> Khoa khay ve Slot 0 Safe Home & Tro ve S_HOME...");
    motorRotateToSlot(0);
    mqttPublishHomeReached();
    audioPlayRefillCancel(); // 🔊 "Đã hủy nạp thuốc, khay đã khóa về vị trí an toàn."
    toast("Da huy -> Ve Safe Home");
    fsmTransitionTo(FSM_SAFE_HOME_IDLE);
    goTo(S_HOME);
}

// 7. Yêu cầu Homing lại từ đầu (khi bấm Reset lỗi kẹt hoặc màn hình hiệu chuẩn)
void fsmTriggerRecalibrate() {
    fsmTransitionTo(FSM_HOMING_SEARCH);
}
