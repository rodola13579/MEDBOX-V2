/**
 * ============================================================================
 * SMARTMED PILL DISPENSER — MQTT MANAGER IMPLEMENTATION
 * Nền tảng: ESP32-S3-WROOM-1 N16R8 | Thư viện: WiFi + PubSubClient + ArduinoJson
 * ============================================================================
 * Tối ưu hóa vận hành mượt mà, triệt tiêu lỗi Watchdog WDT và sụt áp nguồn:
 *   1. Non-blocking với timer millis(), giải phóng Core 0 hoàn toàn cho Loa I2S.
 *   2. Giới hạn TX Power 17dBm để bảo vệ đường nguồn USB 5V/3.3V không sụt áp.
 *   3. Chỉ thử kết nối Broker khi đã kết nối Wi-Fi thành công.
 * ============================================================================
 */

#include "mqtt_manager.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "app_state.h"
#include "motor_control.h"
#include "fsm_controller.h"
#include "display_ui.h"
#include "audio_player.h"

static WiFiClient espClient;
static PubSubClient mqttClient(espClient);

static unsigned long lastReconnectAttempt = 0;
static unsigned long lastTelemetryTime = 0;

// Các hàm nguyên mẫu
static void mqttCallback(char* topic, byte* payload, unsigned int length);
static bool reconnectMqtt();

void mqttManagerInit() {
    Serial.println("\n[MQTT] ==================================================");
    Serial.printf("[MQTT] Khoi tao ket noi Wi-Fi toi SSID: \"%s\"...\n", DEFAULT_WIFI_SSID);

    // Cấu hình Wi-Fi Station
    wifiState = WIFI_ST_CONNECTING;
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_15dBm); // 15dBm giảm mạnh dòng đỉnh RF, chống sụt áp nguồn USB
    WiFi.setAutoReconnect(true);
    WiFi.begin(DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASS);

    // Thiết lập TCP client timeout ngắn (1 giây) chống nghẽn vòng lặp
    espClient.setTimeout(1);

    // Cấu hình MQTT Client
    mqttClient.setServer(DEFAULT_MQTT_HOST, DEFAULT_MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(4096);
    mqttClient.setKeepAlive(MQTT_KEEP_ALIVE_SEC);

    Serial.println("[MQTT] San sang hoat dong (Buffer 4096B)!");
    Serial.println("[MQTT] ==================================================\n");
}

static bool ntpSynced = false;
static unsigned long lastNtpCheck = 0;

static void ntpUpdateCheck(unsigned long now) {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!ntpSynced || (now - lastNtpCheck > 3600000UL)) {
        if (now - lastNtpCheck > 2000) {
            lastNtpCheck = now;
            struct tm ti;
            if (getLocalTime(&ti, 50)) {
                if (ti.tm_year >= 120) { // Sau năm 2020 -> Giờ hợp lệ
                    uint8_t wday = (ti.tm_wday == 0) ? 6 : (ti.tm_wday - 1);
                    syncSystemTime(wday, ti.tm_hour, ti.tm_min, ti.tm_sec);
                    ntpSynced = true;
                    homeDirty = true;
                    if (screen == S_HOME) {
                        drawHome(false);
                    }
                }
            }
        }
    }
}

