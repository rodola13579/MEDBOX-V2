/**
 * ============================================================================
 * MEDBOX PILL DISPENSER — MAIN FIRMWARE CONTROLLER
 * Phiên bản: 3.3.0 | Nền tảng: ESP32-S3-WROOM-1 N16R8
 * Hệ điều hành: FreeRTOS + Arduino Framework
 * Tách biệt phân hệ hiển thị: display_ui.h & display_ui.cpp
 * ============================================================================
 */

#include "app_state.h"
#include "audio_player.h"
#include "config.h"
#include "dht_sensor.h"
#include "display_ui.h"
#include "fsm_controller.h"
#include "motor_control.h"
#include "mqtt_manager.h"
#include <Arduino.h>
#include <Preferences.h>
#include <SPI.h>

// ----------------------------------------------------------------------------
// 1. ĐỊNH NGHĨA DỮ LIỆU & BIẾN TOÀN CỤC
// ----------------------------------------------------------------------------
const char CHARSET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";
const char *DAY_NAMES[7] = {"T2", "T3", "T4", "T5", "T6", "T7", "CN"};
const char *DAY_FULL[7] = {"Thu 2", "Thu 3", "Thu 4",   "Thu 5",
                           "Thu 6", "Thu 7", "Chu nhat"};

Screen screen = S_HOME;
Med meds[N_SLOTS];
Sched sched[N_SCHED];
uint8_t nSched = 0;
SlotSchedule slotSched[MAX_SLOT_SCHEDS];
uint8_t nSlotSched = 0;
Hist hist[N_HIST];
uint8_t histHead = 0, histCount = 0;
uint8_t dueMask = 0;
uint32_t dueAbs[N_SLOTS];

int menuSel = 0, schedSel = 0, medsSel = 0, medRow = 0, curSlot = 0,
    histTop = 0;
int schedDetailSel = 0;
int schedIdx = 0;
uint8_t editTarget = ET_TIME;
int editField = 0, editVal[3];
uint8_t pickMode = PK_SCHED;
uint8_t pickMask = 0;
int pickSel = 0;
int8_t nameIdx[NAME_LEN];
int namePos = 0;
bool nameWasNew = false;
uint8_t numTarget = NT_QTY;
int numVal = 0, numMin = 0, numMax = 99;
int refillAddQty = 5;
bool homeDirty = false;

// Quản lý trạng thái cữ uống thuốc trong ngày (Chống uống trùng lặp - Anti
// Double Dose)
uint16_t schedTakenMask = 0;
uint8_t schedTakenDay = 0xFF;
int earlySchedSel = 0;
int earlySessionSel = 0; // 0: Sáng, 1: Trưa, 2: Chiều, 3: Tối
uint8_t earlySlots[N_SLOTS] = {0};
uint8_t earlySlotCount = 0;
uint8_t earlyCurrentStep = 0;

const char *const SESSION_NAMES[4] = {"Buoi Sang", "Buoi Trua", "Buoi Chieu",
                                      "Buoi Toi"};
const char *const SESSION_HOURS[4] = {"05:00 - 11:00", "11:00 - 14:00",
                                      "14:00 - 18:00", "18:00 - 23:00"};

int getSessionIndexFromHour(uint8_t h) {
  if (h >= 5 && h < 11)
    return 0; // Sáng
  if (h >= 11 && h < 14)
    return 1; // Trưa
  if (h >= 14 && h < 18)
    return 2; // Chiều
  return 3;   // Tối
}

uint8_t getSessionMask(int sessionIdx) {
  uint8_t mask = 0;
  for (int i = 0; i < nSched; i++) {
    if (getSessionIndexFromHour(sched[i].h) == sessionIdx) {
      mask |= (sched[i].mask & sched[i].activeMask);
    }
  }
  return mask;
}

int getSessionSlotCount(int sessionIdx) {
  return __builtin_popcount(getSessionMask(sessionIdx));
}

void getSessionTimeDesc(int sessionIdx, char *buf, size_t len) {
  int count = 0;
  char times[48] = "";
  for (int i = 0; i < nSched; i++) {
    if (getSessionIndexFromHour(sched[i].h) == sessionIdx) {
      char tmp[10];
      snprintf(tmp, sizeof(tmp), "%s%02d:%02d", count > 0 ? ", " : "",
               sched[i].h, sched[i].m);
      strncat(times, tmp, sizeof(times) - strlen(times) - 1);
      count++;
      if (count >= 2)
        break;
    }
  }
  if (count == 0) {
    snprintf(buf, len, "%s", SESSION_HOURS[sessionIdx]);
  } else {
    snprintf(buf, len, "%s (%s)", times, SESSION_HOURS[sessionIdx]);
  }
}

const char *getSessionName(uint8_t h) {
  if (h >= 5 && h < 11)
    return "Buoi Sang";
  if (h >= 11 && h < 14)
    return "Buoi Trua";
  if (h >= 14 && h < 18)
    return "Buoi Chieu";
  if (h >= 18 && h < 23)
    return "Buoi Toi";
  return "Ban Dem";
}

void resetSchedTakenIfNewDay() {
  uint8_t cd = curDay();
  if (schedTakenDay != cd) {
    schedTakenDay = cd;
    schedTakenMask = 0;
    saveCfg();
    Serial.printf(
        "[SCHEDULE] Sang ngay moi (Thu %s) -> Reset cữ đã uống hôm nay\n",
        DAY_NAMES[cd % 7]);
  }
}

bool isSchedTakenToday(int idx) {
  if (idx < 0 || idx >= nSched)
    return false;
  if (schedTakenDay != curDay())
    return false;
  return (schedTakenMask >> idx) & 1;
}

void markSchedTakenToday(int idx) {
  if (idx < 0 || idx >= nSched)
    return;
  resetSchedTakenIfNewDay();
  schedTakenMask |= (1 << idx);
  saveCfg();
  Serial.printf("[SCHEDULE] >>> ĐÃ ĐÁNH DẤU CỮ %d (%02d:%02d) ĐÃ UỐNG HÔM NAY! "
                "(Mask: 0x%04X)\n",
                idx + 1, sched[idx].h, sched[idx].m, schedTakenMask);
}

bool isSessionTakenToday(int sessionIdx) {
  bool hasSched = false;
  for (int i = 0; i < nSched; i++) {
    if (getSessionIndexFromHour(sched[i].h) == sessionIdx) {
      hasSched = true;
      if (!isSchedTakenToday(i))
        return false;
    }
  }
  return hasSched;
}

void markSessionTakenToday(int sessionIdx) {
  for (int i = 0; i < nSched; i++) {
    if (getSessionIndexFromHour(sched[i].h) == sessionIdx) {
      markSchedTakenToday(i);
    }
  }
}

// Cảm biến môi trường thực tế (DHT11) & Wi-Fi
float liveTemp =
    0.0f; // Bắt đầu từ 0.0 (chờ cảm biến DHT11 cập nhật giá trị phòng thật)
float liveHum = 0.0f;
WifiState wifiState = WIFI_ST_CONNECTED;

// Động cơ bước & Hiệu chuẩn
int currentMotorSlot = 0;
int16_t homeOffset = 0;
int16_t stepsPerSlot = STEPS_PER_SLOT;
int calibSel = 0;
int calibTestSlot = 1;
int16_t jogDelta = 0;

Preferences prefs;

// ----------------------------------------------------------------------------
// 3. NÚM XOAY EC11 & NÚT NHẤN (KHỬ RUNG & NGẮT PHẦN CỨNG)
// ----------------------------------------------------------------------------
volatile int8_t encState = 0;
volatile int8_t encAcc = 0;
volatile int32_t encDelta = 0;

void IRAM_ATTR encISR() {
  static const int8_t tbl[16] = {0,  -1, 1, 0, 1, 0, 0,  -1,
                                 -1, 0,  0, 1, 0, 1, -1, 0};
  encState = ((encState << 2) | (digitalRead(PIN_EC11_CLK) << 1) |
              digitalRead(PIN_EC11_DT)) &
             0x0F;
  encAcc += tbl[encState];
  if (encAcc >= 4) {
    encDelta++;
    encAcc = 0;
  } else if (encAcc <= -4) {
    encDelta--;
    encAcc = 0;
  }
}

int encRead() {
  noInterrupts();
  int d = encDelta;
  encDelta = 0;
  interrupts();
  return d;
}

struct Btn {
  uint8_t pin;
  bool last = true;
  uint32_t tDown = 0, tChange = 0;

  void begin(uint8_t p) {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
  }

  uint8_t poll() {
    bool v = digitalRead(pin);
    uint32_t now = millis();
    if (v != last && now - tChange > 25) {
      tChange = now;
      last = v;
      if (!v)
        tDown = now;
      else
        return (now - tDown > 800) ? 2 : 1; // 1 = Click, 2 = Giữ (>800ms)
    }
    return 0;
  }
} btnPush, btnBack;

