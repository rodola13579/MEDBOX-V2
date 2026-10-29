/**
 * ============================================================================
 * MEDBOX PILL DISPENSER — DISPLAY UI SUBSYSTEM IMPLEMENTATION
 * Màn hình: ST7789 2.4" IPS (Nằm ngang 320x240, TFT_eSPI)
 * ============================================================================
 */

#include "display_ui.h"
#include "config.h"
#include "dht_sensor.h"
#include "fsm_controller.h"
#include "motor_control.h"

// ----------------------------------------------------------------------------
// 1. BIẾN BỐ CỤC & MENU
// ----------------------------------------------------------------------------
TFT_eSPI tft = TFT_eSPI();

int W = 320, H = 240;
int UI = 2, HDR_H = 24, BIG = 7;
int clockY, cardY, cardH, tileY, tileH, cellW, tileW;

// 4 tác vụ chính được tối ưu hóa cho bệnh nhân
const char *MENU_ITEMS[4] = {"1. Dat lich uong thuoc", "2. Nap & Quan ly thuoc",
                             "3. Uong thuoc som", "4. Xem lich su uong"};

// ----------------------------------------------------------------------------
// 2. KHỞI TẠO HIỂN THỊ & TÍNH TOÁN BỐ CỤC
// ----------------------------------------------------------------------------
void calcLayout() {
  HDR_H = 12 * UI;           // Thanh tiêu đề (24px)
  clockY = HDR_H + 3 * UI;   // Vị trí đồng hồ (30px)
  cardY = clockY + 54 + 4;   // Vị trí Khung Chữ Thập Y Tế (88px)
  cardH = 52;                // Chiều cao Khung Chữ Thập Y Tế (52px)
  tileY = cardY + cardH + 6; // Vị trí hàng 7 ngăn thuốc (146px)
  tileH = 54;                // Chiều cao ô thuốc (54px)
  cellW = W / N_SLOTS;       // 45px / ngăn
  tileW = cellW - 4 * UI;    // 37px
}


// ----------------------------------------------------------------------------
// 2.1. QUẢN LÝ CHẾ ĐỘ NGỦ MÀN HÌNH TFT (TFT SLEEP MODE)
// ----------------------------------------------------------------------------
static bool tftIsSleeping = false;
static unsigned long lastTftActivityMs = 0;

bool isTftSleeping() {
  return tftIsSleeping;
}

void tftResetActivityTimer() {
  lastTftActivityMs = millis();
  if (tftIsSleeping) {
    tftWake();
  }
}

void tftSleep() {
  if (!tftIsSleeping) {
    tftIsSleeping = true;
    // 1. Tắt đèn nền qua PWM và kéo LOW chân BLK
    ledcWrite(TFT_BLK_PWM_CH, TFT_BRIGHTNESS_OFF);
    digitalWrite(PIN_TFT_BLK, LOW);
    // 2. Gửi lệnh đưa IC ST7789 vào chế độ ngủ sâu (Sleep In)
    tft.writecommand(0x10); // ST7789_SLPIN
    Serial.println("[TFT] Màn hình chuyển sang chế độ ngủ (TFT Sleep Mode).");
  }
}

void tftWake() {
  if (tftIsSleeping) {
    tftIsSleeping = false;
    // 1. Gửi lệnh đánh thức IC ST7789 (Sleep Out)
    tft.writecommand(0x11); // ST7789_SLPOUT
    delay(10);
    // 2. Bật lại đèn nền sáng 100%
    ledcWrite(TFT_BLK_PWM_CH, TFT_BRIGHTNESS_MAX);
    digitalWrite(PIN_TFT_BLK, HIGH);
    // 3. Vẽ lại nội dung giao diện hiện tại
    render(true);
    Serial.println("[TFT] Đánh thức màn hình sáng trở lại 100%.");
  }
}

void tftCheckSleepTimeout(Screen curScreen, int curFsm) {
  if (!tftIsSleeping) {
    // Chỉ cho phép ngủ khi đang ở màn hình chính S_HOME và FSM rảnh rỗi (SAFE_HOME_IDLE).
    // Tuyệt đối KHÔNG ngủ khi đang có cữ thuốc báo động, đang mở ngăn hoặc nạp thuốc!
    if (curScreen == S_HOME && curFsm == (int)FSM_SAFE_HOME_IDLE) {
      if (millis() - lastTftActivityMs >= TFT_SLEEP_TIMEOUT_MS) {
        tftSleep();
      }
    } else {
      // Khi đang ở màn hình khác hoặc đang bận -> liên tục làm mới bộ đếm
      lastTftActivityMs = millis();
    }
  }
}

void displayInit() {
  // 1. Tạo xung Reset phần cứng dứt khoát trên PIN_TFT_RST (GPIO 10) để giải phóng IC ST7789
  pinMode(PIN_TFT_RST, OUTPUT);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(10);
  digitalWrite(PIN_TFT_RST, LOW);
  delay(20);
  digitalWrite(PIN_TFT_RST, HIGH);
  delay(60);

  // 2. Khởi tạo IC ST7789 qua bus SPI
  tft.init();
  tft.setRotation(1);       // NẰM NGANG 320x240
  tft.invertDisplay(false); // Tắt đảo ngược màu phần cứng ST7789
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.fillScreen(C_BG);

  W = tft.width();
  H = tft.height();
  UI = (W >= 300) ? 2 : 1;
  BIG = (W >= 300) ? 7 : 4;
  calcLayout();

  // 3. Cấp nguồn đèn nền BLK luôn sáng 100% bằng LEDC PWM và GPIO Drive Cap 3
  ledcSetup(TFT_BLK_PWM_CH, TFT_BLK_PWM_FREQ, TFT_BLK_PWM_RES);
  ledcAttachPin(PIN_TFT_BLK, TFT_BLK_PWM_CH);
  ledcWrite(TFT_BLK_PWM_CH, TFT_BRIGHTNESS_MAX);
  pinMode(PIN_TFT_BLK, OUTPUT);
  gpio_set_drive_capability((gpio_num_t)PIN_TFT_BLK, GPIO_DRIVE_CAP_3);
  digitalWrite(PIN_TFT_BLK, HIGH);

  lastTftActivityMs = millis();
  tftIsSleeping = false;
}

// ----------------------------------------------------------------------------
// 3. TIỆN ÍCH ĐỒ HỌA CƠ BẢN
// ----------------------------------------------------------------------------
void drawHeader(const char *title) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.fillRoundRect(2, 2, W - 4, HDR_H, 6, C_HDR);
  tft.drawRoundRect(2, 2, W - 4, HDR_H, 6, C_BORDER);
  tft.setTextColor(C_ACC, C_HDR);
  tft.drawString(title, 8, 4, 2);
}

void drawHint(const char *s) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.fillRect(0, H - 10 * UI, W, 10 * UI, C_BG);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(s, 6, H - 10 * UI + 4, 1);
}

void drawRowAt(int row, bool sel, uint16_t fg, const char *txt) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  int rh = 12 * UI, y = HDR_H + row * rh;
  uint16_t bg = sel ? C_SEL : C_BG;
  tft.fillRoundRect(4, y, W - 8, rh - 2, 4, bg);
  if (!txt)
    return;
  tft.setTextColor(fg, bg);
  tft.drawString(sel ? "> " : "  ", 12, y + (rh - 16) / 2, 2);
  tft.drawString(txt, 28, y + (rh - 16) / 2, 2);
}

void printCentered(const char *s, int y) {
  tft.setTextSize(1);
  tft.drawCentreString(s, W / 2, y, 2);
}

void toast(const char *msg) {
  tft.setTextSize(1);
  int bw = W - 30 * UI, bh = 28 * UI;
  int x = (W - bw) / 2, y = (H - bh) / 2;
  tft.fillRoundRect(x, y, bw, bh, 8, C_CARD);
  tft.drawRoundRect(x, y, bw, bh, 8, C_ACC);
  tft.setTextColor(C_FG, C_CARD);
  tft.drawCentreString(msg, W / 2, y + (bh - 16) / 2, 2);
  delay(850);
}

// ----------------------------------------------------------------------------
// 4. BIỂU TƯỢNG VECTOR: WI-FI & CHỮ THẬP Y TẾ
// ----------------------------------------------------------------------------
void drawWifiIndicator(int x, int y, WifiState state) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  // 1. Cột sóng đồ họa (4 vạch)
  for (int b = 0; b < 4; b++) {
    int barH = 3 + b * 3; // 3, 6, 9, 12px
    int barX = x + b * 4;
    int barY = y + 12 - barH;
    uint16_t barCol = C_GREY;
    if (state == WIFI_ST_CONNECTED) {
      barCol = C_OK;
    } else if (state == WIFI_ST_CONNECTING) {
      barCol = (b < 2) ? C_WARN : C_GREY;
    } else if (state == WIFI_ST_AP) {
      barCol = (b % 2 == 0) ? C_WIFI_AP : C_GREY;
    }
    tft.fillRect(barX, barY, 2, barH, barCol);
  }

  // 2. Badge trạng thái chữ
  int badgeX = x + 18;
  int badgeW = 76;
  int badgeH = 16;
  int badgeY = y - 2;

  switch (state) {
  case WIFI_ST_CONNECTED:
    tft.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, 0x02E5);
    tft.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 4, C_OK);
    tft.setTextColor(C_OK, 0x02E5);
    tft.drawString("WiFi: OK", badgeX + 6, badgeY + 1, 2);
    break;

  case WIFI_ST_CONNECTING:
    tft.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, 0x21E0);
    tft.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 4, C_WARN);
    tft.setTextColor(C_WARN, 0x21E0);
    tft.drawString("DANG NOI", badgeX + 4, badgeY + 1, 2);
    break;

  case WIFI_ST_AP:
    tft.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, 0x2106);
    tft.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 4, C_WIFI_AP);
    tft.setTextColor(C_WIFI_AP, 0x2106);
    tft.drawString("AP: CONFIG", badgeX + 2, badgeY + 1, 2);
    break;

  case WIFI_ST_OFF:
  default:
    tft.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, C_CARD);
    tft.drawRoundRect(badgeX, badgeY, badgeW, badgeH, 4, C_BORDER);
    tft.setTextColor(C_DIM, C_CARD);
    tft.drawString("WiFi: OFF", badgeX + 6, badgeY + 1, 2);
    tft.drawLine(x - 1, y + 12, x + 15, y, C_RED);
    break;
  }
}

