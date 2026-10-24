/**
 * ============================================================================
 * MEDBOX PILL DISPENSER — I2S AUDIO SUBSYSTEM IMPLEMENTATION
 * IC giải mã âm thanh: MAX98357A qua I2S (BCLK=GPIO 15, LRC=GPIO 16, DIN=GPIO 17)
 * Thư viện: ESP8266Audio giải mã MP3 từ LittleFS Flash 8.8MB
 * Đa nhân: Chạy vòng giải mã trên Core 0 qua FreeRTOS Task với FreeRTOS Mutex an toàn
 * ============================================================================
 */

#include "audio_player.h"
#include <LittleFS.h>
#include <driver/i2s.h>
#include "AudioFileSourceLittleFS.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutputI2S.h"

// Lớp dẫn xuất giữ driver I2S luôn cài đặt sẵn trên ESP32-S3
// Ngăn AudioOutputI2S::stop() gọi i2s_driver_uninstall, giúp MAX98357A không mất clock và luôn sẵn sàng phát tức thì
class AudioOutputI2SPersistent : public AudioOutputI2S {
public:
    AudioOutputI2SPersistent() : AudioOutputI2S() {}
    virtual bool stop() override {
        #ifdef ESP32
        if (i2sOn) {
            i2s_zero_dma_buffer((i2s_port_t)portNo);
        }
        #endif
        // Giữ i2sOn = true, không gọi i2s_driver_uninstall
        return true;
    }
};

// ----------------------------------------------------------------------------
// 1. BIẾN TOÀN CỤC & ĐỐI TƯỢNG PHẦN CỨNG I2S
// ----------------------------------------------------------------------------
// Bộ nhớ tĩnh đệm trước cho libmad (tránh malloc/free gây phân mảnh RAM và tràn heap)
static uint8_t mp3PreallocBuffer[AudioGeneratorMP3::preAllocSize()];

static AudioGeneratorMP3        *mp3        = nullptr;
static AudioFileSourceLittleFS  *fileSource = nullptr;
static AudioOutputI2SPersistent *outI2S     = nullptr;

static AudioPriority currentPriority = AUDIO_PRIO_NONE;
static volatile bool stopRequested   = false;
static SemaphoreHandle_t audioMutex  = nullptr;

static float currentVolume = 0.85f; // Mức âm lượng mặc định 85%
static bool littleFsMounted = false;
static TaskHandle_t audioTaskHandle = nullptr;

// Dọn dẹp tài nguyên sau khi phát xong hoặc bị ngắt (gọi khi đã giữ audioMutex)
static void audioCleanupInternal() {
    if (mp3 && fileSource && mp3->isRunning()) {
        mp3->stop();
    }
    if (fileSource) {
        delete fileSource;
        fileSource = nullptr;
    }
    currentPriority = AUDIO_PRIO_NONE;
    stopRequested = false;
}