// ----------------------------------------------------------------------------
// 4. QUẢN LÝ FLASH NVS (PREFERENCES)
// ----------------------------------------------------------------------------
void saveCfg() {
  if (!prefs.begin("medbox", false))
    return;
  prefs.putBytes("meds", meds, sizeof(meds));
  prefs.putBytes("slot_sch", slotSched, sizeof(SlotSchedule) * nSlotSched);
  prefs.putUChar("n_slotsch", nSlotSched);
  prefs.putBytes("sch", sched, sizeof(sched));
  prefs.putUChar("nsch", nSched);
  prefs.putShort("h_off", homeOffset);
  prefs.putShort("s_step", stepsPerSlot);
  prefs.putUChar("w_st", (uint8_t)wifiState);
  prefs.putUShort("s_tmask", schedTakenMask);
  prefs.putUChar("s_tday", schedTakenDay);
  prefs.end();
}

void saveHist() {
  if (!prefs.begin("medbox", false))
    return;
  prefs.putBytes("hist", hist, sizeof(hist));
  prefs.putUChar("hh", histHead);
  prefs.putUChar("hc", histCount);
  prefs.end();
}

void factoryResetNVS() {
  Serial.println("\n[FACTORY RESET] Bat dau xoa toan bo Flash NVS va RAM...");
  if (prefs.begin("medbox", false)) {
    prefs.clear();
    prefs.end();
  }
  for (int i = 0; i < N_SLOTS; i++) {
    meds[i].dose = 1;
    meds[i].qty = 0;
    meds[i].name[0] = '\0';
  }
  memset(sched, 0, sizeof(sched));
  nSched = 0;
  memset(slotSched, 0, sizeof(slotSched));
  nSlotSched = 0;
  memset(hist, 0, sizeof(hist));
  histHead = 0;
  histCount = 0;
  dueMask = 0;
  homeOffset = 0;
  stepsPerSlot = STEPS_PER_SLOT;
  schedTakenMask = 0;
  schedTakenDay = 0xFF;

  saveCfg();
  saveHist();
  homeDirty = true;
  toast("Da Reset xuat xuong!");
  Serial.println("[FACTORY RESET] Flash NVS da duoc lam sach 100%!");
  motorRotateToSlot(0);
  mqttPublishHomeReached();
  mqttPublishAllSchedules();
}

void loadData() {
  if (!prefs.begin("medbox", true))
    return;
  if (prefs.getBytesLength("meds") == sizeof(meds))
    prefs.getBytes("meds", meds, sizeof(meds));

  if (prefs.isKey("n_slotsch")) {
    nSlotSched = prefs.getUChar("n_slotsch", 0);
    if (nSlotSched > MAX_SLOT_SCHEDS)
      nSlotSched = 0;
    if (nSlotSched > 0) {
      prefs.getBytes("slot_sch", slotSched, sizeof(SlotSchedule) * nSlotSched);
    }
    rebuildGroupedScheds();
  } else {
    // Migration: nếu chưa có "slot_sch", nạp từ "sch" cũ và tự động chuyển đổi
    size_t schLen = prefs.getBytesLength("sch");
    if (schLen == sizeof(sched)) {
      prefs.getBytes("sch", sched, sizeof(sched));
      nSched = prefs.getUChar("nsch", 0);
      if (nSched > N_SCHED)
        nSched = 0;
      for (int i = 0; i < nSched; i++) {
        if (sched[i].mask != 0 && sched[i].activeMask == 0) {
          sched[i].activeMask = sched[i].mask;
        }
      }
    } else if (schLen == 3 * N_SCHED) {
      uint8_t oldSch[3 * N_SCHED];
      prefs.getBytes("sch", oldSch, sizeof(oldSch));
      nSched = prefs.getUChar("nsch", 0);
      if (nSched > N_SCHED)
        nSched = 0;
      for (int i = 0; i < nSched; i++) {
        sched[i].h = oldSch[i * 3];
        sched[i].m = oldSch[i * 3 + 1];
        sched[i].mask = oldSch[i * 3 + 2];
        sched[i].activeMask = sched[i].mask;
      }
    }
    // Chuyển đổi sched[] cũ sang slotSched[]
    nSlotSched = 0;
    for (int i = 0; i < nSched; i++) {
      for (int s = 0; s < N_SLOTS; s++) {
        if ((sched[i].mask >> s) & 1) {
          if (nSlotSched < MAX_SLOT_SCHEDS) {
            slotSched[nSlotSched].slot = s;
            slotSched[nSlotSched].hour = sched[i].h;
            slotSched[nSlotSched].minute = sched[i].m;
            slotSched[nSlotSched].dose = meds[s].dose ? meds[s].dose : 1;
            slotSched[nSlotSched].active = (sched[i].activeMask >> s) & 1;
            nSlotSched++;
          }
        }
      }
    }
    rebuildGroupedScheds();
  }
  if (prefs.getBytesLength("hist") == sizeof(hist)) {
    prefs.getBytes("hist", hist, sizeof(hist));
    histHead = prefs.getUChar("hh", 0) % N_HIST;
    histCount = prefs.getUChar("hc", 0);
    if (histCount > N_HIST)
      histCount = 0;
  }
  homeOffset = prefs.getShort("h_off", 0);
  stepsPerSlot = prefs.getShort("s_step", STEPS_PER_SLOT);
  if (stepsPerSlot < 400 || stepsPerSlot > 600)
    stepsPerSlot = STEPS_PER_SLOT;
  wifiState = (WifiState)prefs.getUChar("w_st", WIFI_ST_CONNECTED);
  if (wifiState > WIFI_ST_AP)
    wifiState = WIFI_ST_CONNECTED;
  schedTakenMask = prefs.getUShort("s_tmask", 0);
  schedTakenDay = prefs.getUChar("s_tday", 0xFF);
  prefs.end();
  for (int i = 0; i < N_SLOTS; i++)
    meds[i].name[NAME_LEN] = 0;
}

// ----------------------------------------------------------------------------
// 5. ĐỒNG HỒ & BỘ LẬP LỊCH
// ----------------------------------------------------------------------------
uint32_t baseSec = 8UL * 3600;
uint8_t baseDay = 0;
uint32_t baseMs = 0;
uint32_t lastAbsMin = 0;

uint32_t absSec() { return baseSec + (millis() - baseMs) / 1000; }
int curMinOfDay() { return (absSec() % 86400UL) / 60; }
int curDay() { return (baseDay + absSec() / 86400UL) % 7; }

void syncSystemTime(uint8_t day, uint8_t hour, uint8_t min, uint8_t sec) {
  baseDay = day % 7;
  baseSec = hour * 3600UL + min * 60UL + sec;
  baseMs = millis();
  lastAbsMin = absSec() / 60;
  homeDirty = true;
  Serial.printf(
      "[TIME SYNC] Thoi gian he thong da cap nhat: %s %02d:%02d:%02d\n",
      DAY_FULL[baseDay], hour, min, sec);
}

bool slotHasMed(int s) { return meds[s].name[0] != 0; }
bool slotCanDispense(int s) { return slotHasMed(s) && meds[s].qty > 0; }
bool slotUsable(int s) {
  return pickMode == PK_DISP ? slotCanDispense(s) : slotHasMed(s);
}
int editNF() { return editTarget == ET_CLOCK ? 3 : 2; }
int visRows() { return (H - HDR_H - 10 * UI) / (12 * UI); }
int schedRows() { return nSched + (nSched < N_SCHED ? 1 : 0); }

void maskStr(uint8_t mask, char *o, size_t n) {
  size_t len = 0;
  o[0] = 0;
  for (int i = 0; i < N_SLOTS; i++) {
    if (!((mask >> i) & 1) || len + 3 >= n)
      continue;
    if (len)
      o[len++] = ',';
    o[len++] = '1' + i;
    o[len] = 0;
  }
}

void fmtRemain(char *o, size_t n, int m) {
  if (m < 60)
    snprintf(o, n, "con %dp", m);
  else if (m < 1440)
    snprintf(o, n, "con %dh%02d", m / 60, m % 60);
  else
    snprintf(o, n, "con 1 ngay");
}

bool findNext(int &idxOut, int &minsOut) {
  int cm = curMinOfDay(), best = 0x7FFFFFFF;
  bool f = false;
  for (int i = 0; i < nSched; i++) {
    uint8_t effectiveMask = sched[i].mask & sched[i].activeMask;
    if (!effectiveMask)
      continue;
    int diff = sched[i].h * 60 + sched[i].m - cm;
    if (diff <= 0)
      diff += 1440;
    if (diff < best) {
      best = diff;
      idxOut = i;
      minsOut = diff;
      f = true;
    }
  }
  return f;
}