void mqttManagerUpdate() {
    unsigned long now = millis();

    // 1. Kiểm tra trạng thái Wi-Fi
    if (WiFi.status() == WL_CONNECTED) {
        if (wifiState != WIFI_ST_CONNECTED) {
            wifiState = WIFI_ST_CONNECTED;
            homeDirty = true;
            if (screen == S_HOME) {
                drawHomeHeader();
            }
            Serial.printf("[WIFI] Da ket noi thanh cong! IP: %s, RSSI: %d dBm\n",
                          WiFi.localIP().toString().c_str(), WiFi.RSSI());

            audioPlayWifiConnected(); // 🔊 "Đã kết nối Wi-Fi thành công."

            // Kích hoạt đồng bộ thời gian NTP qua Internet (UTC+7 Việt Nam)
            configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);
            Serial.println("[NTP] Da khoi chay tien trinh dong bo gio UTC+7...");
        }

        // Kiểm tra và cập nhật giờ NTP vào hệ thống
        ntpUpdateCheck(now);

        // 2. Kiểm tra kết nối MQTT Broker
        if (!mqttClient.connected()) {
            if (now - lastReconnectAttempt > 5000) {
                lastReconnectAttempt = now;
                if (reconnectMqtt()) {
                    lastReconnectAttempt = 0;
                }
            }
        } else {
            mqttClient.loop();

            // 3. Gửi Telemetry định kỳ
            if (now - lastTelemetryTime >= TELEMETRY_INTERVAL_MS) {
                lastTelemetryTime = now;
                mqttPublishTelemetry();
            }
        }
    } else {
        if (wifiState == WIFI_ST_CONNECTED) {
            wifiState = WIFI_ST_CONNECTING;
            homeDirty = true;
            if (screen == S_HOME) {
                drawHomeHeader();
            }
            static unsigned long lastWifiLostAudio = 0;
            if (now - lastWifiLostAudio > 30000UL) {
                lastWifiLostAudio = now;
                audioPlayWifiDisconnected(); // 🔊 "Mất kết nối Wi-Fi, đang thử kết nối lại."
            }
        }
    }
}

bool mqttIsConnected() {
    return mqttClient.connected();
}

static bool reconnectMqtt() {
    if (WiFi.status() != WL_CONNECTED) return false;
    Serial.printf("[MQTT] Dang ket noi toi Broker %s:%d...\n", DEFAULT_MQTT_HOST, DEFAULT_MQTT_PORT);

    // LWT: Tự động gửi OFFLINE khi mất nguồn đột ngột
    const char* lwtTopic = "pillbox/" DEVICE_ID "/lwt";
    const char* lwtPayload = "{\"status\":\"OFFLINE\"}";

    if (mqttClient.connect(DEVICE_ID, lwtTopic, 1, true, lwtPayload)) {
        Serial.println("[MQTT] >>> KET NOI BROKER THANH CONG! <<<");

        // Xóa retained LWT OFFLINE cũ nếu có trên Broker
        mqttClient.publish(lwtTopic, "", true);

        // Subscribe nhận lệnh từ Backend
        const char* subTopic = "pillbox/" DEVICE_ID "/cmd/#";
        mqttClient.subscribe(subTopic, 1);
        Serial.printf("[MQTT] Subscribed -> %s\n", subTopic);

        // Phát bản tin ONLINE & đồng bộ toàn bộ bảng lịch + tồn kho thuốc lên Backend
        mqttPublishStatus();
        mqttPublishAllSchedules();
        mqttPublishAllSlots();
        return true;
    } else {
        Serial.printf("[MQTT] Ket noi that bai, rc=%d. Se thu lai sau 5s.\n", mqttClient.state());
        return false;
    }
}

