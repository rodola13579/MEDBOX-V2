#pragma once

/**
 * ============================================================================
 * MEDBOX PILL DISPENSER — GLOBAL APPLICATION STATE & DATA CONTRACTS
 * Nền tảng: ESP32-S3-WROOM-1 N16R8 | Hệ điều hành: FreeRTOS + Arduino
 * ============================================================================
 */

#include <Arduino.h>

// ----------------------------------------------------------------------------
// 1. CÁC MÀN HÌNH GIAO DIỆN (SCREENS)
// ----------------------------------------------------------------------------
enum Screen : uint8_t {
  S_HOME,         // Màn hình chính Medbox
  S_MENU,         // Menu điều khiển (4 task chính tối ưu)
  S_SCHED,        // Task 1: Đặt lịch uống thuốc (Danh sách mốc giờ)
  S_SCHED_DETAIL, // Chi tiết 1 mốc giờ: Bật/Tắt từng ngăn, Sửa, Xóa
  S_EDIT,         // Chỉnh giờ / phút
  S_PICK,         // Chọn ngăn thuốc ứng với mốc giờ
  S_MEDS,         // Task 2: Thêm & quản lý 7 ngăn thuốc
  S_MED,          // Chi tiết 1 ngăn thuốc
  S_NAME,         // Nhập tên thuốc
  S_NUM,          // Nhập số lượng tồn kho / liều dùng
  S_HIST,         // Task 4: Xem lịch sử uống thuốc
  S_CALIB,        // Căn chỉnh & Hiệu chuẩn khay xoay (Ngăn 0 an toàn)
  S_REFILL_QTY,   // Bước 1 Nạp thuốc: Chọn số lượng viên nạp thêm
  S_REFILL_CONFIRM, // Bước 2 Nạp thuốc: Xác nhận đã bỏ thuốc vào ngăn -> Tự
                    // quay về Ngăn 0
  S_EARLY_SESSION, // Bước 1 Uống sớm: Chọn theo lịch từng buổi trong ngày
  S_EARLY_CONFIRM, // Bước 2 Uống sớm: Hiển thị các ngăn trong buổi & nút xác
                   // nhận lấy
  S_EARLY_DISPENSE, // Bước 3 Uống sớm: Đang mở lần lượt từng ngăn & bấm xác
                    // nhận chuyển tiếp
  S_REFILL_WEB, // Chế độ nạp thuốc điều khiển từ Web Dashboard (không vào màn
                // hình thông tin ngăn)
  S_CLEAR_CONFIRM, // Xóa ngăn thuốc: Khay xoay đến ngăn để lấy thuốc ra và bấm
                   // xác nhận
  S_DUE_ALERT // Màn hình cảnh báo cữ thuốc chuyên biệt & đếm ngược timeout
};

// ----------------------------------------------------------------------------
// 2. CHẾ ĐỘ HIỂN THỊ WI-FI
// ----------------------------------------------------------------------------
enum WifiState : uint8_t {
  WIFI_ST_OFF = 0,        // Tắt / Mất kết nối
  WIFI_ST_CONNECTING = 1, // Đang quét / Kết nối
  WIFI_ST_CONNECTED = 2,  // Đã kết nối Internet (Online)
  WIFI_ST_AP = 3          // Chế độ phát trạm AP Cấu hình
};

// ----------------------------------------------------------------------------
// 3. ĐỊNH NGHĨA DỮ LIỆU DỰ ÁN
// ----------------------------------------------------------------------------
#define N_SLOTS 7 // 7 ngăn chứa thuốc (Slot 1..7)
#define N_SCHED                                                                \
  28 // Tối đa 28 mốc giờ đặt lịch (đáp ứng tối đa 28 cữ khác nhau)
#define MAX_PER_TIME                                                           \
  N_SLOTS // Không giới hạn số ngăn đặt 1 lần (cho phép chọn tối đa cả 7 ngăn)