// ----------------------------------------------------------------------------
// 6. NGHIỆP VỤ Y TẾ & THAO TÁC KHAY
// ----------------------------------------------------------------------------
void addHist(int slot, uint8_t type) {
  hist[histHead].day = curDay();
  hist[histHead].min = curMinOfDay();
  hist[histHead].slot = slot;
  hist[histHead].type = type;
  histHead = (histHead + 1) % N_HIST;
  if (histCount < N_HIST)
    histCount++;
  saveHist();
}

void dispenseSlot(int s) {
  Serial.printf("[MEDBOX] Xoay mo ngan thuoc %d: %s\n", s + 1, meds[s].name);
  motorRotateToSlot(s + 1);
  audioPlayDispenseSlot(s + 1); // Phát file TTS hoặc dispense_generic.mp3
}

void consumeDose(int s) {
  uint8_t d = meds[s].dose ? meds[s].dose : 1;
  meds[s].qty = (meds[s].qty > d) ? meds[s].qty - d : 0;
}

void sortSched() {
  for (int i = 1; i < nSched; i++) {
    Sched k = sched[i];
    int j = i - 1;
    while (j >= 0 && sched[j].h * 60 + sched[j].m > k.h * 60 + k.m) {
      sched[j + 1] = sched[j];
      j--;
    }
    sched[j + 1] = k;
  }
}

void rebuildGroupedScheds() {
  nSched = 0;
  memset(sched, 0, sizeof(sched));

  for (int i = 0; i < nSlotSched; i++) {
    uint8_t s = slotSched[i].slot;
    if (s >= N_SLOTS)
      continue;
    uint8_t h = slotSched[i].hour;
    uint8_t m = slotSched[i].minute;
    uint8_t bit = (1 << s);

    int found = -1;
    for (int k = 0; k < nSched; k++) {
      if (sched[k].h == h && sched[k].m == m) {
        found = k;
        break;
      }
    }

    if (found >= 0) {
      sched[found].mask |= bit;
      if (slotSched[i].active) {
        sched[found].activeMask |= bit;
      }
    } else if (nSched < N_SCHED) {
      sched[nSched].h = h;
      sched[nSched].m = m;
      sched[nSched].mask = bit;
      sched[nSched].activeMask = slotSched[i].active ? bit : 0;
      nSched++;
    }
  }

  sortSched();
}

bool setSlotScheduleActive(uint8_t slot, uint8_t h, uint8_t m, bool active) {
  bool found = false;
  for (int i = 0; i < nSlotSched; i++) {
    if (slotSched[i].slot == slot && slotSched[i].hour == h &&
        slotSched[i].minute == m) {
      slotSched[i].active = active ? 1 : 0;
      found = true;
      break;
    }
  }
  if (found) {
    rebuildGroupedScheds();
  }
  return found;
}

int countSlotSchedules(uint8_t slot) {
  int cnt = 0;
  for (int i = 0; i < nSlotSched; i++) {
    if (slotSched[i].slot == slot) {
      cnt++;
    }
  }
  return cnt;
}

bool upsertSlotSchedule(uint8_t slot, uint8_t h, uint8_t m, uint8_t dose,
                        bool active) {
  for (int i = 0; i < nSlotSched; i++) {
    if (slotSched[i].slot == slot && slotSched[i].hour == h &&
        slotSched[i].minute == m) {
      slotSched[i].dose = dose;
      slotSched[i].active = active ? 1 : 0;
      rebuildGroupedScheds();
      return true;
    }
  }
  if (countSlotSchedules(slot) >= MAX_SCHEDS_PER_SLOT) {
    Serial.printf("[SCHEDULE] Ngan %d da dat toi da %d cu!\n", slot + 1,
                  MAX_SCHEDS_PER_SLOT);
    return false;
  }
  if (nSlotSched < MAX_SLOT_SCHEDS) {
    slotSched[nSlotSched].slot = slot;
    slotSched[nSlotSched].hour = h;
    slotSched[nSlotSched].minute = m;
    slotSched[nSlotSched].dose = dose;
    slotSched[nSlotSched].active = active ? 1 : 0;
    nSlotSched++;
    rebuildGroupedScheds();
    return true;
  }
  return false;
}

bool removeSlotSchedule(uint8_t slot, uint8_t h, uint8_t m) {
  for (int i = 0; i < nSlotSched; i++) {
    if (slotSched[i].slot == slot && slotSched[i].hour == h &&
        slotSched[i].minute == m) {
      for (int j = i; j < nSlotSched - 1; j++) {
        slotSched[j] = slotSched[j + 1];
      }
      nSlotSched--;
      rebuildGroupedScheds();
      return true;
    }
  }
  return false;
}

void removeSlotSchedulesBySlot(uint8_t slot) {
  int i = 0;
  while (i < nSlotSched) {
    if (slotSched[i].slot == slot) {
      for (int j = i; j < nSlotSched - 1; j++) {
        slotSched[j] = slotSched[j + 1];
      }
      nSlotSched--;
    } else {
      i++;
    }
  }
  rebuildGroupedScheds();
}

void removeTimeCluster(uint8_t h, uint8_t m) {
  int i = 0;
  while (i < nSlotSched) {
    if (slotSched[i].hour == h && slotSched[i].minute == m) {
      for (int j = i; j < nSlotSched - 1; j++) {
        slotSched[j] = slotSched[j + 1];
      }
      nSlotSched--;
    } else {
      i++;
    }
  }
  rebuildGroupedScheds();
}

void tickSchedule() {
  uint32_t am = absSec() / 60;
  if (am == lastAbsMin)
    return;
  lastAbsMin = am;
  int mod = am % 1440;

  resetSchedTakenIfNewDay();

  for (int i = 0; i < nSched; i++) {
    if (sched[i].h * 60 + sched[i].m != mod)
      continue;

    // NẾU CỮ NÀY ĐÃ ĐƯỢC UỐNG HÔM NAY (UỐNG SỚM HOẶC ĐÃ UỐNG) -> BỎ QUA, KHÔNG
    // REO LẠI
    if (isSchedTakenToday(i)) {
      Serial.printf("[SCHEDULE] Cu %02d:%02d da duoc uong hom nay -> Bo qua "
                    "khong kich hoat lai chuong reo!\n",
                    sched[i].h, sched[i].m);
      continue;
    }

    uint8_t newMask = 0;
    uint8_t effectiveMask = sched[i].mask;
    if (sched[i].activeMask != 0) {
      effectiveMask &= sched[i].activeMask;
    }
    for (int s = 0; s < N_SLOTS; s++) {
      if (((effectiveMask >> s) & 1) && !((dueMask >> s) & 1)) {
        newMask |= (1 << s);
        dueAbs[s] = am;
      }
    }
    if (newMask != 0) {
      Serial.printf("[SCHEDULE] >>> DEN GIO UONG THUOC: Mask 0x%02X!\n",
                    newMask);
      tftWake();
      tftResetActivityTimer();
      fsmTriggerDueAlert(newMask);
      return; // Cữ thuốc đã kích hoạt -> Thoát ngay, KHÔNG đánh dấu homeDirty!
    }
  }
  if (screen == S_HOME) {
    homeDirty = true;
  }
}

void toggleWifiMode() {
  wifiState = (WifiState)((wifiState + 1) % 4);
  if (prefs.begin("medbox", false)) {
    prefs.putUChar("w_st", (uint8_t)wifiState);
    prefs.end();
  }
  switch (wifiState) {
  case WIFI_ST_OFF:
    toast("WiFi: TAT (OFF)");
    break;
  case WIFI_ST_CONNECTING:
    toast("WiFi: DANG NOI");
    break;
  case WIFI_ST_CONNECTED:
    toast("WiFi: DA NOI");
    break;
  case WIFI_ST_AP:
    toast("WiFi: PHAT AP");
    break;
  }
}

void goTo(Screen s) {
  screen = s;
  homeDirty = false;
  tftResetActivityTimer();
  render(true);
}

// ----------------------------------------------------------------------------
// 7. CÁC THAO TÁC CÀI ĐẶT
// ----------------------------------------------------------------------------
void startSchedEdit(int idx) {
  schedIdx = idx;
  editTarget = ET_TIME;
  editField = 0;
  if (idx < nSched) {
    editVal[0] = sched[idx].h;
    editVal[1] = sched[idx].m;
    pickMask = sched[idx].mask;
  } else {
    editVal[0] = 8;
    editVal[1] = 0;
    pickMask = 0;
  }
  editVal[2] = 0;
  goTo(S_EDIT);
}