// ----------------------------------------------------------------------------
// TIẾP NHẬN & XỬ LÝ LỆNH TỪ BACKEND
// ----------------------------------------------------------------------------
static void mqttCallback(char* topic, byte* payload, unsigned int length) {
    static char jsonBuf[4096];
    if (length >= sizeof(jsonBuf)) length = sizeof(jsonBuf) - 1;
    memcpy(jsonBuf, payload, length);
    jsonBuf[length] = '\0';

    Serial.printf("[MQTT RECV] Topic: %s | Payload: %s\n", topic, jsonBuf);

    DynamicJsonDocument doc(4096);
    DeserializationError err = deserializeJson(doc, jsonBuf);
    if (err) {
        Serial.printf("[MQTT] JSON parse error: %s\n", err.c_str());
        return;
    }

    String topicStr = String(topic);
    tftResetActivityTimer();

    // 1. LỆNH ĐỒNG BỘ LỊCH UỐNG: cmd/sync_schedules
    if (topicStr.endsWith("/cmd/sync_schedules")) {
        JsonArray schedulesArr = doc["schedules"].as<JsonArray>();
        JsonArray items = doc["items"].as<JsonArray>();

        if (!schedulesArr.isNull() && schedulesArr.size() > 0) {
            nSlotSched = 0;
            for (JsonObject s : schedulesArr) {
                if (nSlotSched >= MAX_SLOT_SCHEDS) break;
                int slot = s["slot"] | 0;
                if (slot < 1 || slot > 7) continue;
                String tStr = s["time"].as<String>();
                int colon = tStr.indexOf(':');
                if (colon < 0) continue;
                uint8_t h = tStr.substring(0, colon).toInt();
                uint8_t m = tStr.substring(colon + 1).toInt();
                uint8_t dose = s["dose"] | 1;
                bool act = s.containsKey("is_active") ? s["is_active"].as<bool>() : true;

                slotSched[nSlotSched].slot = slot - 1; // 0..6
                slotSched[nSlotSched].hour = h;
                slotSched[nSlotSched].minute = m;
                slotSched[nSlotSched].dose = dose;
                slotSched[nSlotSched].active = act ? 1 : 0;
                nSlotSched++;
            }
            rebuildGroupedScheds();
            saveCfg();
            Serial.printf("[MQTT] Da dong bo %d lich theo tung ngan tu Backend!\n", nSlotSched);
            homeDirty = true;
            if (screen == S_SCHED) {
                drawSched(false);
            } else if (screen == S_SCHED_DETAIL) {
                drawSchedDetail(false);
            } else if (screen == S_HOME) {
                drawHome(false);
            }
        } else if (!items.isNull()) {
            nSlotSched = 0;
            for (JsonObject it : items) {
                uint8_t h = it["h"] | 0;
                uint8_t m = it["m"] | 0;
                uint8_t mask = it["mask"] | 0;
                uint8_t actMask = it.containsKey("active_mask") ? (uint8_t)(it["active_mask"] | 0) : mask;

                for (int s = 0; s < N_SLOTS; s++) {
                    if ((mask >> s) & 1) {
                        if (nSlotSched < MAX_SLOT_SCHEDS) {
                            slotSched[nSlotSched].slot = s;
                            slotSched[nSlotSched].hour = h;
                            slotSched[nSlotSched].minute = m;
                            slotSched[nSlotSched].dose = meds[s].dose ? meds[s].dose : 1;
                            slotSched[nSlotSched].active = (actMask >> s) & 1;
                            nSlotSched++;
                        }
                    }
                }
            }
            rebuildGroupedScheds();
            saveCfg();
            Serial.printf("[MQTT] Da dong bo %d lich theo ngan (tu items) vao Flash NVS!\n", nSlotSched);
            homeDirty = true;
            if (screen == S_SCHED) {
                drawSched(false);
            } else if (screen == S_SCHED_DETAIL) {
                drawSchedDetail(false);
            } else if (screen == S_HOME) {
                drawHome(false);
            }
        }

        // Tự động đồng bộ thời gian nếu gói sync_schedules có kèm timestamp
        if (doc.containsKey("timestamp")) {
            time_t epoch = doc["timestamp"];
            epoch += GMT_OFFSET_SEC;
            struct tm* ti = gmtime(&epoch);
            if (ti) {
                uint8_t wday = (ti->tm_wday == 0) ? 6 : (ti->tm_wday - 1);
                syncSystemTime(wday, ti->tm_hour, ti->tm_min, ti->tm_sec);
            }
        }
    }
    // LỆNH ĐỒNG BỘ THỜI GIAN TRỰC TIẾP TỪ BACKEND: cmd/sync_time
    else if (topicStr.endsWith("/cmd/sync_time")) {
        if (doc.containsKey("hour") && doc.containsKey("minute")) {
            uint8_t h = doc["hour"];
            uint8_t m = doc["minute"];
            uint8_t s = doc["second"] | 0;
            uint8_t w = doc["wday"] | 0;
            syncSystemTime(w, h, m, s);
        } else if (doc.containsKey("epoch") || doc.containsKey("timestamp")) {
            time_t epoch = doc.containsKey("epoch") ? (time_t)doc["epoch"] : (time_t)doc["timestamp"];
            epoch += GMT_OFFSET_SEC;
            struct tm* ti = gmtime(&epoch);
            if (ti) {
                uint8_t wday = (ti->tm_wday == 0) ? 6 : (ti->tm_wday - 1);
                syncSystemTime(wday, ti->tm_hour, ti->tm_min, ti->tm_sec);
            }
        }
    }
    // 2a. LỆNH BÁO CỮ THUỐC TỪ XA: cmd/due_alert
    else if (topicStr.endsWith("/cmd/due_alert")) {
        uint8_t slot = doc["target_slot"] | doc["slot"] | 0;
        uint8_t mask = doc["mask"] | 0;
        if (mask == 0 && slot >= 1 && slot <= N_SLOTS) {
            mask = (1 << (slot - 1));
        }
        if (mask != 0) {
            Serial.printf("[MQTT] Lenh DUE_ALERT -> Kich hoat bao cu Mask 0x%02X (15 phut dem nguoc)\n", mask);
            fsmTriggerDueAlert(mask);
        }
    }
    // 2b. LỆNH XÁC NHẬN UỐNG THUỐC TỪ WEB UI: cmd/confirm_dose
    else if (topicStr.endsWith("/cmd/confirm_dose")) {
        Serial.println("[MQTT] Lenh CONFIRM_DOSE tu Web -> Xac nhan uong thuoc va bat dau mo khay");
        fsmOnUserConfirmDose();
    }
    // 2c. LỆNH CẤP THUỐC: cmd/dispense
    else if (topicStr.endsWith("/cmd/dispense")) {
        uint8_t slot = doc["target_slot"] | doc["slot"] | 0;
        const char* src = doc["trigger_source"] | doc["source"] | "";
        if (slot >= 1 && slot <= N_SLOTS) {
            if (strcmp(src, "AUTO_SCHEDULE") == 0) {
                Serial.printf("[MQTT] Lenh DISPENSE theo AUTO_SCHEDULE -> Chuyen thanh DUE_ALERT cho 15p (Slot %d)\n", slot);
                fsmTriggerDueAlert(1 << (slot - 1));
            } else {
                Serial.printf("[MQTT] Lenh DISPENSE thu cong -> Slot %d\n", slot);
                fsmTriggerManualDispense(slot);
            }
        }
    }
    // 2d. LỆNH XÁC NHẬN ĐÃ LẤY THUỐC TỪ WEB UI: cmd/confirm_taken hoặc cmd/taken
    else if (topicStr.endsWith("/cmd/confirm_taken") || topicStr.endsWith("/cmd/taken")) {
        Serial.println("[MQTT] Lenh CONFIRM_TAKEN tu Web -> Xac nhan da lay thuoc ngan hien tai");
        fsmOnUserTakenDone();
    }
    // 3a. LỆNH HỦY NẠP THUỐC TỪ WEB: cmd/refill_cancel
    else if (topicStr.endsWith("/cmd/refill_cancel")) {
        static unsigned long lastRefillCancelMs = 0;
        if (millis() - lastRefillCancelMs > 1500) {
            lastRefillCancelMs = millis();
            Serial.println("[MQTT] Lenh REFILL_CANCEL -> Huy nap thuoc va ve Safe Home");
            fsmTriggerRefillCancel();
        }
    }
    // 3b. LỆNH VỀ NHÀ SAFE HOME CHUNG: cmd/home
    else if (topicStr.endsWith("/cmd/home")) {
        static unsigned long lastHomeCmdMs = 0;
        if (millis() - lastHomeCmdMs > 1500) {
            lastHomeCmdMs = millis();
            Serial.println("[MQTT] Lenh HOME -> Quay ve Ngan 0");
            dueMask = 0;
            motorRotateToSlot(0);
            mqttPublishHomeReached();
            fsmTransitionTo(FSM_SAFE_HOME_IDLE);
            if (screen == S_REFILL_WEB || screen == S_DUE_ALERT) {
                goTo(S_HOME);
            }
        }
    }
    // 4. LỆNH HIỆU CHUẨN TÌM MỐC TCRT5000: cmd/calibrate
    else if (topicStr.endsWith("/cmd/calibrate")) {
        Serial.println("[MQTT] Lenh CALIBRATE -> Do vach TCRT5000");
        autoHoming();
        currentMotorSlot = 0;
        homeOffset = 0;
        saveCfg();
        mqttPublishHomeReached();
    }
    // 5. LỆNH XOAY MOTOR KIỂM THỬ: cmd/move
    else if (topicStr.endsWith("/cmd/move")) {
        uint8_t slot = doc["target_slot"] | doc["slot"] | 0;
        motorRotateToSlot(slot);
        if (slot == 0) {
            mqttPublishHomeReached();
            if (screen == S_REFILL_WEB) {
                goTo(S_HOME);
            }
        }
        else mqttPublishSlotReached(slot);
    }
    // 6. LỆNH MỞ NGĂN NẠP THUỐC: cmd/refill
    else if (topicStr.endsWith("/cmd/refill") || topicStr.endsWith("/cmd/refill_start")) {
        uint8_t slot = 0;
        if (doc.containsKey("target_slot")) slot = doc["target_slot"].as<uint8_t>();
        else if (doc.containsKey("slot")) slot = doc["slot"].as<uint8_t>();
        else if (doc.containsKey("slot_number")) slot = doc["slot_number"].as<uint8_t>();
        
        if (slot >= 1 && slot <= N_SLOTS) {
            Serial.printf("[MQTT] Nhan lenh REFILL mo Ngan %d -> Xoay khay den dung vi tri...\n", slot);
            fsmTriggerRefillStart(slot);
        } else {
            Serial.printf("[MQTT ERR] Lenh REFILL co slot khong hop le: %d\n", slot);
        }
    }
    // 7. LỆNH HOÀN TẤT NẠP THUỐC: cmd/refill_done
    else if (topicStr.endsWith("/cmd/refill_done")) {
        uint8_t slot = doc["target_slot"] | doc["slot"] | 0;
        if (slot >= 1 && slot <= N_SLOTS) {
            if (doc.containsKey("qty"))  meds[slot - 1].qty = doc["qty"];
            if (doc.containsKey("dose")) meds[slot - 1].dose = doc["dose"];
            if (doc.containsKey("name")) {
                const char* n = doc["name"];
                if (n) strlcpy(meds[slot - 1].name, n, sizeof(meds[slot - 1].name));
            }
            saveCfg();
            Serial.printf("[MQTT] Refill done Slot %d: %s (con %d vien)\n", slot, meds[slot - 1].name, meds[slot - 1].qty);
        }
        fsmTriggerRefillDone();
    }
    // 8. LỆNH XÓA CỜ KẸT CƠ: cmd/reset_error
    else if (topicStr.endsWith("/cmd/reset_error")) {
        Serial.println("[MQTT] Lenh RESET_ERROR -> Ngat dien motor");
        motorDeenergize();
    }
    // 9. LỆNH FACTORY RESET XÓA SẠCH BỘ NHỚ FLASH NVS: cmd/factory_reset
    else if (topicStr.endsWith("/cmd/factory_reset")) {
        factoryResetNVS();
    }
}