// Vẽ biểu tượng Chữ Thập Đỏ Y Tế vector sắc nét chuẩn Hội Chữ Thập Đỏ
void drawMedicalCross(int cx, int cy, int size, uint16_t crossColor,
                      uint16_t badgeBg, uint16_t badgeBorder) {
  // 1. Khiên nền bo tròn
  tft.fillRoundRect(cx - size / 2, cy - size / 2, size, size, 6, badgeBg);
  tft.drawRoundRect(cx - size / 2, cy - size / 2, size, size, 6, badgeBorder);

  // 2. Cánh ngang chữ thập (+)
  int hw = size * 70 / 100;
  int hh = size * 26 / 100;
  tft.fillRoundRect(cx - hw / 2, cy - hh / 2, hw, hh, 2, crossColor);

  // 3. Cánh dọc chữ thập (+)
  int vw = size * 26 / 100;
  int vh = size * 70 / 100;
  tft.fillRoundRect(cx - vw / 2, cy - vh / 2, vw, vh, 2, crossColor);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH 1: HOME (MEDBOX — GIAO DIỆN CHÍNH BO TRÒN SANG TRỌNG)
// ----------------------------------------------------------------------------
void drawHomeHeader() {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.fillRoundRect(2, 2, W - 4, HDR_H, 6, C_HDR);
  tft.drawRoundRect(2, 2, W - 4, HDR_H, 6, C_BORDER);

  // 1. Tên Medbox phát sáng cyan
  tft.setTextColor(C_ACC, C_HDR);
  tft.drawString("Medbox", 10, 4, 2);

  // 2. Cột sóng & Badge trạng thái Wi-Fi
  drawWifiIndicator(106, 6, wifiState);

  // 3. Thứ & Ngày bên phải
  const char *d = DAY_FULL[curDay()];
  tft.setTextColor(C_FG, C_HDR);
  int dw = tft.textWidth(d, 2);
  tft.drawString(d, W - dw - 12, 4, 2);
}

void drawHomeClock() {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  // 1. Đồng hồ LED số lớn ở bên trái (nổi bật trên nền đen tuyền)
  uint32_t s = absSec() % 86400UL;
  char b[8];
  snprintf(b, sizeof(b), "%02d:%02d", (int)(s / 3600), (int)((s / 60) % 60));

  tft.fillRect(10, clockY, 145, 52, C_BG);
  tft.setTextColor(C_ACC, C_BG);
  tft.drawString(b, 14, clockY + 2, 7);

  // 2. Thẻ hiển thị Nhiệt độ & Độ ẩm (DHT11) mềm mại bên cạnh đồng hồ
  int envX = 160;
  int envY = clockY - 2;
  int envW = W - envX - 6; // 154px
  int envH = 54;

  tft.fillRoundRect(envX, envY, envW, envH, 8, C_CARD);
  tft.drawRoundRect(envX, envY, envW, envH, 8, C_BORDER);

  // --- DÒNG 1: NHIỆT ĐỘ ---
  tft.fillCircle(envX + 12, envY + 15, 4, C_TEMP_ICON);
  tft.drawCircle(envX + 12, envY + 15, 5, C_BORDER);
  tft.setTextColor(C_TEMP_LBL, C_CARD);
  tft.drawString("Nhiet do:", envX + 22, envY + 8, 2);

  char tBuf[16];
  if (dhtSensorIsReady() || (liveTemp > 0.0f && liveTemp < 70.0f)) {
    snprintf(tBuf, sizeof(tBuf), "%.1f", liveTemp);
  } else {
    snprintf(tBuf, sizeof(tBuf), "--.-");
  }
  tft.setTextColor(C_TEMP_VAL, C_CARD);
  int tValX = envX + 88;
  tft.drawString(tBuf, tValX, envY + 8, 2);
  int degX = tValX + tft.textWidth(tBuf, 2) + 4;
  tft.drawCircle(degX, envY + 11, 2, C_TEMP_ICON);
  tft.setTextColor(C_TEMP_VAL, C_CARD);
  tft.drawString("C", degX + 5, envY + 8, 2);

  // --- DÒNG 2: ĐỘ ẨM ---
  tft.fillCircle(envX + 12, envY + 39, 4, C_HUM_ICON);
  tft.drawCircle(envX + 12, envY + 39, 5, C_BORDER);
  tft.setTextColor(C_HUM_LBL, C_CARD);
  tft.drawString("Do am:", envX + 22, envY + 32, 2);

  char hBuf[16];
  if (dhtSensorIsReady() || (liveHum > 0.0f && liveHum <= 100.0f)) {
    snprintf(hBuf, sizeof(hBuf), "%.0f %%", liveHum);
  } else {
    snprintf(hBuf, sizeof(hBuf), "-- %%");
  }
  tft.setTextColor(C_HUM_VAL, C_CARD);
  tft.drawString(hBuf, envX + 88, envY + 32, 2);
}

void drawHomeCard() {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  FsmState st = fsmGetState();
  bool isDue = (st == FSM_ALERTING_DUE);
  bool isDispensing = (st == FSM_ROTATING_TO_DOSE || st == FSM_WAITING_TAKEN ||
                       st == FSM_ROTATING_TO_HOME);
  bool isJammed = (st == FSM_ERROR_JAMMED);

  uint16_t bg = (isDue || isJammed) ? 0x2800 : (isDispensing ? 0x01E4 : C_CARD);
  uint16_t border =
      (isDue || isJammed) ? C_RED : (isDispensing ? C_ACC : C_BORDER);

  tft.fillRoundRect(8, cardY, W - 16, cardH, 8, bg);
  tft.drawRoundRect(8, cardY, W - 16, cardH, 8, border);

  // 1. Biểu tượng DẤU THẬP ĐỎ Y TẾ (Luôn là màu ĐỎ rực rỡ trên khiên trắng tinh
  // chuẩn y tế)
  int crossCX = 32;
  int crossCY = cardY + cardH / 2;
  uint16_t crossCol = C_RED; // DẤU THẬP ĐỎ
  uint16_t badgeBg = C_FG;   // Nền khiên trắng tinh (#FFFFFF)
  uint16_t badgeBdr = C_RED; // Viền đỏ tươi (#FF0000)
  drawMedicalCross(crossCX, crossCY, 36, crossCol, badgeBg, badgeBdr);

  // 2. Dòng thông tin trạng thái theo FSM - Chữ to rõ, tương phản cao
  char ms[16];
  if (isDue) {
    maskStr(dueMask, ms, sizeof(ms));
    tft.setTextColor(C_RED, bg);
    tft.drawString("[!] DEN GIO UONG THUOC!", 58, cardY + 8, 2);

    tft.setTextColor(C_FG, bg);
    char l2[40];
    snprintf(l2, sizeof(l2), "Ngan %s | Bam K0 de MO KHAY", ms);
    tft.drawString(l2, 58, cardY + 28, 2);
  } else if (st == FSM_WAITING_TAKEN) {
    int s = currentMotorSlot - 1;
    tft.setTextColor(C_WARN, bg);
    char l1[48];
    if (s >= 0 && s < N_SLOTS && slotHasMed(s)) {
      snprintf(l1, sizeof(l1), "[MO NGAN %d] %s (%dv)", currentMotorSlot,
               meds[s].name, meds[s].dose ? meds[s].dose : 1);
    } else {
      snprintf(l1, sizeof(l1), "[DA MO NGAN %d] Xin moi lay", currentMotorSlot);
    }
    tft.drawString(l1, 58, cardY + 8, 2);

    char remMaskStr[16];
    maskStr(dueMask, remMaskStr, sizeof(remMaskStr));
    tft.setTextColor(C_FG, bg);
    char l2[48];
    if (dueMask != 0) {
      snprintf(l2, sizeof(l2), "Con Ngan %s -> Bam PUSH qua ngan", remMaskStr);
    } else {
      snprintf(l2, sizeof(l2), "Ngan cuoi -> Bam PUSH khoa ve Ngan 0");
    }
    tft.drawString(l2, 58, cardY + 28, 2);
  } else if (st == FSM_ROTATING_TO_DOSE || st == FSM_ROTATING_TO_HOME) {
    tft.setTextColor(C_ACC, bg);
    tft.drawString("DANG XOAY KHAY...", 58, cardY + 8, 2);

    tft.setTextColor(C_FG, bg);
    char l2[40];
    snprintf(l2, sizeof(l2), "Di chuyen ve Slot %d", currentMotorSlot);
    tft.drawString(l2, 58, cardY + 28, 2);
  } else if (isJammed) {
    tft.setTextColor(C_RED, bg);
    tft.drawString("[!] CANH BAO KET CO KHAY!", 58, cardY + 8, 2);

    tft.setTextColor(C_FG, bg);
    tft.drawString("Bam K0 thu lai | Giu PUSH calib", 58, cardY + 28, 2);
  } else {
    // FSM_SAFE_HOME_IDLE
    int idx = 0, m = 0;
    if (findNext(idx, m)) {
      char r[16];
      fmtRemain(r, sizeof(r), m);
      maskStr(sched[idx].mask, ms, sizeof(ms));
      tft.setTextColor(C_WARN, bg); // Vàng hổ phách sáng rực rỡ
      char l1[40];
      snprintf(l1, sizeof(l1), "Tiep theo: %02d:%02d (%s)", sched[idx].h,
               sched[idx].m, r);
      tft.drawString(l1, 58, cardY + 8, 2);

      tft.setTextColor(C_FG, bg); // Trắng tinh sáng rõ
      char l2[40];
      snprintf(l2, sizeof(l2), "Ngan: %s | Safe Home: Ngan 0", ms);
      tft.drawString(l2, 58, cardY + 28, 2);
    } else {
      tft.setTextColor(C_ACC, bg); // Cyan sáng
      tft.drawString("MEDBOX SAN SANG", 58, cardY + 8, 2);
      tft.setTextColor(C_FG, bg); // Trắng tinh sáng rõ
      tft.drawString("Nhan EC11 vao Menu dat lich", 58, cardY + 28, 2);
    }
  }
}

void drawTile(int i) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  int x = i * cellW + (cellW - tileW) / 2;
  uint16_t bg, fg = C_FG, border = C_BORDER;

  if ((dueMask >> i) & 1) {
    bg = C_RED;
    fg = C_FG;
    border = C_FG;
  } else if (!slotHasMed(i)) {
    bg = C_CARD;
    fg = C_DIM;
    border = C_BORDER;
  } else if (meds[i].qty <= LOW_QTY) {
    bg = C_WARN;
    fg = 0x0000;
    border = C_FG;
  } else {
    bg = C_OK;
    fg = 0x0000;
    border = C_FG;
  } // Thuốc đầy đủ -> Màu XANH LÁ

  tft.fillRoundRect(x, tileY, tileW, tileH, 6, bg);
  tft.drawRoundRect(x, tileY, tileW, tileH, 6, border);

  tft.setTextColor(fg, bg);
  char b[8];
  snprintf(b, sizeof(b), "%d", i + 1);
  tft.drawCentreString(b, x + tileW / 2, tileY + 4, 4);

  if ((dueMask >> i) & 1)
    snprintf(b, sizeof(b), "!!");
  else if (slotHasMed(i))
    snprintf(b, sizeof(b), "%dv", meds[i].qty);
  else
    snprintf(b, sizeof(b), "--");

  tft.setTextColor(fg, bg);
  tft.drawCentreString(b, x + tileW / 2, tileY + 32, 2);
}