// ----------------------------------------------------------------------------
// 2. FREERTOS TASK TRÊN CORE 0: GIẢI MÃ LIÊN TỤC KHÔNG CHẶN GIAO DIỆN / ĐỘNG CƠ
// ----------------------------------------------------------------------------
static void audioCore0Task(void *param) {
    Serial.println("[AUDIO] Task giai ma MP3 I2S da khoi chay tren Core 0");
    while (true) {
        bool playing = audioLoop();
        if (playing) {
            // Đang phát âm thanh: nhường CPU 1ms giữa các frame để hệ thống đa nhiệm mượt mà
            vTaskDelay(pdMS_TO_TICKS(1));
        } else {
            // Khi rảnh: ngủ 20ms để giải phóng hoàn toàn Core 0 và không chiếm Mutex
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}

// ----------------------------------------------------------------------------
// 3. KHỞI TẠO HỆ THỐNG ÂM THANH
// ----------------------------------------------------------------------------
bool audioInit() {
    Serial.println("\n[AUDIO] ==================================================");
    Serial.printf("[AUDIO] Khoi tao Loa I2S MAX98357A (BCLK=%d, LRC=%d, DIN=%d)\n",
                  PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN);

    if (!audioMutex) {
        audioMutex = xSemaphoreCreateMutex();
    }

    // 1. Gắn kết phân vùng Flash LittleFS 8.8MB
    // Thử nhãn "littlefs" (theo partitions_16mb.csv), sau đó fallback sang "spiffs"
    bool mounted = LittleFS.begin(false, "/littlefs", 10, "littlefs");
    if (!mounted) {
        mounted = LittleFS.begin(false, "/littlefs", 10, "spiffs");
    }
    if (!mounted) {
        mounted = LittleFS.begin(false);
    }

    if (!mounted) {
        Serial.println("[AUDIO ERROR] Khong the mount phan vung LittleFS!");
        Serial.println("[AUDIO HINT] Vui long nap file am thanh bang lenh:");
        Serial.println("   pio run -t uploadfs -e esp32-s3-n16r8");
        littleFsMounted = false;
    } else {
        littleFsMounted = true;
        Serial.println("[AUDIO] Mount LittleFS thanh cong (Phan vung ~8.8MB)!");

        // Quét kiểm tra danh sách file âm thanh trong /audio
        File audioDir = LittleFS.open("/audio");
        if (audioDir && audioDir.isDirectory()) {
            int count = 0;
            File f = audioDir.openNextFile();
            while (f) {
                Serial.printf("   ├── [MP3] /audio/%s (%u bytes)\n", f.name(), (unsigned int)f.size());
                count++;
                f = audioDir.openNextFile();
            }
            Serial.printf("[AUDIO] Da tim thay %d file am thanh san sang.\n", count);
        } else {
            Serial.println("[AUDIO WARN] Chua tim thay thu muc /audio trong LittleFS.");
        }
    }

    // 2. Khởi tạo đầu ra I2S (Persistent driver - không bao giờ uninstall)
    outI2S = new AudioOutputI2SPersistent();
    outI2S->SetPinout(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN);
    outI2S->SetOutputModeMono(true); // Loa đơn (Mono)
    outI2S->SetGain(1.0f); // Âm lượng rõ nét

    // 3. Khởi tạo đối tượng giải mã MP3 với bộ đệm tĩnh cố định (Zero heap alloc, chống tràn RAM tuyệt đối)
    mp3 = new AudioGeneratorMP3(mp3PreallocBuffer, sizeof(mp3PreallocBuffer));

    // 4. Tạo FreeRTOS Task chuyên trách trên Core 0 với Stack 10240 bytes an toàn
    if (!audioTaskHandle) {
        xTaskCreatePinnedToCore(
            audioCore0Task,
            "AudioI2STask",
            10240,
            NULL,
            2,
            &audioTaskHandle,
            0 // Ghim cố định vào Core 0
        );
    }

    Serial.println("[AUDIO] Khoi tao phan he am thanh thanh cong!");
    Serial.println("[AUDIO] ==================================================\n");
    return true;
}

// ----------------------------------------------------------------------------
// 4. PHÁT FILE MP3 VỚI HÀNG ĐỢI ƯU TIÊN (PRIORITY QUEUE)
// ----------------------------------------------------------------------------
bool audioPlay(const char* filepath, AudioPriority prio) {
    if (!littleFsMounted || !mp3 || !outI2S || !audioMutex) return false;

    // Phản hồi click tức thì: UI priority chỉ chờ tối đa 10ms, âm thanh sự kiện chờ 100ms
    uint32_t waitMs = (prio == AUDIO_PRIO_UI) ? 10 : 100;
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(waitMs)) != pdTRUE) {
        Serial.printf("[AUDIO WARN] Mutex ban, bo qua: %s\n", filepath);
        return false;
    }

    bool isRunning = (mp3 && mp3->isRunning());
    AudioPriority curPrio = currentPriority;

    if (isRunning) {
        // Âm thanh UI không được ngắt các âm thanh quan trọng (Alarm, Medical, Info)
        if (prio == AUDIO_PRIO_UI && curPrio < AUDIO_PRIO_UI) {
            xSemaphoreGive(audioMutex);
            return false;
        }

        // Âm thanh mới có ưu tiên THẤP hơn âm thanh đang phát -> Bỏ qua
        if (prio > curPrio) {
            Serial.printf("[AUDIO] Bo qua %s (Prio %d < Prio %d dang phat)\n", filepath, prio, curPrio);
            xSemaphoreGive(audioMutex);
            return false;
        }

        // Âm thanh mới có ưu tiên CAO hơn hoặc bằng -> Ngắt file cũ an toàn dứt khoát
        audioCleanupInternal();
        // Cho phần cứng I2S DMA nghỉ 10ms để xả hoàn toàn mẫu cũ
        vTaskDelay(pdMS_TO_TICKS(10));
    } else {
        audioCleanupInternal();
    }

    // Mở file mới từ LittleFS an toàn bên ngoài critical section
    fileSource = new AudioFileSourceLittleFS(filepath);
    if (!fileSource || !fileSource->isOpen()) {
        if (fileSource) { delete fileSource; fileSource = nullptr; }
        xSemaphoreGive(audioMutex);
        Serial.printf("[AUDIO ERROR] Khong mo duoc stream LittleFS: %s\n", filepath);
        return false;
    }

    currentPriority = prio;
    stopRequested = false;

    // Bắt đầu giải mã stream
    if (!mp3->begin(fileSource, outI2S)) {
        if (fileSource) { delete fileSource; fileSource = nullptr; }
        currentPriority = AUDIO_PRIO_NONE;
        xSemaphoreGive(audioMutex);
        Serial.printf("[AUDIO ERROR] MP3 decoder khong the khoi dong cho: %s\n", filepath);
        return false;
    }

    xSemaphoreGive(audioMutex);
    Serial.printf("[AUDIO PLAY] Dang phat: %s [Prio %d]\n", filepath, prio);
    return true;
}