// ----------------------------------------------------------------------------
// PHÁT SỰ KIỆN TỪ ESP32 LÊN BACKEND (PAYLOAD COMPACT TỐI ƯU)
// ----------------------------------------------------------------------------
void mqttPublishSlotReached(uint8_t slot) {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<128> doc;
    doc["event"] = "SLOT_REACHED";
    doc["slot"] = slot;
    char buf[128];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/dispense", buf, false);
}

void mqttPublishHomeReached() {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<128> doc;
    doc["event"] = "HOME_REACHED";
    doc["slot"] = 0;
    char buf[128];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/home", buf, false);
}

void mqttPublishTaken(uint8_t slot, uint8_t dose) {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<128> doc;
    doc["event"] = "TAKEN";
    doc["slot"] = slot;
    doc["dose"] = dose;
    char buf[128];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/taken", buf, false);
}

void mqttPublishEarlyTaken(uint8_t h, uint8_t m, uint8_t mask) {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<160> doc;
    doc["event"] = "EARLY_TAKEN";
    doc["h"] = h;
    doc["m"] = m;
    doc["mask"] = mask;
    char buf[160];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/taken", buf, false);
}

void mqttPublishMissed(uint8_t slot) {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<128> doc;
    doc["event"] = "MISSED";
    doc["slot"] = slot;
    char buf[128];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/missed", buf, false);
}