void drawHome(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    drawHint("Nhan: Menu | K0: Da uong (Giu K0: Test) | K0: Thoat");
  }
  drawHomeHeader();
  drawHomeClock();
  drawHomeCard();
  for (int i = 0; i < N_SLOTS; i++)
    drawTile(i);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH 2: MENU 4 TÁC VỤ CHÍNH (FONT 4 TO RÕ NÉT, VỪA KHÍT 2.4" KHÔNG MẤT
// CHỮ)
// ----------------------------------------------------------------------------
int menuRowH() { return (H - HDR_H - 10 * UI) / 4; } // 49px / hàng

void drawMenuRow(int i) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  int rh = 44;
  int y = HDR_H + 4 + i * 46;
  bool sel = (i == menuSel);
  uint16_t bg = sel ? C_SEL : C_CARD;
  uint16_t bdr = sel ? C_ACC : C_BORDER;

  // 1. Khung thẻ hàng bo góc
  tft.fillRoundRect(8, y, W - 16, rh, 6, bg);
  tft.drawRoundRect(8, y, W - 16, rh, 6, bdr);

  // 2. Vạch chỉ báo màu vàng rực rỡ ở mép trái khi được chọn
  if (sel) {
    tft.fillRoundRect(10, y + 4, 5, rh - 8, 2, C_WARN);
  }

  // 3. Chữ Menu TO RÕ với Font 4 (cao 26px, nét đậm, căn giữa hàng, không tràn)
  int textX = sel ? 24 : 18;
  int textY = y + (rh - 26) / 2; // y + 9px (căn giữa hoàn hảo)
  uint16_t txtCol =
      sel ? C_WARN : C_FG; // Đang chọn: Vàng sáng | Không chọn: Trắng tinh

  tft.setTextColor(txtCol, bg);
  tft.drawString(MENU_ITEMS[i], textX, textY, 4);
}

void drawMenu(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);

    // Thanh Header Menu: Bên trái là tên Menu, bên phải cùng thanh là dòng căn
    // chỉnh: nhấn giữ
    tft.fillRoundRect(2, 2, W - 4, HDR_H, 6, C_HDR);
    tft.drawRoundRect(2, 2, W - 4, HDR_H, 6, C_BORDER);

    tft.setTextColor(C_ACC, C_HDR);
    tft.drawString("MENU CHON CHE DO", 10, 4, 2);

    const char *calibHint = "Can chinh: Nhan giu";
    int calibW = tft.textWidth(calibHint, 2);
    tft.setTextColor(C_WARN, C_HDR); // Màu Vàng sáng nổi bật
    tft.drawString(calibHint, W - calibW - 12, 4, 2);

    drawHint("Xoay: Chon  |  Nhan: Vao  |  K0: Thoat");
  }
  for (int i = 0; i < 4; i++)
    drawMenuRow(i);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH 3: DANH SÁCH LỊCH UỐNG THUỐC (S_SCHED)
// ----------------------------------------------------------------------------
void drawSchedRow(int idx) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  int vis = visRows(), first = (schedSel / vis) * vis, row = idx - first;
  if (row < 0 || row >= vis)
    return;
  if (idx >= schedRows()) {
    drawRowAt(row, false, C_FG, NULL);
    return;
  }
  char b[48];
  if (idx < nSched) {
    char ms[16];
    maskStr(sched[idx].mask, ms, sizeof(ms));
    int totalSlots = __builtin_popcount(sched[idx].mask);
    int actSlots = __builtin_popcount(sched[idx].mask & sched[idx].activeMask);
    const char *sName = getSessionName(sched[idx].h);
    if (actSlots == 0) {
      snprintf(b, sizeof(b), "%02d:%02d (%s) N%s [TAT]", sched[idx].h,
               sched[idx].m, sName, ms);
    } else {
      snprintf(b, sizeof(b), "%02d:%02d (%s) N%s [%d/%d]", sched[idx].h,
               sched[idx].m, sName, ms, actSlots, totalSlots);
    }
  } else {
    snprintf(b, sizeof(b), "+ Them moc gio moi");
  }
  drawRowAt(row, idx == schedSel, C_FG, b);
}

void drawSched(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    drawHeader("DANH SACH DAT LICH");
    drawHint("Nhan: Xem/Bat tat | Giu: Xoa | K0: Thoat");
  }
  int vis = visRows(), first = (schedSel / vis) * vis;
  for (int r = 0; r < vis; r++)
    drawSchedRow(first + r);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH CHI TIẾT MỐC GIỜ & BẬT/TẮT TỪNG NGĂN TRONG BUỔI (S_SCHED_DETAIL)
// ----------------------------------------------------------------------------
void drawSchedDetail(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "LICH: %02d:%02d (%s)", sched[schedSel].h,
             sched[schedSel].m, getSessionName(sched[schedSel].h));
    drawHeader(hdr);
    drawHint("Xoay: Chon | PUSH: Bat/Tat | K0: Quay lai");
  }

  if (schedSel < 0 || schedSel >= nSched)
    return;

  // Lấy danh sách các ngăn gán cho mốc giờ này
  int assigned[N_SLOTS];
  int assignedCount = 0;
  for (int s = 0; s < N_SLOTS; s++) {
    if ((sched[schedSel].mask >> s) & 1) {
      assigned[assignedCount++] = s;
    }
  }

  int totalRows = assignedCount + 2; // +1 sửa, +1 xóa
  int rowH = 34;
  int startY = HDR_H + 6;

  for (int r = 0; r < totalRows; r++) {
    int rowY = startY + r * (rowH + 4);
    bool sel = (r == schedDetailSel);
    uint16_t bg = sel ? C_SEL : C_CARD;
    uint16_t bdr = sel ? C_ACC : C_BORDER;

    tft.fillRoundRect(8, rowY, W - 16, rowH, 6, bg);
    tft.drawRoundRect(8, rowY, W - 16, rowH, 6, bdr);

    if (r < assignedCount) {
      int s = assigned[r];
      bool active = (sched[schedSel].activeMask >> s) & 1;
      char mName[28];
      if (slotHasMed(s)) {
        snprintf(mName, sizeof(mName), "N%d: %s (%dv)", s + 1, meds[s].name,
                 meds[s].dose ? meds[s].dose : 1);
      } else {
        snprintf(mName, sizeof(mName), "N%d: Thuoc (Lieu: 1v)", s + 1);
      }

      tft.setTextColor(active ? C_FG : C_DIM, bg);
      tft.drawString(mName, 14, rowY + 8, 2);

      // Huy hiệu trạng thái BAT / TAT
      if (active) {
        tft.setTextColor(sel ? C_FG : C_OK, bg);
        tft.drawString("[ DANG BAT ]", W - 92, rowY + 8, 2);
      } else {
        tft.setTextColor(C_DIM, bg);
        tft.drawString("[ DANG TAT ]", W - 92, rowY + 8, 2);
      }
    } else if (r == assignedCount) {
      tft.setTextColor(sel ? C_ACC : C_FG, bg);
      tft.drawString("> Chinh sua gio & cac ngan", 14, rowY + 8, 2);
    } else {
      tft.setTextColor(sel ? C_WARN : C_DIM, bg);
      tft.drawString("> Xoa moc gio nay", 14, rowY + 8, 2);
    }
  }
}

// ----------------------------------------------------------------------------
// MÀN HÌNH 4: CHỈNH GIỜ / PHÚT (S_EDIT)
// ----------------------------------------------------------------------------
void drawEdit(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  int nF = editNF();
  if (full) {
    tft.fillScreen(C_BG);
    drawHeader(editTarget == ET_CLOCK ? "CHINH DONG HO"
                                      : "BUOC 1: DAT GIO UONG");
    drawHint("Xoay: Doi so | Nhan: Tiep | K0: Huy ve truoc");
  }
  const char *names[3] = {"Gio", "Phut", "Thu"};
  int boxW = W / (nF + 1), boxH = 28 * UI, y = HDR_H + 10 * UI;
  for (int i = 0; i < nF; i++) {
    int x = i * (W / nF) + (W / nF - boxW) / 2;
    bool sel = (i == editField);
    uint16_t bg = sel ? C_SEL : C_CARD;
    tft.fillRoundRect(x, y, boxW, boxH, 6, bg);
    tft.drawRoundRect(x, y, boxW, boxH, 6, sel ? C_ACC : C_BORDER);

    tft.setTextColor(C_DIM, bg);
    tft.drawString(names[i], x + 6, y + 4, 2);

    char b[8];
    if (i == 2)
      snprintf(b, sizeof(b), "%s", DAY_NAMES[editVal[2]]);
    else
      snprintf(b, sizeof(b), "%02d", editVal[i]);
    tft.setTextColor(C_FG, bg);
    tft.drawCentreString(b, x + boxW / 2, y + 24, 4);
  }
  int py = y + boxH + 6 * UI;
  tft.fillRect(0, py, W, 10 * UI, C_BG);
  tft.setTextColor(C_ACC, C_BG);
  if (editField < nF - 1)
    tft.drawString("Nhan de qua chinh Phut", 8, py, 2);
  else if (editTarget == ET_CLOCK)
    tft.drawString("Nhan de LUU DONG HO", 8, py, 2);
  else
    tft.drawString("Nhan de qua BUOC 2: CHON NGAN", 8, py, 2);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH 5: CHỌN NGĂN THUỐC ỨNG VỚI GIỜ (S_PICK)
// ----------------------------------------------------------------------------
void drawPickCell(int i) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  int cw = W / 8, bw = cw - 3 * UI, bh = 28 * UI;
  int x = i * cw + (cw - bw) / 2, y = HDR_H + 10 * UI;
  uint16_t bg, fg = C_FG;
  if (i == N_SLOTS)
    bg = pickMask ? C_SEL : C_GREY;
  else if (!slotUsable(i)) {
    bg = C_CARD;
    fg = C_DIM;
  } else if ((pickMask >> i) & 1)
    bg = C_SEL;
  else
    bg = C_HDR;

  tft.fillRoundRect(x, y, bw, bh, 4, bg);
  uint16_t bc = (i == pickSel) ? C_FG : C_BG;
  tft.drawRoundRect(x, y, bw, bh, 4, bc);

  tft.setTextColor(fg, bg);
  if (i == N_SLOTS) {
    tft.drawCentreString("OK", x + bw / 2, y + (bh - 16) / 2, 2);
    return;
  }
  char b[4];
  snprintf(b, sizeof(b), "%d", i + 1);
  tft.drawCentreString(b, x + bw / 2, y + (bh - 26) / 2, 4);
}