void startClock() {
  editTarget = ET_CLOCK;
  editField = 0;
  int m = curMinOfDay();
  editVal[0] = m / 60;
  editVal[1] = m % 60;
  editVal[2] = curDay();
  goTo(S_EDIT);
}

void startPick(uint8_t mode) {
  pickMode = mode;
  pickSel = 0;
  if (mode == PK_DISP)
    pickMask = 0;
  goTo(S_PICK);
}

void startName() {
  nameWasNew = !slotHasMed(curSlot);
  int len = strlen(meds[curSlot].name);
  for (int i = 0; i < NAME_LEN; i++) {
    int idx = 0;
    if (i < len) {
      const char *p = strchr(CHARSET, meds[curSlot].name[i]);
      if (p)
        idx = p - CHARSET;
    }
    nameIdx[i] = idx;
  }
  namePos = 0;
  goTo(S_NAME);
}

void startNum(uint8_t target) {
  numTarget = target;
  if (target == NT_QTY) {
    numVal = meds[curSlot].qty;
    numMin = 0;
    numMax = 250;
    // Đảm bảo khay xoay đúng đến ngăn cần nạp thuốc
    if (currentMotorSlot != curSlot + 1) {
      Serial.printf("[MAIN] Nạp thuốc: Xoay khay đến Ngăn %d...\n",
                    curSlot + 1);
      char msg[36];
      snprintf(msg, sizeof(msg), "Mo Ngan %d nap thuoc...", curSlot + 1);
      toast(msg);
      motorRotateToSlot(curSlot + 1);
      mqttPublishRefillReady(curSlot + 1);
    }
    audioPlayRefillStart(); // "Đang mở ngăn nạp thuốc, xin hãy kiểm tra đúng
                            // loại thuốc"
  } else {
    numVal = meds[curSlot].dose ? meds[curSlot].dose : 1;
    numMin = 1;
    numMax = 9;
  }
  goTo(S_NUM);
}

void saveName() {
  char b[NAME_LEN + 1];
  for (int i = 0; i < NAME_LEN; i++)
    b[i] = CHARSET[nameIdx[i]];
  b[NAME_LEN] = 0;
  int len = NAME_LEN;
  while (len > 0 && b[len - 1] == ' ')
    len--;
  b[len] = 0;
  int st = 0;
  while (b[st] == ' ')
    st++;
  if (len == 0) {
    toast("Chua nhap ten");
    render(true);
    return;
  }
  strncpy(meds[curSlot].name, b + st, NAME_LEN);
  meds[curSlot].name[NAME_LEN] = 0;
  if (nameWasNew) {
    meds[curSlot].qty = 0;
    meds[curSlot].dose = 1;
  }
  saveCfg();
  if (nameWasNew)
    startNum(NT_QTY);
  else
    goTo(S_MED);
}

void saveNum() {
  if (numTarget == NT_QTY) {
    meds[curSlot].qty = numVal;
    // Nạp số lượng xong -> Tự động xoay đóng về Ngăn 0 Safe Home
    Serial.printf(
        "[MAIN] Da luu so luong Ngan %d: %d vien -> Khoa khay ve Safe Home\n",
        curSlot + 1, numVal);
    motorRotateToSlot(0);
    mqttPublishRefillDone(curSlot + 1, meds[curSlot].qty, 0);
    mqttPublishHomeReached();
    audioPlayRefillDone(); // "Nạp thuốc thành công, khay đã khóa an toàn"
    toast("Da nap xong -> Khoa Ngan 0");
  } else {
    meds[curSlot].dose = numVal;
    mqttPublishRefillDone(curSlot + 1, meds[curSlot].qty, 0);
  }
  saveCfg();
  goTo(S_MED);
}

void deleteMed() {
  memset(&meds[curSlot], 0, sizeof(Med));
  meds[curSlot].dose = 1;
  dueMask &= ~(1 << curSlot);
  removeSlotSchedulesBySlot(curSlot);
  saveCfg();
  mqttPublishSlotCleared(curSlot + 1);
  mqttPublishRefillDone(curSlot + 1, 0, 0);
  goTo(S_MEDS);
}

void saveEdit() {
  if (editTarget == ET_CLOCK) {
    baseSec = editVal[0] * 3600UL + editVal[1] * 60UL;
    baseDay = editVal[2];
    baseMs = millis();
    lastAbsMin = absSec() / 60;
    dueMask = 0;
    toast("Da luu gio!");
    goTo(S_HOME);
  } else {
    startPick(PK_SCHED); // Chuyển qua chọn ngăn thuốc
  }
}

void commitSched() {
  uint8_t newH = editVal[0];
  uint8_t newM = editVal[1];

  if (schedIdx < nSched) {
    // Đang chỉnh sửa một mốc giờ đã có: xóa các ngăn cũ ở mốc giờ đó
    uint8_t oldH = sched[schedIdx].h;
    uint8_t oldM = sched[schedIdx].m;
    uint8_t oldMask = sched[schedIdx].mask;

    for (int s = 0; s < N_SLOTS; s++) {
      if ((oldMask >> s) & 1) {
        removeSlotSchedule(s, oldH, oldM);
      }
    }
  }

  // Thêm hoặc cập nhật các ngăn được chọn trong pickMask vào mốc giờ mới
  for (int s = 0; s < N_SLOTS; s++) {
    if ((pickMask >> s) & 1) {
      upsertSlotSchedule(s, newH, newM, meds[s].dose ? meds[s].dose : 1, true);
    }
  }

  rebuildGroupedScheds();

  schedSel = 0;
  for (int i = 0; i < nSched; i++) {
    if (sched[i].h == newH && sched[i].m == newM) {
      schedSel = i;
      break;
    }
  }

  saveCfg();
  mqttPublishScheduleCreated(newH, newM, pickMask,
                             (schedSel < nSched) ? sched[schedSel].activeMask
                                                 : pickMask);
  toast("Da luu lich!");
  audioPlaySchedSaved(); // 🔊 "Đã lưu lịch uống thuốc thành công."
  goTo(S_SCHED);
}

void commitDispense() {
  int target = 0;
  for (int s = 0; s < N_SLOTS; s++) {
    if ((pickMask >> s) & 1) {
      target = s + 1;
      break;
    }
  }
  if (target > 0) {
    goTo(S_HOME);
    fsmTriggerManualDispense(target);
  } else {
    toast("Chua chon ngan");
    goTo(S_HOME);
  }
}

void confirmDue() {
  FsmState st = fsmGetState();
  if (st == FSM_ALERTING_DUE) {
    fsmOnUserConfirmDose();
  } else if (st == FSM_WAITING_TAKEN) {
    fsmOnUserTakenDone();
  }
}

void testDue() {
  for (int s = 0; s < N_SLOTS; s++) {
    if (slotHasMed(s)) {
      toast("Test: Den gio!");
      fsmTriggerDueAlert(1 << s);
      return;
    }
  }
  toast("Test: Den gio Ngan 1!");
  fsmTriggerDueAlert(1 << 0);
}


