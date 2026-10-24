#pragma once

/**
 * ============================================================================
 * MEDBOX PILL DISPENSER — DISPLAY UI SUBSYSTEM HEADER
 * Nền tảng: ST7789 2.4" IPS (Nằm ngang 320x240, TFT_eSPI)
 * ============================================================================
 */

#include "app_state.h"
#include <Arduino.h>
#include <TFT_eSPI.h>

// ----------------------------------------------------------------------------
// ----------------------------------------------------------------------------
// 1. BẢNG MÀU DARK MODE Y TẾ CAO CẤP (RGB565) - TƯƠNG PHẢN CAO, CHỮ SIÊU RÕ NÉT
// ----------------------------------------------------------------------------
#define C_BG                                                                   \
  0x0000 // Nền Đen tuyền tuyệt đối (#000000) - Tương phản tối đa, triệt tiêu
         // đục mờ
#define C_FG 0xFFFF   // Trắng tinh (#FFFFFF) - Độ sáng cực đại
#define C_ACC 0x07FF  // Cyan y tế phát sáng (#00E5FF)
#define C_HDR 0x10A2  // Header xám than đậm (#101418)
#define C_SEL 0x04D3  // Xanh lá ngọc con trỏ chọn (#009B65)
#define C_DIM 0xDEDB  // Trắng bạc sáng (#DEE2E6) - Cực rõ ràng, không bị mờ tối
#define C_RED 0xF800  // Đỏ thuần tươi (#FF0000) - Chuẩn Dấu Thập Đỏ Y Tế
#define C_OK 0x07E0   // Xanh lục bảo (#00FF00)
#define C_WARN 0xFFE0 // Vàng hổ phách sáng (#FFFF00)
#define C_CARD 0x18C3 // Thẻ Card xám than cao cấp (#181818)
#define C_BORDER 0x52AA // Viền thẻ xám sáng rõ nét (#505555)
#define C_GREY 0x8410   // Xám viền phụ (#808080)

// Màu sắc tương phản cao cho Nhiệt độ & Độ ẩm
#define C_TEMP_ICON C_RED // Đỏ tươi thuần (#FF0000)
#define C_TEMP_LBL C_RED  // Chữ Nhiệt độ ĐỎ TƯƠI (#FF0000)
#define C_TEMP_VAL C_RED  // Số nhiệt độ ĐỎ TƯƠI (#FF0000)

#define C_HUM_ICON 0x07FF // Xanh ngọc Cyan (#00E5FF)
#define C_HUM_LBL 0x07FF  // Nhãn Độ ẩm Cyan sáng (#00E5FF)
#define C_HUM_VAL 0x07FF  // Số độ ẩm XANH CYAN (#00E5FF)

#define C_WIFI_AP 0xBA38 // Tím Hotspot AP (#BA68C8)

// ----------------------------------------------------------------------------
// 2. ĐỐI TƯỢNG VẼ & BỐ CỤC MÀN HÌNH
// ----------------------------------------------------------------------------
extern TFT_eSPI tft;

extern int W, H;
extern int UI, HDR_H, BIG;
extern int clockY, cardY, cardH, tileY, tileH, cellW, tileW;

// Khởi tạo hiển thị & tính toán kích thước
void displayInit();
void calcLayout();

// Điều phối render màn hình
void render(bool full);

// Màn hình 1: Trang chủ Medbox
void drawHome(bool full);
void drawHomeHeader();
void drawHomeClock();
void drawHomeCard(); // Khung Chữ Thập Y Tế
void drawTile(int i);
void drawWifiIndicator(int x, int y, WifiState state);
void drawMedicalCross(int cx, int cy, int size, uint16_t crossColor,
                      uint16_t badgeBg, uint16_t badgeBorder);

// Màn hình 2: Menu 4 tác vụ chính
void drawMenu(bool full);
void drawMenuRow(int i);
int menuRowH();

// Màn hình 3: Danh sách đặt lịch & Chi tiết mốc giờ
void drawSched(bool full);
void drawSchedRow(int idx);
void drawSchedDetail(bool full);

// Màn hình 4: Chỉnh giờ uống / đồng hồ
void drawEdit(bool full);

// Màn hình 5: Chọn ngăn thuốc ứng với mốc giờ
void drawPick(bool full);
void drawPickCell(int i);
void drawPickInfo();

// Màn hình 6: Danh sách 7 ngăn thuốc
void drawMeds(bool full);
void drawMedsRow(int idx);

// Màn hình 7: Chi tiết ngăn thuốc
void drawMed(bool full);
void drawMedRow(int r);

// Màn hình 8: Nhập tên thuốc
void drawName(bool full);
void drawNameBox(int i);

// Màn hình 9: Nhập số lượng / liều dùng
void drawNum(bool full);

// Màn hình 10: Xem nhật ký lịch sử uống thuốc
void drawHist(bool full);
void drawHistRow(int r);

// Màn hình 11: Căn chỉnh & Hiệu chuẩn khay xoay (Ngăn 0)
void drawCalib(bool full);

// Màn hình Nạp thuốc 2 bước: Chọn số lượng nạp thêm & Xác nhận đã bỏ thuốc vào
// ngăn
void drawRefillQty(bool full);
void drawRefillConfirm(bool full);
void drawRefillWeb(bool full);
void drawClearConfirm(bool full);

// Màn hình Uống thuốc sớm theo buổi (chọn cữ, xác nhận & đang lấy từng ngăn)
void drawEarlySession(bool full);
void drawEarlyConfirm(bool full);
void drawEarlyDispense(bool full);

// Màn hình cảnh báo cữ thuốc chuyên biệt & đếm ngược timeout
void drawDueAlert(bool full);

// Các hàm tiện ích đồ họa
void drawHeader(const char *title);
void drawHint(const char *s);
void drawRowAt(int row, bool sel, uint16_t fg, const char *txt);
void printCentered(const char *s, int y);
void toast(const char *msg);