void drawPickInfo() {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  int y0 = HDR_H + 46 * UI;
  tft.fillRect(0, y0, W, 24 * UI, C_BG);
  tft.setTextColor(C_FG, C_BG);
  char b[40];
  if (pickSel < N_SLOTS) {
    if (slotHasMed(pickSel))
      snprintf(b, sizeof(b), "Ngan %d: %s (%dv)", pickSel + 1,
               meds[pickSel].name, meds[pickSel].qty);
    else
      snprintf(b, sizeof(b), "Ngan %d: (Chua co thuoc)", pickSel + 1);
  } else {
    snprintf(b, sizeof(b), ">> BAM PUSH DE LUU LICH VAO NVS");
  }
  tft.drawString(b, 8, y0, 2);
  snprintf(b, sizeof(b), "Da chon: %d/%d ngan | K0: Huy",
           __builtin_popcount(pickMask), MAX_PER_TIME);
  tft.drawString(b, 8, y0 + 12 * UI, 2);
}

void drawPick(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    drawHeader(pickMode == PK_DISP ? "CHON NGAN LAY NGAY"
                                   : "BUOC 2: CHON NGAN THUOC");
    drawHint("Xoay: Chon ngan | Nhan: Bat/Tat | Chon OK: Luu | K0: Huy");
  }
  for (int i = 0; i <= N_SLOTS; i++)
    drawPickCell(i);
  drawPickInfo();
}

// ----------------------------------------------------------------------------
// MÀN HÌNH 6: DANH SÁCH 7 NGĂN THUỐC (S_MEDS)
// ----------------------------------------------------------------------------
void drawMedsRow(int idx) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  int vis = visRows(), row = idx;
  if (row < 0 || row >= vis || idx >= N_SLOTS)
    return;
  char b[40];
  if (slotHasMed(idx))
    snprintf(b, sizeof(b), "Ngan %d: %-10s %2dv (Lieu: %dv)", idx + 1,
             meds[idx].name, meds[idx].qty, meds[idx].dose);
  else
    snprintf(b, sizeof(b), "Ngan %d: (Ngan trong - Bam de them)", idx + 1);
  drawRowAt(row, idx == medsSel, slotHasMed(idx) ? C_FG : C_DIM, b);
}

void drawMeds(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    drawHeader("NAP & QUAN LY 7 NGAN THUOC");
    drawHint("Nhan PUSH: Nap thuoc & Xoay | Giu PUSH: Chi tiet | K0: Thoat");
  }
  for (int i = 0; i < N_SLOTS; i++)
    drawMedsRow(i);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH 7: CHI TIẾT NGĂN THUỐC (S_MED)
// ----------------------------------------------------------------------------
void drawMedRow(int r) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  char b[40];
  switch (r) {
  case 0:
    snprintf(b, sizeof(b), "1. Ten thuoc:   %s",
             slotHasMed(curSlot) ? meds[curSlot].name : "(Chua dat)");
    break;
  case 1:
    snprintf(b, sizeof(b), "2. So luong:    %d vien", meds[curSlot].qty);
    break;
  case 2:
    snprintf(b, sizeof(b), "3. Lieu dung:   %d vien / lan", meds[curSlot].dose);
    break;
  case 3:
    snprintf(b, sizeof(b), "4. [XOA NGAN NAY]");
    break;
  }
  drawRowAt(r, r == medRow, (r == 3) ? C_RED : C_FG, b);
}

void drawMed(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    char t[32];
    snprintf(t, sizeof(t), "THONG TIN NGAN %d", curSlot + 1);
    drawHeader(t);
    drawHint("Xoay: Chon muc | Nhan: Chinh sua | K0: Tro ve");
  }
  for (int r = 0; r < 4; r++)
    drawMedRow(r);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH 8 & 9: NHẬP TÊN & SỐ LƯỢNG (S_NAME & S_NUM)
// ----------------------------------------------------------------------------
void drawNameBox(int i) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  int bw = W / NAME_LEN, bh = 24 * UI, x = i * bw, y = HDR_H + 14 * UI;
  bool sel = (i == namePos);
  uint16_t bg = sel ? C_SEL : C_CARD;
  tft.fillRoundRect(x + 1, y, bw - 2, bh, 4, bg);
  tft.drawRoundRect(x + 1, y, bw - 2, bh, 4, sel ? C_ACC : C_BORDER);
  tft.setTextColor(C_FG, bg);
  char charStr[2] = {CHARSET[nameIdx[i]], '\0'};
  tft.drawCentreString(charStr, x + bw / 2, y + (bh - 26) / 2, 4);
}

void drawName(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    char t[32];
    snprintf(t, sizeof(t), "DAT TEN THUOC - NGAN %d", curSlot + 1);
    drawHeader(t);
    drawHint("Xoay: Doi chu | Nhan: Chu tiep | Giu PUSH: Luu | K0: Thoat");
  }
  for (int i = 0; i < NAME_LEN; i++)
    drawNameBox(i);
  char b[24];
  snprintf(b, sizeof(b), "Ky tu %d/%d", namePos + 1, NAME_LEN);
  int labelY = HDR_H + 14 * UI + 24 * UI + 6 * UI;
  tft.fillRect(0, labelY, W, 10 * UI, C_BG);
  tft.setTextColor(C_DIM, C_BG);
  tft.drawString(b, 8, labelY, 2);
}

void drawNum(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    char t[32];
    if (numTarget == NT_QTY)
      snprintf(t, sizeof(t), "SO LUONG VIEN - NGAN %d", curSlot + 1);
    else
      snprintf(t, sizeof(t), "LIEU DUNG MOI LAN");
    drawHeader(t);
    drawHint("Xoay: Tang/Giam so | Nhan: LUU | K0: Huy ve truoc");
  }
  int bw = W / 2, bh = 34 * UI, x = (W - bw) / 2, y = HDR_H + 14 * UI;
  tft.fillRoundRect(x, y, bw, bh, 8, C_SEL);
  tft.drawRoundRect(x, y, bw, bh, 8, C_ACC);
  char b[8];
  snprintf(b, sizeof(b), "%02d", numVal);
  tft.setTextColor(C_FG, C_SEL);
  tft.drawCentreString(b, x + bw / 2, y + 10, 6);

  tft.setTextColor(C_DIM, C_BG);
  printCentered("vien", y + bh + 6 * UI);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH 10: XEM LỊCH SỬ UỐNG THUỐC (S_HIST)
// Hiển thị 5 trường: Thứ cữ, Giờ cữ, Tên thuốc, Trạng thái (Dung cu/Lay som/Bo cu), Giờ lấy đúng
// Bố cục phân cột cố định trên chiều rộng 320px (IPS ST7789), tuyệt đối không tràn màn hình.
// ----------------------------------------------------------------------------
static void getHistSchedTime(const Hist &h, char *buf, size_t len) {
  if (h.sched_h != 0xFF && h.sched_m != 0xFF) {
    snprintf(buf, len, "%02d:%02d", h.sched_h, h.sched_m);
    return;
  }
  // Tìm giờ cữ trong slotSched theo slot và session
  for (int i = 0; i < nSlotSched; i++) {
    if (slotSched[i].slot == h.slot && slotSched[i].active) {
      if (h.session < 4) {
        if (getSessionIndexFromHour(slotSched[i].hour) == h.session) {
          snprintf(buf, len, "%02d:%02d", slotSched[i].hour, slotSched[i].minute);
          return;
        }
      } else {
        snprintf(buf, len, "%02d:%02d", slotSched[i].hour, slotSched[i].minute);
        return;
      }
    }
  }
  // Fallback nếu không có cữ hẹn cụ thể
  if (h.type == H_TAKEN) {
    snprintf(buf, len, "%02d:%02d", h.min / 60, h.min % 60);
  } else {
    snprintf(buf, len, "--:--");
  }
}

static void getHistMedName(uint8_t slot, char *buf, size_t len) {
  if (slot < N_SLOTS && meds[slot].name[0] != '\0') {
    strncpy(buf, meds[slot].name, len - 1);
    buf[len - 1] = '\0';
  } else {
    snprintf(buf, len, "Ng%d", slot + 1);
  }
}

void drawHistRow(int r) {
  int rh = 12 * UI; // 24px
  int y = HDR_H + r * rh;
  uint16_t bg = (r % 2 == 1) ? C_CARD : C_BG;

  int k = histTop + r;
  if (k >= histCount) {
    tft.fillRect(4, y, W - 8, rh - 2, C_BG);
    return;
  }

  tft.fillRoundRect(4, y, W - 8, rh - 2, 4, bg);

  int idx = ((int)histHead - 1 - k + 2 * N_HIST) % N_HIST;
  Hist &h = hist[idx];

  // 1. Thứ cữ + Giờ cữ hẹn (ví dụ: "T2 08:00")
  char schedTimeStr[8];
  getHistSchedTime(h, schedTimeStr, sizeof(schedTimeStr));
  char schedColText[16];
  snprintf(schedColText, sizeof(schedColText), "%s %s", DAY_NAMES[h.day % 7], schedTimeStr);

  // 2. Tên thuốc (tối đa 7 ký tự để không tràn)
  char medName[10];
  getHistMedName(h.slot, medName, sizeof(medName));

  // 3. Trạng thái: Dung cu / Lay som / Bo cu
  const char *tn;
  uint16_t statusCol;
  if (h.type == H_TAKEN) {
    tn = "Dung cu";
    statusCol = C_OK;
  } else if (h.type == H_MISS) {
    tn = "Bo cu";
    statusCol = C_RED;
  } else {
    tn = "Lay som";
    statusCol = C_ACC;
  }

  // 4. Giờ lấy đúng (thời điểm thực tế bệnh nhân lấy)
  char actBuf[10];
  uint16_t actCol = C_FG;
  if (h.type == H_MISS) {
    strcpy(actBuf, "--:--");
    actCol = C_DIM;
  } else {
    snprintf(actBuf, sizeof(actBuf), "%02d:%02d", h.min / 60, h.min % 60);
  }

  // Vẽ các cột thẳng hàng tuyệt đối với Font 2 (cao 16px)
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  int textY = y + (rh - 16) / 2;

  // Cột 1: Thứ & Giờ cữ (X = 8, rộng ~75px)
  tft.setTextColor(C_FG, bg);
  tft.drawString(schedColText, 8, textY, 2);

  // Cột 2: Tên thuốc (X = 90, rộng ~68px)
  tft.setTextColor(C_WARN, bg); // Màu vàng hổ phách sáng
  tft.drawString(medName, 90, textY, 2);

  // Cột 3: Trạng thái (X = 168, rộng ~72px)
  tft.setTextColor(statusCol, bg);
  tft.drawString(tn, 168, textY, 2);

  // Cột 4: Giờ lấy đúng (X = 252, rộng ~50px)
  tft.setTextColor(actCol, bg);
  tft.drawString(actBuf, 252, textY, 2);
}