// ----------------------------------------------------------------------------
// 8. BỘ XỬ LÝ NHẬP LIỆU (ENCODER & NÚT NHẤN)
// ----------------------------------------------------------------------------
void handleInput() {
  int d = encRead();
  uint8_t p = btnPush.poll();
  uint8_t b = btnBack.poll();

  if (d != 0 || p != 0 || b != 0) {
    if (isTftSleeping()) {
      // Blind Click Protection: Lần chạm/xoay đầu tiên chỉ đánh thức màn hình
      tftWake();
      tftResetActivityTimer();
      return;
    }
    tftResetActivityTimer();
  }

  if (d != 0) {
    audioPlayClick(); // Tiếng click nhẹ khi vặn núm xoay EC11
  }

  switch (screen) {
  case S_HOME: {
    FsmState st = fsmGetState();
    if (st == FSM_ALERTING_DUE) {
      // Đang chuông reo đến giờ -> Bấm K0 hoặc PUSH: Bắt đầu mở khay lấy thuốc
      if (b == 1 || p == 1) {
        fsmOnUserConfirmDose();
      }
    } else if (st == FSM_WAITING_TAKEN) {
      // Khay đang mở chờ lấy -> Bấm K0 hoặc PUSH: Đã lấy xong, khóa về Ngăn 0
      if (b == 1 || p == 1) {
        fsmOnUserTakenDone();
      }
    } else if (st == FSM_ERROR_JAMMED) {
      // Đang kẹt cơ -> Bấm K0: Thử rà tìm lại Safe Home
      if (b == 1) {
        fsmTriggerRecalibrate();
      }
    } else {
      // FSM_SAFE_HOME_IDLE
      if (p == 1) {
        audioPlayClick();
        goTo(S_MENU);
      }
      if (b == 1) {
        audioPlayBeepOk();
        toast("Safe Home: Ngan 0");
      }
      if (b == 2)
        testDue(); // Giữ K0 ở Home: test giả lập đến giờ ngay lập tức
    }
    break;
  }

  // MENU CHÍNH (4 TÁC VỤ TỐI ƯU HÓA)
  case S_MENU: {
    if (d) {
      int old = menuSel;
      menuSel = constrain(menuSel + d, 0, 3); // 4 task chính (0..3)
      if (old != menuSel) {
        drawMenuRow(old);
        drawMenuRow(menuSel);
      }
    }
    if (p == 1) {
      if (menuSel == 0) {
        schedSel = 0;
        audioPlayTaskEnter(
            1); // 🔊 "Đặt lịch uống thuốc." khi nhấn PUSH vào task
        goTo(S_SCHED);
      } // 1. Dat lich uong
      else if (menuSel == 1) {
        medsSel = 0;
        audioPlayTaskEnter(
            2); // 🔊 "Thêm và quản lý thuốc." khi nhấn PUSH vào task
        goTo(S_MEDS);
      } // 2. Them & quan ly thuoc
      else if (menuSel == 2) {
        earlySessionSel = 0;
        earlySchedSel = 0;
        audioPlayTaskEnter(
            3); // 🔊 "Uống thuốc sớm theo buổi." khi nhấn PUSH vào task
        goTo(S_EARLY_SESSION); // 3. Uong thuoc som theo buoi
      } else if (menuSel == 3) {
        histTop = 0;
        audioPlayTaskEnter(
            4); // 🔊 "Lịch sử uống thuốc." khi nhấn PUSH vào task
        goTo(S_HIST);
      } // 4. Xem lich su
    }
    if (p == 2) {
      // Giữ PUSH núm xoay >800ms: Vào chế độ Căn chỉnh khay xoay
      calibSel = 0;
      audioPlayCalibEnter(); // 🔊 "Bắt đầu căn chỉnh khay xoay."
      goTo(S_CALIB);
    }
    if (b) {
      audioPlayBeepCancel();
      goTo(S_HOME); // Bấm K0: Thoát về HOME
    }
    break;
  }

  case S_SCHED: {
    if (d) {
      int old = schedSel, vis = visRows();
      schedSel = constrain(schedSel + d, 0, schedRows() - 1);
      if (old / vis != schedSel / vis)
        drawSched(false);
      else if (old != schedSel) {
        drawSchedRow(old);
        drawSchedRow(schedSel);
      }
    }
    if (p == 1) {
      audioPlayClick();
      if (schedSel < nSched) {
        schedDetailSel = 0;
        goTo(S_SCHED_DETAIL); // Xem chi tiết & bật/tắt từng ngăn của mốc giờ
      } else {
        startSchedEdit(nSched); // Thêm mốc giờ mới
      }
    }
    if (p == 2 && schedSel < nSched) {
      uint8_t delH = sched[schedSel].h;
      uint8_t delM = sched[schedSel].m;
      removeTimeCluster(delH, delM);
      saveCfg();
      mqttPublishScheduleCreated(delH, delM, 0,
                                 0); // Báo Backend xóa mốc giờ này
      schedSel = constrain(schedSel, 0, schedRows() - 1);
      toast("Da xoa moc gio");
      audioPlayBeepOk();
      drawSched(false);
    }
    if (b) {
      audioPlayBeepCancel();
      goTo(S_MENU); // Bấm K0: Thoát về MENU
    }
    break;
  }

  // CHI TIẾT 1 MỐC GIỜ: BẬT/TẮT TỪNG NGĂN TRONG BUỔI ĐÓ, SỬA HOẶC XÓA
  case S_SCHED_DETAIL: {
    if (schedSel < 0 || schedSel >= nSched) {
      goTo(S_SCHED);
      break;
    }
    int assigned[N_SLOTS];
    int assignedCount = 0;
    for (int s = 0; s < N_SLOTS; s++) {
      if ((sched[schedSel].mask >> s) & 1) {
        assigned[assignedCount++] = s;
      }
    }
    int totalRows = assignedCount + 2;

    if (d) {
      int old = schedDetailSel;
      schedDetailSel = constrain(schedDetailSel + d, 0, totalRows - 1);
      if (old != schedDetailSel) {
        drawSchedDetail(false);
      }
    }
    if (p == 1) {
      if (schedDetailSel < assignedCount) {
        // BẬT / TẮT LỊCH UỐNG CỦA NGĂN NÀY TRONG BUỔI ĐÓ!
        int s = assigned[schedDetailSel];
        bool currentActive = (sched[schedSel].activeMask >> s) & 1;
        bool newActive = !currentActive;
        setSlotScheduleActive(s, sched[schedSel].h, sched[schedSel].m,
                              newActive);
        saveCfg();
        mqttPublishScheduleCreated(sched[schedSel].h, sched[schedSel].m,
                                   sched[schedSel].mask,
                                   sched[schedSel].activeMask);

        char msg[40];
        snprintf(msg, sizeof(msg), "Ngan %d: %s", s + 1,
                 newActive ? "Da BAT lich" : "Da TAT lich");
        toast(msg);
        audioPlayBeepOk();
        drawSchedDetail(true);
      } else if (schedDetailSel == assignedCount) {
        // Chỉnh sửa giờ & các ngăn
        audioPlayClick();
        startSchedEdit(schedSel);
      } else {
        // Xóa mốc giờ
        uint8_t delH = sched[schedSel].h, delM = sched[schedSel].m;
        removeTimeCluster(delH, delM);
        saveCfg();
        mqttPublishScheduleCreated(delH, delM, 0, 0);
        schedSel = constrain(schedSel, 0, schedRows() - 1);
        toast("Da xoa moc gio");
        audioPlayBeepOk();
        goTo(S_SCHED);
      }
    }
    if (b) {
      audioPlayBeepCancel();
      goTo(S_SCHED); // Bấm K0: Quay lại danh sách lịch
    }
    break;
  }

  case S_EDIT: {
    int nF = editNF();
    if (d) {
      int range = (editField == 0) ? 24 : (editField == 1 ? 60 : 7);
      editVal[editField] = ((editVal[editField] + d) % range + range) % range;
      drawEdit(false);
    }
    if (p == 1) {
      if (editField < nF - 1) {
        editField++;
        audioPlayClick();
        drawEdit(false);
      } else {
        audioPlayBeepOk();
        saveEdit();
      }
    }
    if (b) {
      audioPlayBeepCancel();
      goTo(editTarget == ET_CLOCK ? S_MENU : S_SCHED); // Bấm K0: Thoát
    }
    break;
  }

  case S_PICK: {
    if (d) {
      int old = pickSel;
      pickSel = ((pickSel + d) % 8 + 8) % 8;
      drawPickCell(old);
      drawPickCell(pickSel);
      drawPickInfo();
    }
    if (p == 1) {
      if (pickSel < N_SLOTS) {
        if (!slotUsable(pickSel)) {
          toast((pickMode == PK_DISP && slotHasMed(pickSel)) ? "Het thuoc"
                                                             : "Ngan trong");
          audioPlayBeepErr();
          render(true);
        } else if (pickMode == PK_SCHED && !((pickMask >> pickSel) & 1)) {
          // Bật chọn ngăn: Kiểm tra xem ngăn này đã đạt tối đa 4 cữ chưa
          bool isExistingInThisTime = false;
          for (int i = 0; i < nSlotSched; i++) {
            if (slotSched[i].slot == pickSel &&
                slotSched[i].hour == editVal[0] &&
                slotSched[i].minute == editVal[1]) {
              isExistingInThisTime = true;
              break;
            }
          }
          if (!isExistingInThisTime &&
              countSlotSchedules(pickSel) >= MAX_SCHEDS_PER_SLOT) {
            char msg[32];
            snprintf(msg, sizeof(msg), "N%d da du %d cu!", pickSel + 1,
                     MAX_SCHEDS_PER_SLOT);
            toast(msg);
            audioPlayBeepErr();
            render(true);
          } else {
            pickMask ^= (1 << pickSel);
            audioPlayClick();
            drawPickCell(pickSel);
            drawPickCell(N_SLOTS);
            drawPickInfo();
          }
        } else {
          pickMask ^= (1 << pickSel);
          audioPlayClick();
          drawPickCell(pickSel);
          drawPickCell(N_SLOTS);
          drawPickInfo();
        }
      } else {
        if (!pickMask) {
          toast("Chua chon ngan");
          audioPlayBeepErr();
          render(true);
        } else if (pickMode == PK_SCHED) {
          commitSched();
        } else {
          commitDispense();
        }
      }
    }
    if (b) {
      audioPlayBeepCancel();
      goTo(pickMode == PK_SCHED ? S_SCHED : S_MENU); // Bấm K0: Thoát
    }
    break;
  }

  case S_MEDS: {
    if (d) {
      int old = medsSel;
      medsSel = constrain(medsSel + d, 0, N_SLOTS - 1);
      if (old != medsSel) {
        drawMedsRow(old);
        drawMedsRow(medsSel);
      }
    }
    if (p == 1) {
      // CLICK PUSH: VÀO THẲNG CHẾ ĐỘ NẠP THÊM BAO NHIÊU VIÊN!
      curSlot = medsSel;
      refillAddQty = 5; // Mặc định +5 viên
      Serial.printf(
          "[MAIN] Nạp thuốc: Đã chọn Ngăn %d -> Xoay khay đến Ngăn %d...\n",
          curSlot + 1, curSlot + 1);
      char msg[36];
      snprintf(msg, sizeof(msg), "Mo Ngan %d nap thuoc...", curSlot + 1);
      toast(msg);
      motorRotateToSlot(curSlot + 1);
      audioPlayRefillSlot(curSlot + 1); // 🔊 "Đang mở ngăn số X để nạp thuốc,
                                        // xin hãy kiểm tra đúng loại thuốc"
      mqttPublishRefillReady(curSlot + 1);
      goTo(S_REFILL_QTY);
    }
    if (p == 2) {
      // GIỮ PUSH >800ms: VÀO CẤU HÌNH CHI TIẾT NGĂN (ĐỔI TÊN THUỐC, LIỀU DÙNG
      // HOẶC XÓA)
      curSlot = medsSel;
      medRow = 0;
      audioPlayClick();
      goTo(S_MED);
    }
    if (b) {
      if (currentMotorSlot != 0) {
        motorRotateToSlot(0);
        mqttPublishHomeReached();
        audioPlayRefillCancel();
      } else {
        audioPlayBeepCancel();
      }
      goTo(S_MENU); // Bấm K0: Thoát về MENU
    }
    break;
  }

  // BƯỚC 1: CHỌN SỐ LƯỢNG VIÊN NẠP THÊM VÀO NGĂN
  case S_REFILL_QTY: {
    if (d) {
      refillAddQty = constrain(refillAddQty + d, 1, 99);
      drawRefillQty(false);
    }
    if (p == 1) {
      audioPlayClick();
      goTo(S_REFILL_CONFIRM); // Chuyển sang Bước 2: Xác nhận đã bỏ thuốc
    }
    if (b) {
      Serial.println(
          "[MAIN] Hủy nạp thuốc -> Tự động đóng khay về Safe Home...");
      if (currentMotorSlot != 0) {
        motorRotateToSlot(0);
        mqttPublishHomeReached();
        audioPlayRefillCancel(); // 🔊 "Đã hủy nạp thuốc, khay đã khóa về vị trí
                                 // an toàn."
      } else {
        audioPlayBeepCancel();
      }
      toast("Da huy -> Khoa Ngan 0");
      goTo(S_MEDS);
    }
    break;
  }

  // BƯỚC 2: XÁC NHẬN ĐÃ BỎ THUỐC VÀO NGĂN RỒI TỰ QUAY LẠI NGĂN 0
  case S_REFILL_CONFIRM: {
    if (p == 1) {
      // 1. Cập nhật số lượng thuốc trong ngăn
      meds[curSlot].qty = constrain(meds[curSlot].qty + refillAddQty, 0, 255);
      if (strlen(meds[curSlot].name) == 0) {
        snprintf(meds[curSlot].name, sizeof(meds[curSlot].name), "Thuoc %d",
                 curSlot + 1);
      }
      saveCfg();

      // 2. Bắn sự kiện lên Backend qua MQTT
      mqttPublishRefillDone(curSlot + 1, meds[curSlot].qty, refillAddQty);

      // 3. TỰ ĐỘNG XOAY KHAY ĐÓNG VỀ NGĂN 0 SAFE HOME!
      Serial.printf("[MAIN] Xác nhận đã bỏ thuốc Ngăn %d (+%d viên, Tổng: %d "
                    "viên) -> Tự động đóng về Ngăn 0 Safe Home\n",
                    curSlot + 1, refillAddQty, meds[curSlot].qty);
      motorRotateToSlot(0);
      mqttPublishHomeReached();
      audioPlayRefillDone(); // "Nạp thuốc thành công, khay đã khóa an toàn"

      char msg[40];
      snprintf(msg, sizeof(msg), "+%dv -> Da khoa Ngan 0!", refillAddQty);
      toast(msg);

      goTo(S_MEDS); // Quay về danh sách ngăn thuốc
    }
    if (b) {
      Serial.println(
          "[MAIN] Hủy xác nhận nạp thuốc -> Tự động đóng khay về Safe Home...");
      if (currentMotorSlot != 0) {
        motorRotateToSlot(0);
        mqttPublishHomeReached();
        audioPlayRefillCancel(); // 🔊 "Đã hủy nạp thuốc, khay đã khóa về vị trí
                                 // an toàn."
      } else {
        audioPlayBeepCancel();
      }
      toast("Da huy -> Khoa Ngan 0");
      goTo(S_MEDS);
    }
    break;
  }

  // CHẾ ĐỘ NẠP THUỐC QUA WEB DASHBOARD (S_REFILL_WEB)
  // Khi người dùng bấm PUSH hoặc K0 ngay trên hộp thuốc -> Khóa an toàn về Ngăn
  // 0 Safe Home
  case S_REFILL_WEB: {
    if (p == 1 || b) {
      Serial.println(
          "[MAIN] Dong khay nap thuoc Web -> Quay ve Ngay 0 Safe Home...");
      if (currentMotorSlot != 0) {
        motorRotateToSlot(0);
        mqttPublishHomeReached();
        audioPlayRefillDone();
      }
      toast("Da khoa ve Safe Home");
      goTo(S_HOME);
    }
    break;
  }

  // TÁC VỤ 3: UỐNG THUỐC SỚM THEO BUỔI (BƯỚC 1: CHỌN BUỔI TRONG NGÀY: SÁNG,
  // TRƯA, CHIỀU, TỐI)
  case S_EARLY_SESSION: {
    if (d) {
      int old = earlySessionSel;
      earlySessionSel =
          constrain(earlySessionSel + d, 0,
                    3); // 4 Buổi: 0: Sáng, 1: Trưa, 2: Chiều, 3: Tối
      if (old != earlySessionSel) {
        drawEarlySession(false);
      }
    }
    if (p == 1) {
      uint8_t mask = getSessionMask(earlySessionSel);
      int nSlots = __builtin_popcount(mask);
      if (nSlots == 0) {
        toast("Buoi nay chua co thuoc!");
        audioPlayEarlyNoDose(); // 🔊 "Hiện tại không có cữ thuốc nào cần uống."
      } else if (isSessionTakenToday(earlySessionSel)) {
        toast("Buoi nay da uong roi!");
        audioPlayBeepErr();
      } else {
        // Nạp danh sách các ngăn thuốc trong buổi này (1-indexed: 1..7)
        earlySlotCount = 0;
        for (int s = 0; s < N_SLOTS; s++) {
          if ((mask >> s) & 1) {
            earlySlots[earlySlotCount++] = s + 1;
          }
        }
        earlyCurrentStep = 0;
        audioPlayEarlySession(
            earlySessionSel); // 🔊 "Lấy thuốc cữ sáng/trưa/chiều/tối hôm nay."
        goTo(S_EARLY_CONFIRM);
      }
    }
    if (b) {
      audioPlayBeepCancel();
      goTo(S_MENU); // Bấm K0: Thoát về MENU
    }
    break;
  }

  // TÁC VỤ 3: UỐNG THUỐC SỚM THEO BUỔI (BƯỚC 2: HIỂN THỊ CÁC NGĂN, LIỀU DÙNG &
  // NÚT XÁC NHẬN)
  case S_EARLY_CONFIRM: {
    if (p == 1) {
      if (isSessionTakenToday(earlySessionSel)) {
        toast("Buoi nay da uong roi!");
        audioPlayBeepErr();
      } else if (earlySlotCount == 0) {
        toast("Buoi nay chua co ngan nao!");
        audioPlayEarlyNoDose();
      } else {
        earlyCurrentStep = 0;
        int firstSlot = earlySlots[0];
        Serial.printf(
            "[MAIN] Xác nhận lấy %s -> Xoay tới Ngăn đầu tiên (%d) [1/%d]...\n",
            SESSION_NAMES[earlySessionSel], firstSlot, earlySlotCount);
        motorRotateToSlot(firstSlot);
        audioPlayDispenseSlot(firstSlot); // 🔊 "Đang mở ngăn số X, xin hãy lấy
                                          // thuốc và uống đúng liều lượng."
        char msg[36];
        snprintf(msg, sizeof(msg), "Mo Ngan %d lay thuoc...", firstSlot);
        toast(msg);
        goTo(S_EARLY_DISPENSE);
      }
    }
    if (b) {
      audioPlayBeepCancel();
      goTo(S_EARLY_SESSION); // Bấm K0: Quay lại danh sách chọn buổi
    }
    break;
  }

  // TÁC VỤ 3: UỐNG THUỐC SỚM THEO BUỔI (BƯỚC 3: KHAY XOAY TỪNG NGĂN, BẤM XÁC
  // NHẬN -> QUA NGĂN TIẾP THEO)
  case S_EARLY_DISPENSE: {
    if (p == 1) {
      // 1. Xác nhận đã lấy thuốc ở ngăn hiện tại:
      if (earlyCurrentStep < earlySlotCount) {
        int curSlotNum = earlySlots[earlyCurrentStep];
        int curSlotIdx = curSlotNum - 1; // 0-indexed

        // Trừ thuốc theo liều dùng của ngăn
        consumeDose(curSlotIdx);
        addHist(curSlotIdx, H_MANUAL); // Ghi nhận nhật ký lấy thuốc
        uint8_t dQty = meds[curSlotIdx].dose ? meds[curSlotIdx].dose : 1;
        mqttPublishTaken(curSlotNum, dQty); // Báo cáo lên Backend MQTT
        saveCfg();

        Serial.printf("[MAIN] Đã xác nhận lấy thuốc Ngăn %d (%d viên). Tiến "
                      "trình: %d/%d (%s)\n",
                      curSlotNum, dQty, earlyCurrentStep + 1, earlySlotCount,
                      SESSION_NAMES[earlySessionSel]);
      }

      earlyCurrentStep++;

      if (earlyCurrentStep < earlySlotCount) {
        // Vẫn còn ngăn tiếp theo trong buổi này -> Tự động xoay sang ngăn tiếp
        // theo
        int nextSlotNum = earlySlots[earlyCurrentStep];
        Serial.printf("[MAIN] Xoay sang ngăn tiếp theo: Ngăn %d (%d/%d)...\n",
                      nextSlotNum, earlyCurrentStep + 1, earlySlotCount);
        audioPlayDispenseNext(); // 🔊 "Vui lòng lấy tiếp ngăn thuốc tiếp theo."
        unsigned long startWait = millis();
        while (audioIsPlaying() && (millis() - startWait < 2200)) {
          vTaskDelay(pdMS_TO_TICKS(50));
        }
        vTaskDelay(pdMS_TO_TICKS(200));
        motorRotateToSlot(nextSlotNum);
        audioPlayDispenseSlot(
            nextSlotNum); // 🔊 "Đang mở ngăn số X, xin hãy lấy thuốc và uống
                          // đúng liều lượng."
        char msg[36];
        snprintf(msg, sizeof(msg), "Chuyen sang Ngan %d...", nextSlotNum);
        toast(msg);
        drawEarlyDispense(true); // Cập nhật màn hình TFT sang ngăn mới
      } else {
        // ĐÃ HOÀN TẤT TOÀN BỘ CÁC NGĂN CỦA BUỔI ĐÓ!
        Serial.printf("[MAIN] Hoàn thành lấy thuốc toàn bộ %s! Khóa khay về "
                      "Ngăn 0 Safe Home.\n",
                      SESSION_NAMES[earlySessionSel]);
        motorRotateToSlot(0);
        markSessionTakenToday(
            earlySessionSel); // Đánh dấu BUỔI NÀY ĐÃ UỐNG trong ngày hôm nay!
        mqttPublishHomeReached();
        uint8_t sMask = getSessionMask(earlySessionSel);
        mqttPublishEarlyTaken(earlySessionSel * 6, 0, sMask);
        audioPlayTakenSuccess(); // 🔊 "Bạn đã uống thuốc thành công, chúc bạn
                                 // một ngày nhiều sức khỏe."
        char doneMsg[40];
        snprintf(doneMsg, sizeof(doneMsg), "Da uong xong %s! Khoa Ngan 0",
                 SESSION_NAMES[earlySessionSel]);
        toast(doneMsg);
        goTo(S_HOME);
      }
    }
    if (b) {
      // Bấm K0: Hủy giữa chừng, tự động xoay khay về Ngăn 0 Safe Home an toàn
      Serial.println("[MAIN] Người dùng bấm K0 dừng lấy thuốc -> Đóng khay về "
                     "Safe Home...");
      if (currentMotorSlot != 0) {
        motorRotateToSlot(0);
        mqttPublishHomeReached();
        audioPlayRefillCancel();
      } else {
        audioPlayBeepCancel();
      }
      toast("Da dung -> Khoa Ngan 0");
      goTo(S_HOME);
    }
    break;
  }

  case S_MED: {
    if (d) {
      int old = medRow;
      medRow = constrain(medRow + d, 0, 3);
      if (old != medRow) {
        drawMedRow(old);
        drawMedRow(medRow);
      }
    }
    if (p == 1) {
      if (medRow == 0)
        startName();
      else if (!slotHasMed(curSlot)) {
        toast("Nhap ten truoc");
        render(true);
      } else if (medRow == 1)
        startNum(NT_QTY);
      else if (medRow == 2)
        startNum(NT_DOSE);
      else {
        // Người dùng chọn dòng 4: [XOA NGAN NAY]
        // BƯỚC 1: Xoay ngay khay ra hướng 6h để lấy thuốc ra
        Serial.printf(
            "[MAIN] Chon Xoa ngan: Xoay khay den Ngan %d de lay thuoc ra...\n",
            curSlot + 1);
        motorRotateToSlot(curSlot + 1);
        audioPlayClearSlot(
            curSlot +
            1); // 🔊 "Đang mở ngăn số X, xin hãy lấy hết thuốc ra ngoài."
        goTo(S_CLEAR_CONFIRM); // Chuyển sang màn hình xác nhận đã lấy thuốc ra
      }
    }
    if (b) {
      // Khi nhấn K0 quay lại: tự động khóa khay về Slot 0 Safe Home an toàn
      if (currentMotorSlot != 0) {
        Serial.println(
            "[MAIN] Thoát khỏi nạp ngăn -> Đóng khay về Slot 0 Safe Home...");
        motorRotateToSlot(0);
        mqttPublishHomeReached();
        audioPlayRefillDone(); // "Nạp thuốc thành công, khay đã khóa an toàn"
        toast("Da khoa ve Safe Home");
      } else {
        audioPlayBeepCancel();
      }
      goTo(S_MEDS); // Bấm K0: Thoát về S_MEDS
    }
    break;
  }

  // MÀN HÌNH XÁC NHẬN XÓA NGĂN THUỐC & LẤY THUỐC RA KHỎI KHAY
  case S_CLEAR_CONFIRM: {
    if (p == 1) {
      Serial.printf("[MAIN] Xác nhận đã lấy thuốc ra -> Xóa Ngăn %d và toàn bộ "
                    "lịch liên quan...\n",
                    curSlot + 1);

      // 1. Xóa thuốc trong RAM
      memset(&meds[curSlot], 0, sizeof(Med));
      meds[curSlot].dose = 1;
      dueMask &= ~(1 << curSlot);

      // 2. Xóa toàn bộ các lịch liên quan đến ngăn này trong bộ nhớ
      removeSlotSchedulesBySlot(curSlot);

      // 3. Lưu cấu hình vào Flash NVS
      saveCfg();

      // 4. Đồng bộ bộ nhớ với Backend qua MQTT
      mqttPublishSlotCleared(curSlot + 1);
      mqttPublishRefillDone(curSlot + 1, 0, 0);

      // 5. Tự động xoay đóng khay về Ngăn 0 Safe Home an toàn
      motorRotateToSlot(0);
      mqttPublishHomeReached();
      audioPlayClearDone(); // 🔊 "Xác nhận xóa, dữ liệu đã được dọn, khay trở
                            // về vị trí an toàn."

      toast("Da xoa thuoc & lich -> Khoa 0");
      goTo(S_MEDS);
    }
    if (b) {
      Serial.println(
          "[MAIN] Hủy xóa ngăn thuốc -> Tự động đóng khay về Safe Home...");
      if (currentMotorSlot != 0) {
        motorRotateToSlot(0);
        mqttPublishHomeReached();
        audioPlayRefillCancel();
      } else {
        audioPlayBeepCancel();
      }
      toast("Da huy xoa -> Khoa 0");
      goTo(S_MED);
    }
    break;
  }

  case S_NAME: {
    if (d) {
      nameIdx[namePos] = ((nameIdx[namePos] + d) % CS_N + CS_N) % CS_N;
      drawNameBox(namePos);
    }
    if (p == 1) {
      if (namePos < NAME_LEN - 1) {
        int old = namePos;
        namePos++;
        audioPlayClick();
        drawNameBox(old);
        drawName(false);
      } else {
        audioPlayBeepOk();
        saveName();
      }
    }
    if (p == 2) {
      audioPlayBeepOk();
      saveName();
    }
    if (b) {
      audioPlayBeepCancel();
      goTo(S_MED); // Bấm K0: Thoát
    }
    break;
  }

  case S_NUM: {
    if (d) {
      if (d >= 2 || d <= -2)
        d *= 4;
      numVal = constrain(numVal + d, numMin, numMax);
      drawNum(false);
    }
    if (p == 1) {
      audioPlayBeepOk();
      saveNum();
    }
    if (b) {
      audioPlayBeepCancel();
      goTo(S_MED); // Bấm K0: Thoát
    }
    break;
  }

  case S_HIST: {
    if (d) {
      int vis = visRows();
      int mx = histCount > vis ? histCount - vis : 0;
      int old = histTop;
      histTop = constrain(histTop + d, 0, mx);
      if (old != histTop)
        drawHist(false);
    }
    if (b) {
      audioPlayBeepCancel();
      goTo(S_MENU); // Bấm K0: Thoát
    }
    break;
  }

  // TÁC VỤ AUTO HIỆU CHUẨN MỨC 0 (GẶP VẠCH ĐEN LÀ TỰ ĐỘNG NHẬN NGĂN 0)
  case S_CALIB: {
    if (d) {
      calibSel = constrain(calibSel + d, 0, 2); // 3 tùy chọn (0..2)
      drawCalib(false);
    }
    if (p == 1) {
      if (calibSel == 0) {
        toast("Dang Auto quay tim vach...");
        // autoHoming(): Quay tới gặp vạch đen là DỪNG và TỰ ĐỘNG LƯU Ngăn 0 vào
        // Flash
        if (autoHoming()) {
          toast("Da gap vach -> Nhan Ngan 0!");
          audioPlayBeepOk();
        } else {
          toast("Loi: Khong thay vach den!");
          audioPlayBeepErr();
        }
        drawCalib(true);
      } else if (calibSel == 1) {
        toast("Dang xoay ve Ngan 0...");
        motorRotateToSlot(0);
        toast("Da ve Ngan 0 Safe Home");
        audioPlayBeepOk();
        drawCalib(false);
      } else if (calibSel == 2) {
        audioPlayBeepCancel();
        goTo(S_MENU);
      }
    }
    if (b) {
      audioPlayBeepCancel();
      goTo(S_MENU); // Bấm K0: Luôn thoát về MENU
    }
    break;
  }

  // MÀN HÌNH CẢNH BÁO CỮ THUỐC CHUYÊN BIỆT & ĐẾM NGƯỢC TIMEOUT
  case S_DUE_ALERT: {
    FsmState st = fsmGetState();
    if (st == FSM_ALERTING_DUE) {
      // Đang chuông reo cữ thuốc -> Bấm PUSH hoặc K0 để mở khay uống thuốc
      if (p == 1 || b == 1) {
        Serial.println(
            "[MAIN] Xác nhận uống thuốc cữ hẹn -> Bắt đầu xoay mở khay...");
        fsmOnUserConfirmDose();
      }
    } else if (st == FSM_WAITING_TAKEN) {
      // Đang mở ngăn chờ lấy -> Bấm PUSH hoặc K0 để xác nhận đã lấy thuốc
      if (p == 1 || b == 1) {
        Serial.println("[MAIN] Xác nhận đã lấy thuốc -> Xoay tiếp hoặc khóa về "
                       "Safe Home...");
        fsmOnUserTakenDone();
      }
    }
    break;
  }
  }
}