#define MAX_SCHEDS_PER_SLOT 4 // Mỗi ngăn tối đa 4 cữ uống trong ngày
#define MAX_SLOT_SCHEDS 28    // Tổng tối đa 28 mốc lịch (7 ngăn x 4 cữ)
#define NAME_LEN 10           // Độ dài tối đa tên thuốc
#define N_HIST 30             // Lưu 30 nhật ký gần nhất
#define LOW_QTY 5             // Báo sắp hết khi <= 5 viên
#define MISS_MIN 30           // Quá 30 phút không uống = Bỏ lỡ

#define H_TAKEN 0  // Đã uống đúng giờ
#define H_MISS 1   // Bỏ lỡ cữ thuốc
#define H_MANUAL 2 // Uống thủ công (Lấy ngay)

#define ET_TIME 0  // Sửa mốc giờ uống thuốc
#define ET_CLOCK 1 // Chỉnh đồng hồ hệ thống
#define PK_SCHED 0 // Chọn ngăn cho mốc giờ
#define PK_DISP 1  // Chọn ngăn để lấy thuốc ngay
#define NT_QTY 0   // Nhập số lượng tồn kho
#define NT_DOSE 1  // Nhập liều dùng mỗi lần

struct Med {
  char name[NAME_LEN + 1];
  uint8_t qty;  // Số viên còn trong ngăn
  uint8_t dose; // Số viên uống mỗi lần
};

struct Sched {
  uint8_t h;          // Giờ
  uint8_t m;          // Phút
  uint8_t mask;       // Bitmask ngăn thuốc: bit 0 = Ngăn 1, bit 1 = Ngăn 2...
  uint8_t activeMask; // Bitmask các ngăn đang BẬT (1 = BẬT, 0 = TẮT). Mặc định
                      // = mask
};

struct SlotSchedule {
  uint8_t slot;   // 0..6 (tương ứng Ngăn 1..7)
  uint8_t hour;   // 0..23
  uint8_t minute; // 0..59
  uint8_t dose;   // 1..5 viên
  uint8_t active; // 1 = BẬT, 0 = TẮT
};

struct Hist {
  uint8_t day;       // 0..6 (T2..CN)
  uint16_t min;      // 0..1439 (Thời gian thực tế ghi nhận)
  uint8_t slot;      // 0..6 (Ngăn 1..7)
  uint8_t type;      // 0: H_TAKEN, 1: H_MISS, 2: H_MANUAL (Uống sớm)
  uint8_t session;   // 0: Sáng, 1: Trưa, 2: Chiều, 3: Tối, 0xFF: None
  uint8_t sched_h;   // Giờ cữ hẹn (0xFF nếu không có)
  uint8_t sched_m;   // Phút cữ hẹn (0xFF nếu không có)
};

// ----------------------------------------------------------------------------
// 4. BIẾN DỮ LIỆU TOÀN CỤC (EXTERN CHO CÁC MODULE CÙNG DÙNG)
// ----------------------------------------------------------------------------
extern const char CHARSET[];
#define CS_N 39

extern const char *DAY_NAMES[7];
extern const char *DAY_FULL[7];
extern const char *MENU_ITEMS[4]; // 4 task chính tối ưu

extern Screen screen;
extern Med meds[N_SLOTS];
extern Sched sched[N_SCHED];
extern uint8_t nSched;
extern SlotSchedule slotSched[MAX_SLOT_SCHEDS];
extern uint8_t nSlotSched;

int countSlotSchedules(uint8_t slot);
void rebuildGroupedScheds();
bool setSlotScheduleActive(uint8_t slot, uint8_t h, uint8_t m, bool active);
bool upsertSlotSchedule(uint8_t slot, uint8_t h, uint8_t m, uint8_t dose,
                        bool active);
bool removeSlotSchedule(uint8_t slot, uint8_t h, uint8_t m);
void removeSlotSchedulesBySlot(uint8_t slot);
void removeTimeCluster(uint8_t h, uint8_t m);