void drawHist(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    drawHeader("LICH SU UONG THUOC");
  }
  int vis = visRows();
  for (int r = 0; r < vis; r++)
    drawHistRow(r);
  if (histCount == 0)
    drawRowAt(0, false, C_DIM, "(Chua co nhat ky nao)");
  char h[40];
  if (histCount == 0)
    snprintf(h, sizeof(h), "K0: Thoat ve Menu");
  else {
    int last = histTop + vis;
    if (last > histCount)
      last = histCount;
    snprintf(h, sizeof(h), "Xoay: Cuon | K0: Thoat ve Menu (%d-%d/%d)",
             histTop + 1, last, histCount);
  }
  drawHint(h);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH 11: AUTO HIỆU CHUẨN NGĂN 0 (S_CALIB - GẶP VẠCH ĐEN TỰ ĐỘNG NHẬN NGĂN
// 0)
// ----------------------------------------------------------------------------
void drawCalib(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    drawHeader("AUTO HIEU CHUAN NGAN 0");
    drawHint("Nhan PUSH: Chay Auto | K0: Thoat ve Menu");
  }

  // 1. Thẻ hiển thị Cảm biến quang TCRT5000 & Vị trí khay hiện tại
  bool atSensor = isSensorAtHome(); // Kiểm tra có đang bắt trúng vạch đen không
  uint16_t bdrColor = atSensor ? C_OK : C_BORDER;
  uint16_t cardBg = atSensor ? 0x0A26 : C_CARD;

  tft.fillRoundRect(8, HDR_H + 4, W - 16, 48, 6, cardBg);
  tft.drawRoundRect(8, HDR_H + 4, W - 16, 48, 6, bdrColor);

  tft.setTextColor(C_DIM, cardBg);
  tft.drawString("TCRT5000:", 14, HDR_H + 9, 2);
  if (atSensor) {
    tft.setTextColor(C_OK, cardBg);
    tft.drawString("[ DA NHAN VACH DEN - MOC 0 ]", 95, HDR_H + 9, 2);
  } else {
    tft.setTextColor(C_WARN, cardBg);
    tft.drawString("[ CHUA THAY VACH DEN ]", 95, HDR_H + 9, 2);
  }

  tft.setTextColor(C_DIM, cardBg);
  tft.drawString("Vi tri khay:", 14, HDR_H + 28, 2);
  char slotStr[48];
  if (currentMotorSlot == 0) {
    snprintf(slotStr, sizeof(slotStr), "NGAN 0 (SAFE HOME - DA DONG)");
    tft.setTextColor(C_ACC, cardBg);
  } else {
    snprintf(slotStr, sizeof(slotStr), "NGAN %d (DANG MO THUOC)",
             currentMotorSlot);
    tft.setTextColor(C_WARN, cardBg);
  }
  tft.drawString(slotStr, 95, HDR_H + 28, 2);

  // 2. Danh sách 3 tùy chọn Auto cực kỳ tinh gọn
  const char *opts[3] = {"1. AUTO Hieu chuan (Gap vach la nhan Ngan 0)",
                         "2. Xoay thu ve Ngan 0 (Kiem tra)",
                         "3. [ Thoat ve Menu ]"};

  int startY = HDR_H + 58;
  int rowH = 30;
  for (int i = 0; i < 3; i++) {
    bool sel = (i == calibSel);
    uint16_t bg = sel ? C_SEL : C_BG;
    tft.fillRoundRect(8, startY + i * rowH, W - 16, rowH - 2, 6, bg);
    if (sel) {
      tft.drawRoundRect(8, startY + i * rowH, W - 16, rowH - 2, 6, C_ACC);
    }
    tft.setTextColor(sel ? C_FG : C_DIM, bg);
    tft.drawString(sel ? "> " : "  ", 12, startY + i * rowH + 6, 2);
    tft.drawString(opts[i], 28, startY + i * rowH + 6, 2);
  }
}

// ----------------------------------------------------------------------------
// MÀN HÌNH NẠP THUỐC BƯỚC 1: CHỌN SỐ VIÊN NẠP THÊM (S_REFILL_QTY)
// ----------------------------------------------------------------------------
void drawRefillQty(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    drawHeader("BUOC 1: CHON SO VIEN NAP");
    drawHint("Xoay: Chon so vien | Nhan PUSH: Tiep tuc | K0: Huy & Dong");

    // 1. Thẻ thông tin ngăn hiện tại
    int cardY = HDR_H + 4;
    tft.fillRoundRect(8, cardY, W - 16, 42, 6, C_CARD);
    tft.drawRoundRect(8, cardY, W - 16, 42, 6, C_BORDER);

    char titleBuf[48];
    snprintf(titleBuf, sizeof(titleBuf), "NGAN %d: %s", curSlot + 1,
             slotHasMed(curSlot) ? meds[curSlot].name : "(Ngan trong)");
    tft.setTextColor(C_ACC, C_CARD);
    tft.drawString(titleBuf, 16, cardY + 6, 2);

    char curQtyBuf[48];
    snprintf(curQtyBuf, sizeof(curQtyBuf),
             "Hien co trong ngan: %d vien  (Lieu: %dv)", meds[curSlot].qty,
             meds[curSlot].dose);
    tft.setTextColor(C_DIM, C_CARD);
    tft.drawString(curQtyBuf, 16, cardY + 23, 2);
  }

  // 2. Thẻ tương tác chính: Chọn số viên nạp thêm
  int boxX = 20, boxY = 78, boxW = W - 40, boxH = 88;
  tft.fillRoundRect(boxX, boxY, boxW, boxH, 8, C_CARD);
  tft.drawRoundRect(boxX, boxY, boxW, boxH, 8, C_ACC);

  tft.setTextColor(C_ACC, C_CARD);
  tft.drawCentreString("SO VIEN NAP THEM:", W / 2, boxY + 8, 2);

  char addBuf[24];
  snprintf(addBuf, sizeof(addBuf), "+ %02d VIEN", refillAddQty);
  tft.setTextColor(C_FG, C_CARD);
  tft.drawCentreString(addBuf, W / 2, boxY + 28, 4);

  tft.setTextColor(C_DIM, C_CARD);
  tft.drawCentreString("< Van num EC11 de tang / giam >", W / 2, boxY + 66, 1);

  // 3. Thẻ dự tính tổng số viên sau nạp
  int sumY = 174, sumH = 34;
  tft.fillRoundRect(boxX, sumY, boxW, sumH, 6, 0x0A26);
  tft.drawRoundRect(boxX, sumY, boxW, sumH, 6, C_OK);

  char totalBuf[56];
  int totalNew = meds[curSlot].qty + refillAddQty;
  snprintf(totalBuf, sizeof(totalBuf), "Tong sau nap: %d vien  (%d + %d)",
           totalNew, meds[curSlot].qty, refillAddQty);
  tft.setTextColor(C_OK, 0x0A26);
  tft.drawCentreString(totalBuf, W / 2, sumY + 8, 2);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH NẠP THUỐC BƯỚC 2: XÁC NHẬN ĐÃ BỎ THUỐC VÀO NGĂN (S_REFILL_CONFIRM)
// ----------------------------------------------------------------------------
void drawRefillConfirm(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    drawHeader("BUOC 2: XAC NHAN DA BO THUOC");
    drawHint("PUSH: Xac nhan bo xong | K0: Huy & Dong khay");
  }

  int cardX = 12, cardY = HDR_H + 6, cardW = W - 24, cardH = 174;
  tft.fillRoundRect(cardX, cardY, cardW, cardH, 8, C_CARD);
  tft.drawRoundRect(cardX, cardY, cardW, cardH, 8, C_WARN);

  tft.setTextColor(C_DIM, C_CARD);
  tft.drawCentreString("Vui long bo thuoc vao khay dang mo:", W / 2, cardY + 10,
                       2);

  char line2[48];
  snprintf(line2, sizeof(line2), "DA BO +%d VIEN VAO NGAN %d ?", refillAddQty,
           curSlot + 1);
  tft.setTextColor(C_WARN, C_CARD);
  tft.drawCentreString(line2, W / 2, cardY + 34, 2);

  char line3[56];
  int tot = meds[curSlot].qty + refillAddQty;
  snprintf(line3, sizeof(line3), "[ %s ]  Tong sau nap: %d vien",
           slotHasMed(curSlot) ? meds[curSlot].name : "Thuoc", tot);
  tft.setTextColor(C_FG, C_CARD);
  tft.drawCentreString(line3, W / 2, cardY + 60, 2);

  // Nút bấm xác nhận PUSH lớn
  int btnX = cardX + 16, btnY = cardY + 90, btnW = cardW - 32, btnH = 42;
  tft.fillRoundRect(btnX, btnY, btnW, btnH, 8, C_SEL);
  tft.drawRoundRect(btnX, btnY, btnW, btnH, 8, C_ACC);

  tft.setTextColor(C_FG, C_SEL);
  tft.drawCentreString(">>> BAM PUSH DE XAC NHAN <<<", W / 2, btnY + 12, 2);

  // Dòng cam kết an toàn
  tft.setTextColor(C_ACC, C_CARD);
  tft.drawCentreString("* Khay se TU DONG QUAY VE NGAN 0 de khoa", W / 2,
                       cardY + 148, 1);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH NẠP THUỐC QUA WEB DASHBOARD (S_REFILL_WEB)