void mqttPublishDueAlert(uint8_t dueMask) {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<128> doc;
    doc["event"] = "DUE_ALERT";
    doc["mask"] = dueMask;
    char buf[128];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/due_alert", buf, false);
}

void mqttPublishAllSchedules() {
    if (!mqttClient.connected()) return;
    DynamicJsonDocument doc(2048);
    doc["event"] = "SYNC_SCHEDULES";
    JsonArray schedArr = doc.createNestedArray("schedules");
    for (int i = 0; i < nSlotSched; i++) {
        JsonObject s = schedArr.createNestedObject();
        s["slot"] = slotSched[i].slot + 1; // 1..7
        char tStr[8];
        snprintf(tStr, sizeof(tStr), "%02d:%02d", slotSched[i].hour, slotSched[i].minute);
        s["time"] = tStr;
        s["dose"] = slotSched[i].dose ? slotSched[i].dose : 1;
        s["is_active"] = (slotSched[i].active == 1);
    }
    JsonArray itemsArr = doc.createNestedArray("items");
    for (int i = 0; i < nSched; i++) {
        JsonObject it = itemsArr.createNestedObject();
        it["h"] = sched[i].h;
        it["m"] = sched[i].m;
        it["mask"] = sched[i].mask;
        it["active_mask"] = sched[i].activeMask;
    }
    String out;
    serializeJson(doc, out);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/sync_schedules", out.c_str(), false);
    Serial.printf("[MQTT] Da dong bo toan bo %d cu lich len Backend!\n", nSlotSched);
}