// ----------------------------------------------------------------------------
// 9. SETUP & LOOP
// ----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println(
      "\n==================================================================");
  Serial.println("   🏥 MEDBOX PILL DISPENSER — MODULAR DISPLAY & 4-TASK MENU");
  Serial.println("   Màn hình: ST7789 2.4\" 320x240 | Bo mạch: ESP32-S3 N16R8");
  Serial.println(
      "==================================================================");

  // Cấp nguồn đèn nền BLK với Drive Strength cực đại
  pinMode(PIN_TFT_BLK, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)PIN_TFT_BLK, GPIO_DRIVE_CAP_3);
  digitalWrite(PIN_TFT_BLK, HIGH);

  // Khởi tạo các chân Động cơ bước & Cảm biến quang TCRT5000
  motorInit();

  // Khởi tạo núm xoay EC11 & Nút K0
  pinMode(PIN_EC11_CLK, INPUT);
  pinMode(PIN_EC11_DT, INPUT);
  btnPush.begin(PIN_EC11_SW);
  btnBack.begin(PIN_BTN_K0);

  encState = (digitalRead(PIN_EC11_CLK) << 1) | digitalRead(PIN_EC11_DT);
  attachInterrupt(digitalPinToInterrupt(PIN_EC11_CLK), encISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_EC11_DT), encISR, CHANGE);
  // Khởi tạo cảm biến nhiệt độ & độ ẩm phòng thực tế DHT11
  dhtSensorInit();

  for (int i = 0; i < N_SLOTS; i++)
    meds[i].dose = 1;
  loadData();

  // Khởi tạo phân hệ hiển thị tách biệt
  displayInit();

  Serial.printf("[TFT] Man hinh ngang: %d x %d\n", W, H);
  lastAbsMin = absSec() / 60;

  // Hiển thị ngay giao diện chính để màn hình sáng rõ ngay khi bật nguồn
  render(true);

  // Khởi tạo phân hệ Loa I2S MAX98357A & Flash LittleFS
  audioInit();

  // Khởi tạo Máy trạng thái FSM lõi y tế (Tự động rà tìm Slot 0 Safe Home & phát Intro)
  fsmInit();

  // Khởi tạo phân hệ truyền thông mạng Wi-Fi & MQTT Client
  mqttManagerInit();

  // Cập nhật lại giao diện sau khi toàn bộ ngoại vi đã sẵn sàng
  render(true);
}