// Khi Web yêu cầu nạp thuốc (ví dụ Ngăn 4), màn hình hiển thị chế độ nạp thuốc
// qua Web Tuyệt đối không hiển thị màn hình thông tin ngăn (S_MED)
// ----------------------------------------------------------------------------
void drawRefillWeb(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    drawHeader("NAP THUOC QUA WEB");
    drawHint("Thao tac tren Web  |  PUSH / K0: Khoa khay ve 0");
  }

  // 1. Thẻ trạng thái mở ngăn lớn nổi bật
  int cardX = 12, cardY = HDR_H + 4, cardW = W - 24, cardH = 176;
  tft.fillRoundRect(cardX, cardY, cardW, cardH, 8, C_CARD);
  tft.drawRoundRect(cardX, cardY, cardW, cardH, 8, C_ACC);

  // Tiêu đề phụ: Đang mở cửa nạp hướng 6 giờ
  tft.setTextColor(C_DIM, C_CARD);
  tft.drawCentreString("CUA NAP THUOC HUONG 6 GIO DANG MO", W / 2, cardY + 8,
                       2);

  // Số ngăn hiển thị TO, RÕ RÀNG
  char slotBig[32];
  snprintf(slotBig, sizeof(slotBig), ">>> NGAN %d <<<", curSlot + 1);
  tft.setTextColor(C_WARN, C_CARD);
  tft.drawCentreString(slotBig, W / 2, cardY + 28, 4);

  // Thông tin thuốc hiện tại
  char medInfo[64];
  snprintf(medInfo, sizeof(medInfo), "[ %s ]  Hien co: %d vien  (Lieu: %dv)",
           slotHasMed(curSlot) ? meds[curSlot].name : "Ngan trong",
           meds[curSlot].qty, meds[curSlot].dose);
  tft.setTextColor(C_FG, C_CARD);
  tft.drawCentreString(medInfo, W / 2, cardY + 62, 2);

  // Thẻ thông báo Web đang điều khiển
  int webBoxX = cardX + 10, webBoxY = cardY + 86, webBoxW = cardW - 20,
      webBoxH = 46;
  tft.fillRoundRect(webBoxX, webBoxY, webBoxW, webBoxH, 6, 0x0A26);
  tft.drawRoundRect(webBoxX, webBoxY, webBoxW, webBoxH, 6, C_OK);

  tft.setTextColor(C_OK, 0x0A26);
  tft.drawCentreString("DANG KET NOI WEB DASHBOARD", W / 2, webBoxY + 6, 2);

  tft.setTextColor(C_DIM, 0x0A26);
  tft.drawCentreString("Vui long nhap ten, so luong & bam Xac Nhan", W / 2,
                       webBoxY + 26, 1);

  // Dòng chỉ dẫn nút bấm an toàn tại chỗ
  tft.setTextColor(C_ACC, C_CARD);
  tft.drawCentreString("* Bam PUSH hoac K0 tren hop thuoc de DONG KHAY", W / 2,
                       cardY + 144, 2);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH XÁC NHẬN XÓA NGĂN THUỐC (S_CLEAR_CONFIRM)
// Khay đã tự động xoay mở ngăn này ra hướng 6 giờ để người dùng lấy hết thuốc
// ra
// ----------------------------------------------------------------------------
void drawClearConfirm(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    drawHeader("XAC NHAN XOA NGAN");
    drawHint("PUSH: Xac nhan da lay & Xoa | K0: Huy & Ve 0");
  }

  // 1. Thẻ cảnh báo mở ngăn lấy thuốc lớn
  int cardX = 12, cardY = HDR_H + 4, cardW = W - 24, cardH = 176;
  tft.fillRoundRect(cardX, cardY, cardW, cardH, 8, C_CARD);
  tft.drawRoundRect(cardX, cardY, cardW, cardH, 8, C_RED);

  // Tiêu đề phụ: Đang mở cửa lấy thuốc hướng 6 giờ
  tft.setTextColor(C_DIM, C_CARD);
  tft.drawCentreString("CUA 6H DANG MO DE LAY THUOC RA:", W / 2, cardY + 8, 2);

  // Số ngăn hiển thị TO, RÕ RÀNG với màu Đỏ Cảnh Báo
  char slotBig[32];
  snprintf(slotBig, sizeof(slotBig), ">>> NGAN %d <<<", curSlot + 1);
  tft.setTextColor(C_RED, C_CARD);
  tft.drawCentreString(slotBig, W / 2, cardY + 28, 4);

  // Chi tiết thuốc hiện tại
  char medInfo[64];
  snprintf(medInfo, sizeof(medInfo), "[ %s ]  Con: %d vien  (Lieu: %dv)",
           slotHasMed(curSlot) ? meds[curSlot].name : "Ngan trong",
           meds[curSlot].qty, meds[curSlot].dose);
  tft.setTextColor(C_FG, C_CARD);
  tft.drawCentreString(medInfo, W / 2, cardY + 62, 2);

  // Thẻ hướng dẫn thao tác nổi bật
  int warnBoxX = cardX + 10, warnBoxY = cardY + 86, warnBoxW = cardW - 20,
      warnBoxH = 46;
  tft.fillRoundRect(warnBoxX, warnBoxY, warnBoxW, warnBoxH, 6,
                    0x2800); // Nền đỏ sẫm cảnh báo
  tft.drawRoundRect(warnBoxX, warnBoxY, warnBoxW, warnBoxH, 6, C_RED);

  tft.setTextColor(0xFF55, 0x2800);
  tft.drawCentreString("VUI LONG LAY HET THUOC RA KHOI KHAY!", W / 2,
                       warnBoxY + 6, 2);

  tft.setTextColor(C_DIM, 0x2800);
  tft.drawCentreString("Toan bo thuoc & lich uong cua ngan se bi xoa", W / 2,
                       warnBoxY + 26, 1);

  // Nút xác nhận PUSH
  int btnX = cardX + 12, btnY = cardY + 138, btnW = cardW - 24, btnH = 32;
  tft.fillRoundRect(btnX, btnY, btnW, btnH, 6, C_SEL);
  tft.drawRoundRect(btnX, btnY, btnW, btnH, 6, C_ACC);

  tft.setTextColor(C_FG, C_SEL);
  tft.drawCentreString(">>> BAM PUSH: DA LAY THUOC RA <<<", W / 2, btnY + 8, 2);
}

// ----------------------------------------------------------------------------
// MÀN HÌNH UỐNG SỚM BƯỚC 1: CHỌN THEO TỪNG BUỔI (SÁNG, TRƯA, CHIỀU, TỐI)
// ----------------------------------------------------------------------------
void drawEarlySession(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    drawHeader("LAY THUOC THEO BUOI");
    drawHint("EC11: Chon buoi | PUSH: Xem thuoc buoi | K0: Thoat");
  }

  // Hiển thị 4 thẻ buổi lớn: Sáng, Trưa, Chiều, Tối
  for (int i = 0; i < 4; i++) {
    int cardY = 30 + i * 46;
    int cardH = 42;
    int cardW = W - 16;
    int cardX = 8;

    bool sel = (i == earlySessionSel);
    bool taken = isSessionTakenToday(i);
    uint8_t mask = getSessionMask(i);
    int nSlots = __builtin_popcount(mask);

    uint16_t bg = sel ? C_SEL : C_CARD;
    uint16_t bdr = sel ? C_ACC : C_BORDER;

    tft.fillRoundRect(cardX, cardY, cardW, cardH, 6, bg);
    tft.drawRoundRect(cardX, cardY, cardW, cardH, 6, bdr);

    // 1. Tên buổi và số thứ tự (dòng trên)
    char tTitle[48];
    snprintf(tTitle, sizeof(tTitle), "%s %d. %s", sel ? ">" : " ", i + 1,
             SESSION_NAMES[i]);
    tft.setTextColor(sel ? C_FG : C_FG, bg);
    tft.drawString(tTitle, cardX + 6, cardY + 5, 2);

    // Huy hiệu trạng thái bên phải
    if (nSlots == 0) {
      tft.setTextColor(C_DIM, bg);
      tft.drawString("[TRONG]", W - 66, cardY + 5, 2);
    } else if (taken) {
      tft.setTextColor(C_DIM, bg);
      tft.drawString("[DA UONG]", W - 84, cardY + 5, 2);
    } else {
      tft.setTextColor(sel ? C_ACC : C_OK, bg);
      tft.drawString("[CHUA UONG]", W - 98, cardY + 5, 2);
    }

    // 2. Dòng dưới: Chi tiết ngăn thuốc tóm tắt
    if (nSlots == 0) {
      tft.setTextColor(C_DIM, bg);
      char sub[40];
      snprintf(sub, sizeof(sub), "Khung gio: %s (Chua co lich)",
               SESSION_HOURS[i]);
      tft.drawString(sub, cardX + 16, cardY + 23, 1);
    } else {
      char timeDesc[48];
      getSessionTimeDesc(i, timeDesc, sizeof(timeDesc));
      char slotSummary[64] = "";
      int count = 0;
      for (int s = 0; s < N_SLOTS; s++) {
        if ((mask >> s) & 1) {
          char item[20];
          snprintf(item, sizeof(item), "%sN%d", count > 0 ? ", " : "", s + 1);
          strncat(slotSummary, item,
                  sizeof(slotSummary) - strlen(slotSummary) - 1);
          count++;
        }
      }
      char lineBuf[72];
      snprintf(lineBuf, sizeof(lineBuf), "Lich %s | Co %d ngan: %s", timeDesc,
               nSlots, slotSummary);
      tft.setTextColor(sel ? C_ACC : C_DIM, bg);
      tft.drawString(lineBuf, cardX + 16, cardY + 23, 1);
    }
  }
}