void mqttPublishAllSlots() {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<1024> doc;
    doc["event"] = "SYNC_SLOTS";
    JsonArray arr = doc.createNestedArray("slots");
    for (int i = 0; i < N_SLOTS; i++) {
        JsonObject s = arr.createNestedObject();
        s["slot"] = i + 1; // 1..7
        s["name"] = meds[i].name;
        s["qty"] = meds[i].qty;
        s["dose"] = meds[i].dose ? meds[i].dose : 1;
    }
    String out;
    serializeJson(doc, out);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/sync_slots", out.c_str(), false);
    Serial.println("[MQTT] Da dong bo toan bo ton kho 7 ngan thuoc len Backend!");
}

void mqttPublishScheduleCreated(uint8_t h, uint8_t m, uint8_t mask, uint8_t activeMask) {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<192> doc;
    doc["event"] = "SCHEDULE_SAVED";
    doc["h"] = h;
    doc["m"] = m;
    doc["mask"] = mask;
    doc["active_mask"] = (activeMask == 0xFF) ? mask : activeMask;
    char buf[192];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/schedule", buf, false);

    // Đồng thời đồng bộ toàn bộ bảng lịch lên Backend
    mqttPublishAllSchedules();
}

void mqttPublishRefillReady(uint8_t slot) {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<128> doc;
    doc["event"] = "REFILL_READY";
    doc["slot"] = slot;
    char buf[128];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/refill", buf, false);
}

void mqttPublishRefillDone(uint8_t slot, uint8_t qty, uint8_t added) {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<160> doc;
    doc["event"] = "REFILL_DONE";
    doc["slot"] = slot;
    doc["qty"] = qty;
    doc["added"] = added;
    char buf[160];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/refill", buf, false);
}

void mqttPublishSlotCleared(uint8_t slot) {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<128> doc;
    doc["event"] = "SLOT_CLEARED";
    doc["slot"] = slot;
    char buf[128];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/slot_cleared", buf, false);
}

void mqttPublishError(const char* code, const char* msg) {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<256> doc;
    doc["event"] = "ERROR";
    doc["code"] = code;
    doc["message"] = msg;
    char buf[256];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/events/alert", buf, false);
}

void mqttPublishStatus() {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<160> doc;
    doc["status"] = "ONLINE";
    doc["fsm"] = fsmGetStateString();
    doc["slot"] = currentMotorSlot;
    char buf[160];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/state/status", buf, true);
}

void mqttPublishTelemetry() {
    if (!mqttClient.connected()) return;
    StaticJsonDocument<256> doc;
    doc["temp"] = (int)round(liveTemp);
    doc["hum"] = (int)round(liveHum);
    doc["rssi"] = WiFi.RSSI();
    doc["heap"] = (uint32_t)ESP.getFreeHeap();
    doc["slot"] = currentMotorSlot;
    doc["fsm"] = fsmGetStateString();
    doc["up"] = (uint32_t)(millis() / 1000);
    doc["ip"] = WiFi.localIP().toString();

    char buf[256];
    serializeJson(doc, buf);
    mqttClient.publish("pillbox/" DEVICE_ID "/state/telemetry", buf, false);
}