extern Hist hist[N_HIST];
extern uint8_t histHead, histCount;
extern uint8_t dueMask;
extern uint32_t dueAbs[N_SLOTS];

extern int menuSel, schedSel, medsSel, medRow, curSlot, histTop;
extern int schedDetailSel;
extern int schedIdx;
extern uint8_t editTarget;
extern int editField, editVal[3];
extern uint8_t pickMode;
extern uint8_t pickMask;
extern int pickSel;
extern int8_t nameIdx[NAME_LEN];
extern int namePos;
extern bool nameWasNew;
extern uint8_t numTarget;
extern int numVal, numMin, numMax;
extern int refillAddQty;
extern bool homeDirty;

// Quản lý trạng thái cữ uống thuốc trong ngày (Chống uống trùng lặp - Anti
// Double Dose)
extern uint16_t schedTakenMask; // Bitmask: bit i = cữ sched[i] đã uống hôm nay
extern uint8_t schedTakenDay;   // Ngày hiện tại lưu vết cữ đã uống (0..6)
extern int earlySchedSel;       // Chỉ số mốc giờ được chọn
extern int earlySessionSel;     // 0: Sáng, 1: Trưa, 2: Chiều, 3: Tối
extern uint8_t earlySlots[N_SLOTS]; // Danh sách các số ngăn trong buổi (1..7)
extern uint8_t earlySlotCount;      // Tổng số ngăn có trong buổi
extern uint8_t
    earlyCurrentStep; // Ngăn hiện tại đang mở trong buổi (0..earlySlotCount-1)
bool isSchedTakenToday(int idx);
void markSchedTakenToday(int idx);
void resetSchedTakenIfNewDay();
const char *getSessionName(uint8_t h);

// 4 Buổi cố định trong ngày (Sáng, Trưa, Chiều, Tối)
extern const char *const SESSION_NAMES[4];
extern const char *const SESSION_HOURS[4];
int getSessionIndexFromHour(uint8_t h);
uint8_t getSessionMask(int sessionIdx);
int getSessionSlotCount(int sessionIdx);
bool isSessionTakenToday(int sessionIdx);
void markSessionTakenToday(int sessionIdx);
void getSessionTimeDesc(int sessionIdx, char *buf, size_t len);

// Môi trường & Wi-Fi
extern float liveTemp;
extern float liveHum;
extern WifiState wifiState;

// Động cơ & Căn chỉnh khay
extern int currentMotorSlot;
extern int16_t homeOffset;
extern int16_t stepsPerSlot;
extern int calibSel;
extern int calibTestSlot;
extern int16_t jogDelta;

// ----------------------------------------------------------------------------
// 5. CÁC HÀM TIỆN ÍCH TRUY VẤN
// ----------------------------------------------------------------------------
uint32_t absSec();
int curMinOfDay();
int curDay();
bool slotHasMed(int s);
bool slotCanDispense(int s);
void maskStr(uint8_t mask, char *o, size_t n);
bool findNext(int &idxOut, int &minsOut);
void fmtRemain(char *o, size_t n, int m);
bool slotUsable(int s);
int editNF();
int visRows();
int schedRows();

// Điều hướng trang, Nghiệp vụ thuốc & Lưu cấu hình
void goTo(Screen s);
void toggleWifiMode();
void saveCfg();
void sortSched();
void addHist(int slot, uint8_t type, uint8_t sessionIdx = 0xFF, uint8_t schedH = 0xFF, uint8_t schedM = 0xFF);
void consumeDose(int s);

// Đồng bộ thời gian hệ thống (từ NTP hoặc Backend MQTT)
void syncSystemTime(uint8_t day, uint8_t hour, uint8_t min, uint8_t sec);
extern uint32_t baseSec;
extern uint8_t baseDay;
extern uint32_t baseMs;

// Khôi phục cài đặt gốc và xóa sạch bộ nhớ NVS
void factoryResetNVS();

// Đặt lại bộ đếm thời gian hoạt động & đánh thức màn hình nếu đang tắt