// ----------------------------------------------------------------------------
// MÀN HÌNH UỐNG SỚM BƯỚC 2: HIỂN THỊ CHI TIẾT NGĂN & NÚT XÁC NHẬN LẤY BUỔI
// ----------------------------------------------------------------------------
void drawEarlyConfirm(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "CHI TIET %s", SESSION_NAMES[earlySessionSel]);
    drawHeader(hdr);
    drawHint("PUSH: Xac nhan lay buoi | K0: Quay lai chon buoi");
  }

  if (earlySessionSel < 0 || earlySessionSel >= 4)
    return;

  const char *sName = SESSION_NAMES[earlySessionSel];
  char timeDesc[64];
  getSessionTimeDesc(earlySessionSel, timeDesc, sizeof(timeDesc));
  uint8_t mask = getSessionMask(earlySessionSel);
  int nSlotsInSched = __builtin_popcount(mask);
  bool taken = isSessionTakenToday(earlySessionSel);

  // 1. Thẻ thông tin buổi được chọn (Header card)
  int cardY = HDR_H + 4;
  tft.fillRoundRect(8, cardY, W - 16, 38, 6, C_CARD);
  tft.drawRoundRect(8, cardY, W - 16, 38, 6, C_BORDER);

  char tBuf[56];
  snprintf(tBuf, sizeof(tBuf), "%s - %s", sName, timeDesc);
  tft.setTextColor(C_ACC, C_CARD);
  tft.drawString(tBuf, 14, cardY + 4, 2);

  tft.setTextColor(taken ? C_WARN : (nSlotsInSched > 0 ? C_OK : C_DIM), C_CARD);
  if (taken) {
    tft.drawString("Trang thai: [ DA UONG HOM NAY ]", 14, cardY + 22, 1);
  } else if (nSlotsInSched == 0) {
    tft.drawString("Trang thai: [ CHUA CAI DAT LICH THUOC ]", 14, cardY + 22,
                   1);
  } else {
    char stBuf[48];
    snprintf(stBuf, sizeof(stBuf),
             "Trang thai: [ CHUA UONG - CO %d NGAN THUOC ]", nSlotsInSched);
    tft.drawString(stBuf, 14, cardY + 22, 1);
  }

  // 2. Thẻ danh sách các ngăn thuốc có trong buổi
  int listY = 70, listH = 94;
  tft.fillRoundRect(8, listY, W - 16, listH, 6, C_CARD);
  tft.drawRoundRect(8, listY, W - 16, listH, 6, C_BORDER);

  tft.setTextColor(C_DIM, C_CARD);
  char listTitle[56];
  snprintf(listTitle, sizeof(listTitle), "CAC NGAN THUOC TRONG %s:", sName);
  tft.drawString(listTitle, 14, listY + 5, 1);

  int line = 0;
  for (int s = 0; s < N_SLOTS; s++) {
    if (!((mask >> s) & 1))
      continue;
    char medLine[56];
    bool hasStock = (meds[s].qty > 0);
    uint16_t rowCol = hasStock ? C_FG : C_WARN;

    if (slotHasMed(s)) {
      snprintf(medLine, sizeof(medLine),
               "* Ngan %d: %-10s (Lieu: %dv | Con: %dv)%s", s + 1, meds[s].name,
               meds[s].dose ? meds[s].dose : 1, meds[s].qty,
               hasStock ? "" : " [HET!]");
    } else {
      snprintf(medLine, sizeof(medLine),
               "* Ngan %d: Thuoc chua dat ten (Lieu: 1v)", s + 1);
    }

    tft.setTextColor(rowCol, C_CARD);
    tft.drawString(medLine, 14, listY + 20 + line * 18, 2);
    line++;
    if (line >= 4)
      break;
  }

  if (line == 0) {
    tft.setTextColor(C_WARN, C_CARD);
    tft.drawString("(Buoi nay chua duoc gan ngan thuoc nao!)", 14, listY + 30,
                   2);
  }

  // 3. Hộp nút bấm xác nhận lấy buổi đó
  int btnY = 168, btnH = 46, btnW = W - 24, btnX = 12;
  if (taken) {
    tft.fillRoundRect(btnX, btnY, btnW, btnH, 8, C_CARD);
    tft.drawRoundRect(btnX, btnY, btnW, btnH, 8, C_WARN);
    tft.setTextColor(C_WARN, C_CARD);
    tft.drawCentreString("[ BUOI NAY DA UONG - KHONG UONG LAP LAI ]", W / 2,
                         btnY + 14, 2);
  } else if (line == 0) {
    tft.fillRoundRect(btnX, btnY, btnW, btnH, 8, C_CARD);
    tft.drawRoundRect(btnX, btnY, btnW, btnH, 8, C_BORDER);
    tft.setTextColor(C_DIM, C_CARD);
    tft.drawCentreString("[ BUOI NAY KHONG CO THUOC DE LAY ]", W / 2, btnY + 14,
                         2);
  } else {
    tft.fillRoundRect(btnX, btnY, btnW, btnH, 8, C_SEL);
    tft.drawRoundRect(btnX, btnY, btnW, btnH, 8, C_ACC);

    tft.setTextColor(C_FG, C_SEL);
    char btnText[56];
    snprintf(btnText, sizeof(btnText), ">>> BAM PUSH: XAC NHAN LAY %s <<<",
             sName);
    tft.drawCentreString(btnText, W / 2, btnY + 7, 2);

    tft.setTextColor(C_ACC, C_SEL);
    tft.drawCentreString("* Khay se quay tung ngan, lay xong bam PUSH tiep *",
                         W / 2, btnY + 27, 1);
  }
}

// ----------------------------------------------------------------------------
// MÀN HÌNH UỐNG SỚM BƯỚC 3: ĐANG MỞ LẦN LƯỢT TỪNG NGĂN ĐỂ LẤY THUỐC
// (S_EARLY_DISPENSE)
// ----------------------------------------------------------------------------
void drawEarlyDispense(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  if (full) {
    tft.fillScreen(C_BG);
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "%s: NGAN %d / %d",
             SESSION_NAMES[earlySessionSel], earlyCurrentStep + 1,
             earlySlotCount);
    drawHeader(hdr);
    drawHint("PUSH: Xac nhan da lay -> Qua ngan tiep | K0: Huy & Dong");
  }

  if (earlyCurrentStep >= earlySlotCount)
    return;

  int slotNum = earlySlots[earlyCurrentStep];
  int sIdx = slotNum - 1;

  // 1. Thẻ thông tin ngăn hiện tại to rõ nét
  int cardX = 10, cardY = HDR_H + 6, cardW = W - 20, cardH = 120;
  tft.fillRoundRect(cardX, cardY, cardW, cardH, 8, C_CARD);
  tft.drawRoundRect(cardX, cardY, cardW, cardH, 8, C_ACC);

  // Tiêu đề số ngăn
  char slotBadge[36];
  snprintf(slotBadge, sizeof(slotBadge), "[ DANG MO: NGAN %d ]", slotNum);
  tft.setTextColor(C_ACC, C_CARD);
  tft.drawCentreString(slotBadge, W / 2, cardY + 8, 2);

  // Tên thuốc lớn
  const char *mName = slotHasMed(sIdx) ? meds[sIdx].name : "Thuoc";
  tft.setTextColor(C_FG, C_CARD);
  tft.drawCentreString(mName, W / 2, cardY + 30, 4);

  // Liều dùng cần lấy
  char doseStr[48];
  uint8_t dVal = meds[sIdx].dose ? meds[sIdx].dose : 1;
  snprintf(doseStr, sizeof(doseStr), "HAY LAY: %d VIEN", dVal);
  tft.setTextColor(C_OK, C_CARD);
  tft.drawCentreString(doseStr, W / 2, cardY + 68, 4);

  // Dòng nhắc nhở
  tft.setTextColor(C_DIM, C_CARD);
  char remPillStr[40];
  snprintf(remPillStr, sizeof(remPillStr), "(Con lai trong ngan: %d vien)",
           meds[sIdx].qty);
  tft.drawCentreString(remPillStr, W / 2, cardY + 98, 2);

  // 2. Hộp nút bấm hành động xác nhận đã lấy thuốc
  int btnY = 158, btnH = 52, btnW = W - 24, btnX = 12;
  tft.fillRoundRect(btnX, btnY, btnW, btnH, 8, C_SEL);
  tft.drawRoundRect(btnX, btnY, btnW, btnH, 8, C_ACC);

  tft.setTextColor(C_FG, C_SEL);
  if (earlyCurrentStep < earlySlotCount - 1) {
    int nextSlotNum = earlySlots[earlyCurrentStep + 1];
    int nextSIdx = nextSlotNum - 1;
    const char *nxtName = slotHasMed(nextSIdx) ? meds[nextSIdx].name : "Thuoc";
    uint8_t nxtDose = meds[nextSIdx].dose ? meds[nextSIdx].dose : 1;

    tft.drawCentreString(">>> BAM PUSH: DA LAY XONG NGAN NAY <<<", W / 2,
                         btnY + 10, 2);

    char nxtInfo[64];
    snprintf(nxtInfo, sizeof(nxtInfo),
             "* Tiep theo se xoay den: Ngan %d (%s - %dv)", nextSlotNum,
             nxtName, nxtDose);
    tft.setTextColor(C_ACC, C_SEL);
    tft.drawCentreString(nxtInfo, W / 2, btnY + 30, 1);
  } else {
    tft.drawCentreString(">>> BAM PUSH: DA LAY XONG NGAN CUOI <<<", W / 2,
                         btnY + 10, 2);

    tft.setTextColor(C_ACC, C_SEL);
    tft.drawCentreString("* Hoan tat cu uong -> Tu dong khoa ve Ngan 0 *",
                         W / 2, btnY + 30, 1);
  }
}