// ----------------------------------------------------------------------------
// 5. CÁC HÀM ĐIỀU KHIỂN & VÒNG LẶP STREAM
// ----------------------------------------------------------------------------
void audioStop() {
    stopRequested = true;
    if (audioMutex && xSemaphoreTake(audioMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        audioCleanupInternal();
        xSemaphoreGive(audioMutex);
    }
}

bool audioIsPlaying() {
    if (!audioMutex) return false;
    bool playing = false;
    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        playing = (mp3 && mp3->isRunning());
        xSemaphoreGive(audioMutex);
    }
    return playing;
}

void audioSetVolume(float volume) {
    currentVolume = constrain(volume, 0.0f, 2.0f);
    if (outI2S) outI2S->SetGain(currentVolume);
}

bool audioLoop() {
    if (!mp3 || !audioMutex) return false;
    bool active = false;

    if (xSemaphoreTake(audioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (stopRequested) {
            audioCleanupInternal();
            xSemaphoreGive(audioMutex);
            return false;
        }

        if (mp3 && mp3->isRunning()) {
            active = true;
            // Đẩy 1 chunk dữ liệu ra I2S DMA
            if (!mp3->loop()) {
                // Đã phát xong hết file
                audioCleanupInternal();
                Serial.println("[AUDIO] Hoan tat phat file am thanh.");
                active = false;
            }
        }
        xSemaphoreGive(audioMutex);
    }
    return active;
}

// ----------------------------------------------------------------------------
// 6. CÁC HÀM TIỆN ÍCH GHÉP NỐI VÀO SỰ KIỆN HỆ THỐNG
// ----------------------------------------------------------------------------

// ===== NHÓM 1: CHU TRÌNH CỮ THUỐC & UỐNG SỚM (FSM Y TẾ) =====
void audioPlayAlertDue() {
    audioPlay("/audio/alert_due.mp3", AUDIO_PRIO_MEDICAL);
}

void audioPlayAlertRemind() {
    audioPlay("/audio/alert_remind.mp3", AUDIO_PRIO_MEDICAL);
}

// 9. Mở ngăn thuốc: Phát trực tiếp đích danh file theo số ngăn (dispense_slot1..7.mp3)
void audioPlayDispenseSlot(int slot) {
    if (slot < 1 || slot > 7) slot = 1;
    char dynFile[40];
    snprintf(dynFile, sizeof(dynFile), "/audio/dispense_slot%d.mp3", slot);
    audioPlay(dynFile, AUDIO_PRIO_INFO);
}

void audioPlayDispenseNext() {
    audioPlay("/audio/dispense_next.mp3", AUDIO_PRIO_INFO);
}

void audioPlayTakenSuccess() {
    audioPlay("/audio/taken_success.mp3", AUDIO_PRIO_INFO);
}

void audioPlayMissedTimeout() {
    audioPlay("/audio/missed_timeout.mp3", AUDIO_PRIO_MEDICAL);
}

void audioPlayEarlyTake() {
    audioPlay("/audio/early_take.mp3", AUDIO_PRIO_INFO);
}

void audioPlayEarlySession(int sessionIdx) {
    const char* sessFiles[4] = {
        "/audio/early_take_morning.mp3",
        "/audio/early_take_noon.mp3",
        "/audio/early_take_afternoon.mp3",
        "/audio/early_take_evening.mp3"
    };
    if (sessionIdx >= 0 && sessionIdx < 4) {
        audioPlay(sessFiles[sessionIdx], AUDIO_PRIO_INFO);
    }
}

void audioPlayEarlyNoDose() {
    audioPlay("/audio/early_no_dose.mp3", AUDIO_PRIO_INFO);
}

// ===== NHÓM 2: MENU & NẠP / DỌN THUỐC THEO NGĂN =====
void audioPlayTaskEnter(int taskNum) {
    switch (taskNum) {
        case 1: audioPlay("/audio/task1_sched_enter.mp3", AUDIO_PRIO_UI); break;
        case 2: audioPlay("/audio/task2_refill_enter.mp3", AUDIO_PRIO_UI); break;
        case 3: audioPlay("/audio/task3_early_enter.mp3",  AUDIO_PRIO_UI); break;
        case 4: audioPlay("/audio/task4_hist_enter.mp3",   AUDIO_PRIO_UI); break;
        default: break;
    }
}

void audioPlaySchedSaved() {
    audioPlay("/audio/task1_sched_saved.mp3", AUDIO_PRIO_INFO);
}

void audioPlayRefillSlot(int slot) {
    if (slot < 1 || slot > 7) slot = 1;
    char dynFile[40];
    snprintf(dynFile, sizeof(dynFile), "/audio/refill_open_slot%d.mp3", slot);
    audioPlay(dynFile, AUDIO_PRIO_INFO);
}

void audioPlayRefillStart() {
    audioPlay("/audio/refill_start.mp3", AUDIO_PRIO_INFO);
}

void audioPlayRefillDone() {
    audioPlay("/audio/refill_done.mp3", AUDIO_PRIO_INFO);
}

void audioPlayRefillCancel() {
    audioPlay("/audio/refill_cancel.mp3", AUDIO_PRIO_INFO);
}

void audioPlayClearSlot(int slot) {
    if (slot < 1 || slot > 7) slot = 1;
    char dynFile[40];
    snprintf(dynFile, sizeof(dynFile), "/audio/clear_open_slot%d.mp3", slot);
    audioPlay(dynFile, AUDIO_PRIO_INFO);
}

void audioPlayClearDone() {
    audioPlay("/audio/clear_done.mp3", AUDIO_PRIO_INFO);
}

void audioPlayCalibEnter() {
    audioPlay("/audio/calib_enter.mp3", AUDIO_PRIO_INFO);
}

// ===== NHÓM 3: CẢNH BÁO AN TOÀN Y TẾ & MÔI TRƯỜNG =====
void audioPlayWarnLowStock() {
    audioPlay("/audio/warn_low_stock.mp3", AUDIO_PRIO_INFO);
}

void audioPlayWarnEmptyStock() {
    audioPlay("/audio/warn_empty_stock.mp3", AUDIO_PRIO_MEDICAL);
}

void audioPlayWarnTempHigh() {
    audioPlay("/audio/warn_temp_high.mp3", AUDIO_PRIO_MEDICAL);
}

void audioPlayWarnHumHigh() {
    audioPlay("/audio/warn_hum_high.mp3", AUDIO_PRIO_MEDICAL);
}

void audioPlayAlarmJammed() {
    audioPlay("/audio/alarm_jammed.mp3", AUDIO_PRIO_ALARM);
}

// ===== NHÓM 4: MẠNG IOT & HỆ THỐNG =====
void audioPlayIntro() {
    audioPlay("/audio/intro.mp3", AUDIO_PRIO_UI);
}

void audioPlayWifiConnected() {
    audioPlay("/audio/wifi_connected.mp3", AUDIO_PRIO_INFO);
}

void audioPlayWifiDisconnected() {
    audioPlay("/audio/wifi_disconnected.mp3", AUDIO_PRIO_UI);
}

void audioPlayResetSuccess() {
    audioPlay("/audio/reset_success.mp3", AUDIO_PRIO_INFO);
}

// ===== NHÓM 5: HIỆU ỨNG ÂM THANH GIAO DIỆN MÁY MÓC TỰ NHIÊN (TACTILE UI) =====
void audioPlayClick() {
    audioPlay("/audio/click.mp3", AUDIO_PRIO_UI);
}

void audioPlayBeepOk() {
    audioPlay("/audio/beep_ok.mp3", AUDIO_PRIO_UI);
}

void audioPlayBeepErr() {
    audioPlay("/audio/beep_err.mp3", AUDIO_PRIO_UI);
}

void audioPlayBeepCancel() {
    audioPlay("/audio/beep_cancel.mp3", AUDIO_PRIO_UI);
}