void loop() {
  handleInput();
  fsmUpdate();
  tickSchedule();
  dhtSensorUpdate();
  mqttManagerUpdate();

  // 1. Quản lý tự động chuyển sang chế độ ngủ cho màn hình TFT (TFT Sleep Mode)
  // Sau 60s không tương tác ở màn chính -> Tắt đèn nền & đưa ST7789 vào Sleep In
  tftCheckSleepTimeout(screen, fsmGetState());

  if (!isTftSleeping()) {
    if (homeDirty) {
      homeDirty = false;
      if (screen == S_HOME) {
        drawHome(false);
      } else if (screen == S_SCHED) {
        drawSched(false);
      } else if (screen == S_SCHED_DETAIL) {
        drawSchedDetail(false);
      }
    }

    // Cập nhật đồng hồ Trang chủ khi sang phút mới
    static uint32_t lastHomeMin = 0xFFFFFFFF;
    uint32_t curMin = absSec() / 60;
    if (screen == S_HOME && curMin != lastHomeMin) {
      lastHomeMin = curMin;
      drawHomeClock();
      if (wifiState != WIFI_ST_CONNECTED) {
        drawHomeHeader();
      }
    }
  }

  // Cập nhật đếm ngược thời gian thực trên màn hình cảnh báo cữ thuốc S_DUE_ALERT
  if (screen == S_DUE_ALERT) {
    if (fsmGetState() == FSM_SAFE_HOME_IDLE) {
      goTo(S_HOME);
    } else {
      static uint32_t lastAlertSec = 0xFFFFFFFF;
      static FsmState lastAlertState = FSM_HOMING_SEARCH;
      FsmState curState = fsmGetState();
      uint32_t curRemain = fsmGetRemainingTimeoutSec();
      if (curRemain != lastAlertSec || curState != lastAlertState) {
        lastAlertSec = curRemain;
        lastAlertState = curState;
        drawDueAlert(false);
      }
    }
  }

  // Tự động nhận Ngăn 0 thời gian thực khi cảm biến chạm vạch đen trong màn hình hiệu chuẩn
  if (screen == S_CALIB) {
    static bool lastSensState = false;
    static unsigned long lastSensCheck = 0;
    if (millis() - lastSensCheck > 100) {
      lastSensCheck = millis();
      bool curSens = isSensorAtHome();
      if (curSens != lastSensState) {
        lastSensState = curSens;
        if (curSens) {
          // Cảm biến vừa chạm vạch đen -> TỰ ĐỘNG nhận ngay là Ngăn 0 & lưu Flash NVS
          currentMotorSlot = 0;
          homeOffset = 0;
          jogDelta = 0;
          saveCfg();
          toast("Auto nhan Ngan 0 tai vach!");
        }
        drawCalib(false);
      }
    }
  }
}