// ----------------------------------------------------------------------------
// MÀN HÌNH CẢNH BÁO CỮ THUỐC CHUYÊN BIỆT & ĐẾM NGƯỢC TIMEOUT
// ----------------------------------------------------------------------------
void drawDueAlert(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  FsmState st = fsmGetState();
  int actSlot = fsmGetActiveSlot();
  int dispSlot = (actSlot >= 1 && actSlot <= N_SLOTS) ? actSlot : currentMotorSlot;
  uint32_t remainSec = fsmGetRemainingTimeoutSec();
  uint32_t totalSec = (st == FSM_ALERTING_DUE) ? 900 : 300;
  if (totalSec == 0)
    totalSec = 900;

  int min = remainSec / 60;
  int sec = remainSec % 60;
  char timeStr[12];
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d", min, sec);

  uint16_t timerCol;
  if (st == FSM_ALERTING_DUE) {
    timerCol = (remainSec <= 180) ? C_RED : C_WARN;
  } else if (st == FSM_ROTATING_TO_DOSE) {
    timerCol = C_ACC; // Màu xanh Cyan phát sáng, chuẩn bị sẵn sàng 05:00
  } else {
    timerCol = (remainSec <= 60) ? C_RED : C_ACC;
  }

  if (full) {
    tft.fillScreen(C_BG);

    // 1. BANNER TIÊU ĐỀ TRÊN CÙNG (Y: 4..36, H = 32)
    if (st == FSM_ALERTING_DUE) {
      tft.fillRoundRect(8, 4, W - 16, 32, 6, 0x8800);
      tft.drawRoundRect(8, 4, W - 16, 32, 6, C_RED);
      tft.setTextColor(C_FG, 0x8800);
      tft.drawCentreString("[!] CANH BAO: DEN GIO UONG THUOC!", W / 2, 11, 2);
    } else if (st == FSM_ROTATING_TO_DOSE) {
      tft.fillRoundRect(8, 4, W - 16, 32, 6, 0x21E0);
      tft.drawRoundRect(8, 4, W - 16, 32, 6, C_WARN);
      tft.setTextColor(C_FG, 0x21E0);
      char hdr[48];
      snprintf(hdr, sizeof(hdr), ">>> DANG XOAY MO NGAN %d (CUA 6H) <<<", actSlot);
      tft.drawCentreString(hdr, W / 2, 11, 2);
    } else if (st == FSM_WAITING_TAKEN) {
      tft.fillRoundRect(8, 4, W - 16, 32, 6, 0x02E5);
      tft.drawRoundRect(8, 4, W - 16, 32, 6, C_OK);
      tft.setTextColor(C_FG, 0x02E5);
      char hdr[48];
      snprintf(hdr, sizeof(hdr), ">>> DANG MO NGAN %d O CUA 6H <<<",
               dispSlot);
      tft.drawCentreString(hdr, W / 2, 11, 2);
    } else {
      tft.fillRoundRect(8, 4, W - 16, 32, 6, 0x21E0);
      tft.drawRoundRect(8, 4, W - 16, 32, 6, C_WARN);
      tft.setTextColor(C_FG, 0x21E0);
      tft.drawCentreString("DANG DIEU KHIEN KHAY THUOC...", W / 2, 11, 2);
    }

    // 2. KHUNG ĐẾM NGƯỢC TIMEOUT (Y: 40, H = 76, X: 8, W = 304)
    tft.fillRoundRect(8, 40, W - 16, 76, 8, C_CARD);
    tft.drawRoundRect(8, 40, W - 16, 76, 8, timerCol);

    tft.setTextColor(C_DIM, C_CARD);
    if (st == FSM_ALERTING_DUE) {
      tft.drawCentreString("THOI GIAN CHO XAC NHAN (15 PHUT):", W / 2, 44, 2);
    } else if (st == FSM_ROTATING_TO_DOSE) {
      tft.drawCentreString("CHUAN BI MO NGAN THUOC (CHO 5 PHUT):", W / 2, 44, 2);
    } else {
      tft.drawCentreString("TU DONG DONG KHAY AN TOAN SAU (5 PHUT):", W / 2, 44, 2);
    }

    // 3. KHUNG THÔNG TIN THUỐC (Y: 120, H = 66, X: 8, W = 304)
    tft.fillRoundRect(8, 120, W - 16, 66, 8, C_CARD);
    tft.drawRoundRect(8, 120, W - 16, 66, 8, C_BORDER);

    if (st == FSM_ALERTING_DUE) {
      tft.setTextColor(C_ACC, C_CARD);
      tft.drawString("CAC NGAN THUOC CAN UONG:", 16, 124, 2);

      // Liệt kê các ngăn trong dueMask
      char medList[80] = "";
      bool first = true;
      for (int s = 0; s < N_SLOTS; s++) {
        if ((dueMask >> s) & 1) {
          char item[32];
          const char *nm = slotHasMed(s) ? meds[s].name : "Thuoc";
          uint8_t d = meds[s].dose ? meds[s].dose : 1;
          snprintf(item, sizeof(item), "%sN%d:%s(%dv)", first ? "" : "  ",
                   s + 1, nm, d);
          first = false;
          strncat(medList, item, sizeof(medList) - strlen(medList) - 1);
        }
      }
      if (medList[0] == '\0') {
        snprintf(medList, sizeof(medList), "Ngan 1..%d", N_SLOTS);
      }
      tft.setTextColor(C_FG, C_CARD);
      tft.drawString(medList, 16, 144, 2);

      tft.setTextColor(C_DIM, C_CARD);
      tft.drawString("Khay dang dong kin tai Ngan 0 (Safe Home)", 16, 168, 1);
    } else if (st == FSM_ROTATING_TO_DOSE) {
      int s = actSlot - 1;
      tft.setTextColor(C_WARN, C_CARD);
      tft.drawString("[DANG DI CHUYEN NGAN THUOC DEN CUA 6H...]", 16, 124, 2);

      char medInfo[64];
      const char *nm =
          (s >= 0 && s < N_SLOTS && slotHasMed(s)) ? meds[s].name : "Thuoc";
      uint8_t d = (s >= 0 && s < N_SLOTS && meds[s].dose) ? meds[s].dose : 1;
      snprintf(medInfo, sizeof(medInfo), "Ngan %d: %s | Lieu uong: %d vien",
               actSlot, nm, d);
      tft.setTextColor(C_FG, C_CARD);
      tft.drawString(medInfo, 16, 144, 2);

      tft.setTextColor(C_ACC, C_CARD);
      tft.drawString("Vui long doi khay xoay dung vi tri cua 6h...", 16, 168, 1);
    } else {
      int s = dispSlot - 1;
      tft.setTextColor(C_OK, C_CARD);
      tft.drawString("[CUA LAY THUOC HUONG 6H DANG MO]", 16, 124, 2);

      char medInfo[64];
      const char *nm =
          (s >= 0 && s < N_SLOTS && slotHasMed(s)) ? meds[s].name : "Thuoc";
      uint8_t d = (s >= 0 && s < N_SLOTS && meds[s].dose) ? meds[s].dose : 1;
      snprintf(medInfo, sizeof(medInfo), "Ngan %d: %s | Lieu uong: %d vien",
               dispSlot, nm, d);
      tft.setTextColor(C_FG, C_CARD);
      tft.drawString(medInfo, 16, 144, 2);

      tft.setTextColor(C_WARN, C_CARD);
      tft.drawString("Vui long lay het thuoc ra khoi khay!", 16, 168, 1);
    }

    // 4. KHUNG NÚT THAO TÁC Ở CHÂN MÀN HÌNH (Y: 190, H = 46, X: 8, W = 304)
    tft.fillRoundRect(8, 190, W - 16, 46, 8, 0x02E5);
    tft.drawRoundRect(8, 190, W - 16, 46, 8, C_OK);
    tft.setTextColor(C_FG, 0x02E5);

    if (st == FSM_ALERTING_DUE) {
      tft.drawCentreString(">>> BAM PUSH / K0: XAC NHAN DEN LAY <<<", W / 2, 198, 2);
      tft.setTextColor(C_ACC, 0x02E5);
      tft.drawCentreString("* Xac nhan de bat dau mo khay lay thuoc *", W / 2,
                           220, 1);
    } else if (st == FSM_ROTATING_TO_DOSE) {
      tft.drawCentreString(">>> DANG XOAY KHAY, VUI LONG DOI... <<<", W / 2, 198, 2);
      tft.setTextColor(C_ACC, 0x02E5);
      tft.drawCentreString("* Khay dang xoay den vi tri cua mo 6h an toan *", W / 2,
                           220, 1);
    } else {
      int nextSlot = 0;
      for (int s = 0; s < N_SLOTS; s++) {
        if (((dueMask >> s) & 1) && (s + 1 != dispSlot)) {
          nextSlot = s + 1;
          break;
        }
      }
      if (nextSlot > 0) {
        tft.drawCentreString(">>> BAM PUSH / K0: DA LAY -> QUA NGAN TIEP <<<", W / 2,
                             198, 2);
        char sub[48];
        snprintf(sub, sizeof(sub), "* Tiep theo se xoay den Ngan %d *",
                 nextSlot);
        tft.setTextColor(C_ACC, 0x02E5);
        tft.drawCentreString(sub, W / 2, 220, 1);
      } else {
        tft.drawCentreString(">>> BAM PUSH / K0: DA LAY -> HOAN TAT CU <<<",
                             W / 2, 198, 2);
        tft.setTextColor(C_ACC, 0x02E5);
        tft.drawCentreString("* Hoan tat cu uong -> Tu dong khoa ve Ngan 0 *",
                             W / 2, 220, 1);
      }
    }
  }

  // --- CẬP NHẬT SỐ ĐẾM NGƯỢC (Realtime Countdown Digits) ---
  tft.fillRect(40, 60, W - 80, 38, C_CARD);
  tft.setTextColor(timerCol, C_CARD);
  int tw = tft.textWidth(timeStr, 7);
  int tx = (W - tw) / 2;
  tft.drawString(timeStr, tx, 58, 7);

  // --- CẬP NHẬT THANH TIẾN TRÌNH CO NGẮN (Progress Bar Y: 102, H: 6) ---
  int barMaxW = W - 36; // 284px
  int barW = (int)((remainSec * (uint32_t)barMaxW) / totalSec);
  if (barW > barMaxW)
    barW = barMaxW;
  if (barW < 0)
    barW = 0;

  tft.fillRect(18, 102, barW, 6, timerCol);
  if (barW < barMaxW) {
    tft.fillRect(18 + barW, 102, barMaxW - barW, 6, C_BG);
  }
}

// ----------------------------------------------------------------------------
// 5. ĐIỀU PHỐI RENDER CHUNG
// ----------------------------------------------------------------------------
void render(bool full) {
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  switch (screen) {
  case S_HOME:
    drawHome(full);
    break;
  case S_MENU:
    drawMenu(full);
    break;
  case S_SCHED:
    drawSched(full);
    break;
  case S_SCHED_DETAIL:
    drawSchedDetail(full);
    break;
  case S_EDIT:
    drawEdit(full);
    break;
  case S_PICK:
    drawPick(full);
    break;
  case S_MEDS:
    drawMeds(full);
    break;
  case S_MED:
    drawMed(full);
    break;
  case S_NAME:
    drawName(full);
    break;
  case S_NUM:
    drawNum(full);
    break;
  case S_HIST:
    drawHist(full);
    break;
  case S_CALIB:
    drawCalib(full);
    break;
  case S_REFILL_QTY:
    drawRefillQty(full);
    break;
  case S_REFILL_CONFIRM:
    drawRefillConfirm(full);
    break;
  case S_EARLY_SESSION:
    drawEarlySession(full);
    break;
  case S_EARLY_CONFIRM:
    drawEarlyConfirm(full);
    break;
  case S_EARLY_DISPENSE:
    drawEarlyDispense(full);
    break;
  case S_REFILL_WEB:
    drawRefillWeb(full);
    break;
  case S_CLEAR_CONFIRM:
    drawClearConfirm(full);
    break;
  case S_DUE_ALERT:
    drawDueAlert(full);
    break;
  }
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
}
