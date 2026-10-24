# SMARTMED PILL DISPENSER — BÁO CÁO ĐÁNH GIÁ & ĐỀ XUẤT TỐI ƯU HÓA LOGIC FIRMWARE

> **Hệ thống:** ESP32-S3-WROOM-1 N16R8 (16MB Flash Octal, 8MB PSRAM Octal)  
> **Phần mềm:** PlatformIO + Arduino Framework + FreeRTOS  
> **Tài liệu tham chiếu:** [fsm_controller.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/fsm_controller.cpp), [main.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/main.cpp), [display_ui.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/display_ui.cpp), [mqtt_manager.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/mqtt_manager.cpp), [motor_control.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/motor_control.cpp), [audio_player.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/audio_player.cpp)  
> **Ngày lập báo cáo:** 24/10/2026

---

## 📑 MỤC LỤC
1. [Tổng quan hiện trạng & Điểm sáng của Firmware](#1-tổng-quan-hiện-trạng--điểm-sáng-của-firmware)
2. [Chi tiết 5 vấn đề logic & Xung đột thiết kế](#2-chi-tiết-5-vấn-đề-logic--xung-đột-thiết-kế)
   - [Vấn đề 1: Màn hình cảnh báo cữ thuốc S_DUE_ALERT không bao giờ hiển thị (Dead Screen)](#vấn-đề-1-màn-hình-cảnh-báo-cữ-thuốc-s_due_alert-không-bao-giờ-hiển-thị-dead-screen)
   - [Vấn đề 2: Khuyết thiếu trạng thái nạp thuốc (REFILL) trong máy trạng thái FSM](#vấn-đề-2-khuyết-thiếu-trạng-thái-nạp-thuốc-refill-trong-máy-trạng-thái-fsm)
   - [Vấn đề 3: Thiếu chốt bảo vệ trạng thái (State Guard Clauses) khi nhận lệnh ngoài](#vấn-đề-3-thiếu-chốt-bảo-vệ-trạng-thái-state-guard-clauses-khi-nhận-lệnh-ngoài)
   - [Vấn đề 4: Thừa hành trình động cơ khi uống nhiều ngăn trong cùng 1 buổi](#vấn-đề-4-thừa-hành-trình-động-cơ-khi-uống-nhiều-ngăn-trong-cùng-1-buổi)
   - [Vấn đề 5: Xung đột cạnh tranh giữa Backend Scheduler và Firmware Scheduler](#vấn-đề-5-xung-đột-cạnh-tranh-giữa-backend-scheduler-và-firmware-scheduler)
   - [Vấn đề 6: Lỗi phát lặp 2 lần âm thanh khi bấm Hủy nạp thuốc từ Web](#vấn-đề-6-lỗi-phát-lặp-2-lần-âm-thanh-khi-bấm-hủy-nạp-thuốc-từ-web)
   - [Vấn đề 7: Cảm giác máy bị đơ lúc khởi động & Biểu tượng Wi-Fi không tự cập nhật](#vấn-đề-7-cảm-giác-máy-bị-đơ-lúc-khởi-động--biểu-tượng-wi-fi-không-tự-cập-nhật)
3. [Bảng ma trận so sánh Trước & Sau tối ưu (Before vs After Matrix)](#3-bảng-ma-trận-so-sánh-trước--sau-tối-ưu-before-vs-after-matrix)
4. [Lộ trình code thực chiến chi tiết từng file](#4-lộ-trình-code-thực-chiến-chi-tiết-từng-file)

---

## 1. TỔNG QUAN HIỆN TRẠNG & ĐIỂM SÁNG CỦA FIRMWARE

Mã nguồn Firmware SmartMed hiện tại được xây dựng với cấu trúc module hóa rất tốt, áp dụng đúng chuẩn kỹ thuật nhúng cao cấp:

* **Kiến trúc Offline-First thực thụ:** Toàn bộ lịch uống thuốc, cấu hình tên thuốc, liều dùng, tồn kho và nhật ký đều được lưu trữ trực tiếp trong bộ nhớ Flash NVS (`Preferences` namespace `"medbox"`). Khi mất kết nối mạng Wi-Fi/MQTT, thiết bị vẫn hoàn toàn tự vận hành đúng giờ, phát âm thanh và quay khay chính xác.
* **Tách biệt đa nhân (Dual-Core Task Separation):** Phân hệ giải mã âm thanh MP3 I2S (MAX98357A) được ghim độc lập vào **Core 0** bằng FreeRTOS Task và Mutex (`audioCore0Task`), giải phóng **Core 1** hoàn toàn cho vòng quét giao diện ST7789, núm xoay EC11 và phát xung động cơ bước.
* **Xử lý ngắt núm xoay EC11 chống dội (Debounced Quadrature Interrupt):** Hàm `encISR()` chạy trong bộ nhớ ram tốc độ cao `IRAM_ATTR` với bảng chuyển đổi trạng thái Gray-code 16 phần tử, đọc xoay mượt mà, không giật lag.
* **Cơ chế bảo vệ phần cứng động cơ & ULN2003:** Hàm `motorDeenergize()` tự động ngắt điện toàn bộ 4 cuộn dây của 28BYJ-48 sau mỗi lần hoàn thành bước xoay, ngăn chặn triệt để tình trạng nóng IC công suất và cháy cuộn dây.

---

## 2. CHI TIẾT 5 VẤN ĐỀ LOGIC & XUNG ĐỘT THIẾT KẾ

### Vấn đề 1: Màn hình cảnh báo cữ thuốc & Thông tin từng ngăn thuốc `S_DUE_ALERT` không hiển thị (Dead Screen)

#### A. Trả lời câu hỏi: Hiện tại code có phần hiển thị thông tin mỗi ngăn không?
👉 **CÓ, trong code đã được viết ĐẦY ĐỦ 100% phần vẽ giao diện thông tin chi tiết từng ngăn thuốc!**

Cụ thể, trong [display_ui.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/display_ui.cpp#L1500-L1605), hàm `drawDueAlert()` đã phân chia thành 2 chế độ hiển thị rất chi tiết:

1. **Khi chuông đang reo (`st == FSM_ALERTING_DUE`):**
   - Banner đỏ: `[!] CANH BAO: DEN GIO UONG THUOC!`
   - Đếm ngược: `THOI GIAN CHO XAC NHAN (15 PHUT)` (đổi sang màu đỏ khi < 3 phút).
   - Liệt kê toàn bộ các ngăn thuốc và liều dùng cần uống trong cữ: `N1: Paracetamol (1v)  N2: Vitamin C (2v)`.
   - Nút hành động: `BAM PUSH / K0: XAC NHAN DEN LAY`.

2. **Khi đã bấm nút và khay đang mở tại từng ngăn (`st == FSM_WAITING_TAKEN`):**
   - Banner xanh lá: `>>> DANG MO NGAN X O CUA 6H <<<`
   - Đếm ngược an toàn: `TU DONG DONG KHAY AN TOAN SAU (5 PHUT)`.
   - **Thẻ thông tin đích danh ngăn đang mở:**
     * `[CUA LAY THUOC HUONG 6H DANG MO]`
     * Tên thuốc & Liều dùng: `Ngan X: [Ten thuoc] | Lieu uong: [Y] vien`
     * Lời nhắc: `Vui long lay het thuoc ra khoi khay!`
   - **Chỉ dẫn nút bấm chuyển tiếp ngăn:**
     * Nếu còn ngăn tiếp theo: `>>> BAM PUSH / K0: DA LAY -> QUA NGAN TIEP <<<` (Kèm dòng phụ: `* Tiep theo se xoay den Ngan Y *`).
     * Nếu là ngăn cuối: `>>> BAM PUSH / K0: DA LAY XONG -> KHOA AN TOAN <<<` (Kèm dòng phụ: `* Khay se tu dong khoa ve Ngan 0 *`).

Ngoài ra, ở màn hình chính [drawHomeStatus()](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/display_ui.cpp#L305-L326) cũng đã có thẻ hiển thị `[MO NGAN X] Ten thuoc (Yv)` khi ở trạng thái `FSM_WAITING_TAKEN`.

---

#### B. Tại sao trên thực tế lại KHÔNG THẤY màn hình thông tin này xuất hiện?
Dù code UI đã được viết hoàn chỉnh, nhưng hệ thống **không bao giờ hiển thị được lên màn hình** do 3 nguyên nhân cốt lõi trong logic điều phối:

1. **Thiếu lệnh kích hoạt màn hình `goTo(S_DUE_ALERT)`:**
   Khi đến giờ uống thuốc, [tickSchedule()](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/main.cpp#L676) gọi `fsmTriggerDueAlert(newMask)`. Nhưng trong [fsm_controller.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/fsm_controller.cpp#L107-L113), hàm chỉ đổi FSM sang `FSM_ALERTING_DUE` mà **không hề gọi `goTo(S_DUE_ALERT)`**. Do đó `screen` vẫn giữ nguyên là `S_HOME` (hoặc menu con).

2. **Khối lệnh vẽ thông tin ngăn bị chặn điều kiện (Blocked Render Gate):**
   Khi người bệnh bấm nút xác nhận uống thuốc, FSM chuyển sang mở ngăn `FSM_WAITING_TAKEN`. Trong [fsm_controller.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/fsm_controller.cpp#L126-L128):
   ```cpp
   case FSM_WAITING_TAKEN: {
       homeDirty = true;
       if (screen == S_DUE_ALERT) {
           render(true); // ❌ VÌ screen ĐANG LÀ S_HOME NÊN DÒNG NÀY BỊ BỎ QUA HOÀN TOÀN!
       }
       audioPlayDispenseSlot(activeSlot);
       break;
   }
   ```
   Do `screen` không phải là `S_DUE_ALERT`, lệnh `render(true)` **không bao giờ được thực thi**, dẫn đến màn hình không hề vẽ thẻ thông tin của ngăn thuốc vừa mở!

3. **Khi uống thuốc thủ công từ Web (`cmd/dispense`):**
   Hàm [fsmTriggerManualDispense()](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/fsm_controller.cpp#L302) chỉ xoay động cơ và chuyển sang `FSM_ROTATING_TO_DOSE`, hoàn toàn không chuyển `screen` sang `S_DUE_ALERT`. Vì thế màn hình trên hộp thuốc vẫn đứng im như không có chuyện gì xảy ra!

---

#### C. Hậu quả thực tế:
1. Người bệnh nghe thấy loa nói *"Xin mời lấy thuốc ở ngăn X"*, động cơ xoay ra vị trí cửa mở 6h, nhưng **màn hình LCD vẫn giữ nguyên màn hình cũ**, không hiển thị tên thuốc, không hiển thị số viên cần uống.
2. Người bệnh không biết ngăn này cần uống bao nhiêu viên nếu không nghe kịp âm thanh.
3. Người bệnh không biết thao tác bấm nút tiếp theo là gì (bấm nút để qua ngăn tiếp theo hay bấm để đóng khay).

---

#### D. Giải pháp kỹ thuật triệt để:
1. Khi đến cữ thuốc (`fsmTriggerDueAlert()`), gọi ngay `goTo(S_DUE_ALERT)` để hiển thị đếm ngược 15 phút.
2. Khi người bệnh bấm xác nhận mở ngăn (`fsmOnUserConfirmDose()`) hoặc khi nhận lệnh cấp thuốc từ Web (`fsmTriggerManualDispense()`):
   - Đảm bảo `screen = S_DUE_ALERT;`
   - Trong `FSM_WAITING_TAKEN`, gọi hàm `goTo(S_DUE_ALERT);` (hoặc `render(true);`) để vẽ toàn bộ thông tin: Tên thuốc, Liều lượng, Số ngăn và Nút bấm chuyển tiếp.
3. Khi đã uống hết toàn bộ các ngăn trong cữ (`dueMask == 0`) và khay khóa về Slot 0 an toàn: tự động gọi `goTo(S_HOME)` để trở về màn hình chính.

---

### Vấn đề 2: Khuyết thiếu trạng thái nạp thuốc (`REFILL`) trong máy trạng thái FSM

#### A. Hiện trạng code:
Trong [fsm_controller.h](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/include/fsm_controller.h#L9-L17), enum `FsmState` chỉ gồm:
```cpp
enum FsmState {
    FSM_HOMING_SEARCH = 0,
    FSM_SAFE_HOME_IDLE = 1,
    FSM_ALERTING_DUE = 2,
    FSM_ROTATING_TO_DOSE = 3,
    FSM_WAITING_TAKEN = 4,
    FSM_ROTATING_TO_HOME = 5,
    FSM_ERROR_JAMMED = 6
};
```
Khi người dùng thực hiện nạp thuốc qua Web ([fsmTriggerRefillStart(slot)](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/fsm_controller.cpp#L343-L354)):
```cpp
void fsmTriggerRefillStart(int slot) {
    if (slot < 1 || slot > N_SLOTS) return;
    motorRotateToSlot(slot);
    mqttPublishRefillReady(slot);
    audioPlayRefillSlot(slot);
    curSlot = slot - 1;
    goTo(S_REFILL_WEB);
    // ❌ BIẾN currentState VẪN GIỮ NGUYÊN LÀ FSM_SAFE_HOME_IDLE!
}
```

#### B. Điểm bất hợp lý:
1. Khay thuốc đang mở toang tại Ngăn `slot`, nhưng FSM vẫn tự nhận là `FSM_SAFE_HOME_IDLE` (trạng thái an toàn đóng kín nắp tại Slot 0).
2. Bản tin Telemetry MQTT phát lên Broker vẫn báo `"fsm_state": "SAFE_HOME_IDLE"`. Backend hiểu lầm thiết bị đang rảnh và nắp đang đóng.
3. Trong [fsm_controller.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/fsm_controller.cpp#L200-L206):
   ```cpp
   case FSM_SAFE_HOME_IDLE: {
       if (dueMask != 0) {
           fsmTriggerDueAlert(dueMask);
       }
       break;
   }
   ```
   Nếu đúng lúc người dùng đang nạp thuốc mà có lịch hẹn đến giờ, máy sẽ tự động kích hoạt chuông và xoay khay ngay lập tức trong khi tay người dùng vẫn đang thò vào ngăn!
4. Không có cơ chế Timeout bảo vệ: Nếu người dùng bấm mở ngăn nạp thuốc rồi bỏ quên, khay sẽ mở vô thời hạn, khiến thuốc bên trong bị ẩm mốc hoặc rơi vãi.

#### C. Giải pháp kỹ thuật:
1. Thêm trạng thái `FSM_REFILL_WAITING = 7` vào enum `FsmState` và mảng chuỗi `FSM_STATE_STRINGS`.
2. Khi bắt đầu nạp thuốc, chuyển sang `fsmTransitionTo(FSM_REFILL_WAITING)`.
3. Trong `fsmUpdate()`, bổ sung watchdog timeout 5 phút (300 giây) cho `FSM_REFILL_WAITING`: nếu người dùng quên không bấm "Khóa về Safe Home", thiết bị tự động xoay khay về Slot 0 để bảo vệ thuốc.

---

### Vấn đề 3: Thiếu chốt bảo vệ trạng thái (State Guard Clauses) khi nhận lệnh ngoài

#### A. Hiện trạng code:
Trong [fsm_controller.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/fsm_controller.cpp#L302-L340):
```cpp
void fsmTriggerManualDispense(int slot) {
    if (slot < 1 || slot > N_SLOTS) return;
    activeSlot = slot;
    isManualTake = true;
    fsmTransitionTo(FSM_ROTATING_TO_DOSE);
}
```
Và trong [fsmTriggerRefillStart()](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/fsm_controller.cpp#L343), hàm không hề kiểm tra trạng thái hiện tại của `currentState`.

#### B. Điểm bất hợp lý:
* Nếu thiết bị đang trong trạng thái kẹt cơ `FSM_ERROR_JAMMED` (đã ngắt điện động cơ để bảo vệ phần cứng), mà trên Web bấm "Cấp thuốc" hoặc "Nạp thuốc", lệnh MQTT sẽ gọi thẳng vào `fsmTriggerManualDispense()`, ép động cơ tiếp tục phát xung quay. Việc này có thể làm vỡ bánh răng giảm tốc hoặc cháy IC ULN2003.
* Nếu thiết bị đang mở khay chờ bệnh nhân lấy thuốc hẹn giờ (`FSM_WAITING_TAKEN`), một lệnh cấp thuốc thủ công gửi xuống sẽ đè nát cữ thuốc đang chờ, làm sai lệch toàn bộ cờ `dueMask`.

#### C. Giải pháp kỹ thuật:
Bổ sung Guard Clause:
```cpp
void fsmTriggerManualDispense(int slot) {
    if (slot < 1 || slot > N_SLOTS) return;
    if (currentState != FSM_SAFE_HOME_IDLE) {
        Serial.printf("[FSM WARN] Tu choi Manual Dispense: Thiet bi dang ban (%s)\n", fsmGetStateString());
        return;
    }
    // Tiến hành cấp thuốc...
}
```
Áp dụng tương tự cho `fsmTriggerRefillStart()` và `fsmTriggerEarlySession()`.

---

### Vấn đề 4: Thừa hành trình động cơ khi uống nhiều ngăn trong cùng 1 buổi

#### A. Hiện trạng code:
Trong [fsm_controller.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/fsm_controller.cpp#L134-L178):
```cpp
case FSM_ROTATING_TO_HOME: {
    homeDirty = true;
    motorRotateToSlot(0); // ❌ LUÔN QUAY VỀ NGĂN 0
    mqttPublishHomeReached();

    // Kiểm tra xem cữ thuốc này còn ngăn nào khác cần uống không
    int nextSlot = 0;
    for (int s = 0; s < N_SLOTS; s++) {
        if ((dueMask >> s) & 1) {
            nextSlot = s + 1;
            break;
        }
    }

    if (nextSlot > 0) {
        activeSlot = nextSlot;
        fsmTransitionTo(FSM_ROTATING_TO_DOSE); // ❌ TỪ 0 LẠI QUAY TIẾP RA NGĂN TIẾP THEO
    }
    ...
```

#### B. Phân tích cơ học:
Giả sử cữ Sáng bệnh nhân cần uống cả **Ngăn 1** và **Ngăn 2**:
1. Khay quay ra Ngăn 1 $\rightarrow$ Bệnh nhân lấy thuốc $\rightarrow$ Bấm nút K0 xác nhận.
2. Thiết bị quay từ **Ngăn 1 về Ngăn 0** (chạy ngược 512 bước).
3. Vừa về đến 0 xong, code phát hiện còn Ngăn 2 $\rightarrow$ Thiết bị lại quay từ **Ngăn 0 ra Ngăn 2** (chạy tới 1024 bước).
4. Bệnh nhân đứng chờ máy quay 2 lần liên tục với tổng hành trình 1536 bước!

#### C. Giải pháp kỹ thuật:
Khi bệnh nhân bấm lấy xong ngăn hiện tại:
- Kiểm tra `dueMask`: nếu **vẫn còn ngăn thuốc tiếp theo**, xoay trực tiếp từ ngăn hiện tại sang ngăn kế tiếp (ví dụ từ Ngăn 1 $\rightarrow$ Ngăn 2 chỉ mất 512 bước).
- Chỉ khi nào đã uống hết **toàn bộ các ngăn trong cữ** (`nextSlot == 0`), thiết bị mới hồi vị về Slot 0 an toàn.

---

### Vấn đề 5: Xung đột cạnh tranh giữa Backend Scheduler và Firmware Scheduler

#### A. Hiện trạng thiết kế:
* **Tại Firmware:** [tickSchedule()](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/main.cpp#L642) quét danh sách cữ thuốc mỗi phút theo đồng hồ nội tại.
* **Tại Backend:** [scheduler_engine.py](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/backend/services/scheduler_engine.py#L109-L179) chạy cron mỗi phút quét bảng `schedules` trong SQLite.

#### B. Kịch bản xung đột khi đến 08:00:
1. Đúng 08:00:00, ESP32 phát hiện cữ hẹn $\rightarrow$ `fsmTriggerDueAlert()` kích hoạt $\rightarrow$ ESP32 chuyển sang `FSM_ALERTING_DUE` $\rightarrow$ Phát telemetry `fsm_state = "ALERTING_DUE"`.
2. Đúng 08:00:01, Backend Scheduler quét thấy cữ 08:00 $\rightarrow$ gọi `dispensing_engine.dispense_now()`.
3. Trong [dispensing_engine.py:L134-L139](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/backend/services/dispensing_engine.py#L134-L139):
   ```python
   allowed_states = ("SAFE_HOME_IDLE",)
   if device_state.fsm_state not in allowed_states:
       raise HTTPException(
           status_code=409,
           detail=f"Thiết bị đang bận: {device_state.fsm_state}. Chờ về Slot 0 trước."
       )
   ```
4. Backend nhận thấy `device_state.fsm_state` đang là `"ALERTING_DUE"` $\rightarrow$ Backend ném lỗi **409 Conflict: Thiết bị đang bận** và tự động hủy bỏ phiên điều phối cữ thuốc trên Web Dashboard!

#### C. Giải pháp phân định vai trò kiến trúc:
* **Quy ước vai trò:** ESP32 là **Thiết bị Thực thi & An toàn cục bộ** (Local Safety Arbiter). Backend là **Bộ ghi nhật ký & Quản lý tập trung**.
* Khi đến giờ, ESP32 phát sự kiện MQTT `pillbox/{DEVICE_ID}/events/due_alert` kèm `mask`.
* Backend khi nhận được event `due_alert` sẽ tự động tạo phiên ghi nhận `dose_log` với trạng thái `PENDING` thay vì cố gắng gửi lệnh `cmd/dispense` chèn ngang.
* Trong [dispensing_engine.py](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/backend/services/dispensing_engine.py), nới lỏng `allowed_states` chấp nhận cả `"ALERTING_DUE"` khi lệnh phát sinh từ chính cữ hẹn đó.

---

### Vấn đề 6: Lỗi phát lặp 2 lần âm thanh khi bấm Hủy nạp thuốc từ Web

#### A. Hiện trạng code & Phân tích nguyên nhân:
Khi người dùng bấm nút **"Hủy nạp"** trên giao diện Web, hệ thống phát lặp lại 2 lần liên tiếp câu giọng nói:  
*🔊 "Đã hủy nạp thuốc, khay đã khóa về vị trí an toàn."*

Nguyên nhân gốc rễ xuất phát từ việc **phát kép 2 lệnh MQTT từ Backend và gộp chung nhánh xử lý trên Firmware**:

1. **Tại Backend ([backend/api/device.py:L306-L308](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/backend/api/device.py#L306-L308)):**
   ```python
   @router.post("/refill/cancel")
   async def cancel_refill():
       if device_state.is_online:
           cmd = build_home_cmd()
           # ❌ GỬI LIỀN LÚC 2 LỆNH MQTT CHO CÙNG 1 HÀNH ĐỘNG HỦY:
           await mqtt_client.publish(topic_cmd(DEVICE_ID, "home"), cmd)
           await mqtt_client.publish(topic_cmd(DEVICE_ID, "refill_cancel"), {"cmd": "REFILL_CANCEL"})
   ```
   Backend phát 2 gói tin MQTT liên tiếp: gói 1 vào topic `cmd/home`, gói 2 vào topic `cmd/refill_cancel`.

2. **Tại Firmware ([firmware/src/mqtt_manager.cpp:L289-L293](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/mqtt_manager.cpp#L289-L293)):**
   ```cpp
   // 3. LỆNH VỀ NHÀ SAFE HOME HOẶC HỦY NẠP THUỐC: cmd/home, cmd/refill_cancel
   else if (topicStr.endsWith("/cmd/home") || topicStr.endsWith("/cmd/refill_cancel")) {
       Serial.println("[MQTT] Lenh HOME / REFILL_CANCEL -> Quay ve Ngan 0 va man hinh S_HOME");
       fsmTriggerRefillCancel(); // ❌ CẢ 2 TOPIC ĐỀU GỌI CHUNG HÀM NÀY!
   }
   ```
   Firmware gộp chung cả `cmd/home` và `cmd/refill_cancel` vào một điều kiện `||`. Khi nhận gói `cmd/home`, hàm `fsmTriggerRefillCancel()` được gọi lần 1 $\rightarrow$ phát âm thanh lần 1. Ngay tích tắc sau, ESP32 nhận tiếp gói `cmd/refill_cancel` $\rightarrow$ hàm `fsmTriggerRefillCancel()` lại được gọi lần 2 $\rightarrow$ phát âm thanh lần 2!

#### B. Giải pháp kỹ thuật triệt để (2 lớp phòng vệ):
1. **Lớp 1 - Sửa Backend:** Trong `backend/api/device.py`, hàm `cancel_refill()` **chỉ gửi duy nhất 1 lệnh** `cmd/refill_cancel`, xóa bỏ dòng gửi `cmd/home` dư thừa.
2. **Lớp 2 - Tách bạch Topic & Chống dội (Debounce) trên Firmware:**
   - Tách riêng nhánh `cmd/refill_cancel` và nhánh `cmd/home`:
     * `cmd/refill_cancel`: chỉ gọi khi đang ở chế độ nạp thuốc $\rightarrow$ xoay về 0 và phát âm thanh `refill_cancel.mp3`.
     * `cmd/home`: lệnh hồi vị thông thường (từ tab kiểm thử hoặc thao tác chung) $\rightarrow$ chỉ xoay về 0 và phát sự kiện `home_reached`, không phát âm thanh hủy nạp thuốc.
   - Thêm bộ lọc chống nhận lệnh kép trong 1.500ms (`debounce`):
     ```cpp
     static unsigned long lastCancelMs = 0;
     if (millis() - lastCancelMs < 1500) return;
     lastCancelMs = millis();
     ```

---

### Vấn đề 7: Cảm giác máy bị đơ lúc khởi động & Biểu tượng Wi-Fi không tự cập nhật

#### A. Hiện trạng người dùng gặp phải:
Khi vừa cấp nguồn bật máy:
1. Có cảm giác màn hình bị đơ, bấm nút hay vặn núm xoay chưa nhận ngay. Chỉ sau khi bấm nút PUSH vào menu rồi quay ra Home thì mọi thứ mới bắt đầu bình thường.
2. Dù loa đã phát câu *🔊 "Đã kết nối Wi-Fi thành công"*, biểu tượng Wi-Fi trên màn hình vẫn trơ trơ chữ `WiFi: DANG NOI` (hoặc `WiFi: OFF`). Chỉ khi bấm PUSH vào `S_MENU` rồi bấm thoát về `S_HOME` thì biểu tượng mới chịu đổi thành `WiFi: OK`!

#### B. Phân tích nguyên nhân kỹ thuật (3 nguyên nhân cốt lõi):

1. **Biểu tượng Wi-Fi không tự vẽ lại khi có kết nối mới (Mất cờ `homeDirty`):**
   Trong [mqtt_manager.cpp:L82-L93](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/mqtt_manager.cpp#L82-L93):
   ```cpp
   if (WiFi.status() == WL_CONNECTED) {
       if (wifiState != WIFI_ST_CONNECTED) {
           wifiState = WIFI_ST_CONNECTED;
           audioPlayWifiConnected(); // Đã phát âm thanh thông báo
           configTime(...);
           // ❌ THIẾU HOÀN TOÀN LỆNH CẬP NHẬT MÀN HÌNH!
           // Không hề set homeDirty = true; và không hề gọi drawHomeHeader();
       }
   ```
   Biến `wifiState` đã được gán bằng `WIFI_ST_CONNECTED`, nhưng code **không hề báo cho màn hình biết để vẽ lại**. Do đó, màn hình vẫn giữ nguyên khung hình vẽ lúc `setup()` (lúc chưa có Wi-Fi). Khi người dùng bấm PUSH vào Menu rồi thoát ra Home, hàm `drawHome(true)` được gọi lại từ đầu $\rightarrow$ lúc này mới đọc biến `wifiState` mới và hiển thị `WiFi: OK`!

2. **Ảo giác "bị đơ" do màn hình tĩnh 100% trong 60 giây đầu:**
   Trên màn hình chính `S_HOME`, đồng hồ chỉ hiển thị `HH:MM`. Trong [main.cpp:L643](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/main.cpp#L643), hàm `tickSchedule()` chỉ set `homeDirty = true` **mỗi 60 giây một lần** khi bước sang phút mới.
   Từ giây thứ 1 đến giây thứ 59 sau khi bật nguồn:
   - Đồng hồ không nhảy giây.
   - Biểu tượng Wi-Fi không tự đổi màu.
   - Nhiệt độ DHT11 chưa đổi $\rightarrow$ không vẽ lại.
   Toàn bộ màn hình **bất động 100% không đổi một điểm ảnh nào**, tạo cảm giác thiết bị đã bị treo (treo màn hình). Khi người dùng bấm PUSH, hàm `goTo(S_MENU)` xóa toàn màn hình và vẽ menu mới, tạo ảo giác "bấm PUSH mới hết đơ".

3. **Hiện tượng nghẽn mạng TCP (Blocking Socket Connect) khi kết nối Broker:**
   Trong [mqtt_manager.cpp:L99-L105](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/mqtt_manager.cpp#L99-L105), ngay khi Wi-Fi vừa kết nối, hàm `reconnectMqtt()` lập tức được gọi:
   ```cpp
   if (!mqttClient.connected()) {
       if (now - lastReconnectAttempt > 5000) {
           lastReconnectAttempt = now;
           if (reconnectMqtt()) { ... }
       }
   }
   ```
   Nếu lúc đó máy tính chạy Broker Mosquitto chưa bật hoặc mạng LAN bị trễ, hàm `WiFiClient::connect()` trên ESP32 sẽ bị **chặn luồng (blocking) từ 1 đến 3 giây** để chờ gói tin SYN-ACK từ Broker. Trong suốt khoảng thời gian chờ socket này, vòng lặp `handleInput()` không thể thăm dò núm xoay EC11 và nút bấm $\rightarrow$ gây ra độ trễ thực sự khi nhấn nút lúc mới khởi động!

4. **Quá trình Homing trong `setup()`:**
   Trong `setup()`, hàm `autoHoming()` rà tìm vạch đen TCRT5000 có thể mất từ 1 đến 5 giây nếu khay đang nằm xa mốc Ngăn 0. Trong lúc này, vòng lặp đọc nút bấm chưa khởi chạy.

#### C. Giải pháp kỹ thuật triệt để:
1. **Cập nhật Wi-Fi tức thì:** Trong [mqtt_manager.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/mqtt_manager.cpp), ngay tại thời điểm `wifiState` chuyển sang `WIFI_ST_CONNECTED` (hoặc mất mạng):
   ```cpp
   wifiState = WIFI_ST_CONNECTED;
   homeDirty = true;
   if (screen == S_HOME) {
       drawHomeHeader(); // Vẽ lại ngay thanh trạng thái Header mà không cần tải lại toàn trang
   }
   ```
2. **Chống nghẽn MQTT Connect:** Đặt timeout kết nối ngắn và chỉ thử kết nối Broker khi IP đã ổn định, tránh treo CPU quá 500ms.

---

## 3. BẢNG MA TRẬN SO SÁNH TRƯỚC & SAU TỐI ƯU

| Tiêu chí | Trước khi tối ưu (Hiện tại) | Sau khi áp dụng đề xuất tối ưu |
| :--- | :--- | :--- |
| **Màn hình khi đến giờ** | Màn hình kẹt ở `S_HOME` hoặc menu con; không có đếm ngược trực quan. | Tự động chuyển thẳng sang `S_DUE_ALERT`, hiển thị đếm ngược 15 phút rực rỡ và danh sách thuốc. |
| **Thông tin từng ngăn khi mở** | Code có sẵn nhưng bị ẩn do không kích hoạt màn hình `S_DUE_ALERT`. | Ngăn nào mở là màn hình hiển thị ngay Tên thuốc, Liều dùng, Số ngăn ở cửa 6h và Ngăn tiếp theo. |
| **Thao tác nút khi chuông reo** | Nếu đang ở `S_MENU`, bấm nút bị hiểu lầm thành điều hướng cài đặt. | Luôn ưu tiên xác nhận uống thuốc khi ở `S_DUE_ALERT`. |
| **Trạng thái Nạp thuốc** | Nạp thuốc nhưng FSM vẫn báo `SAFE_HOME_IDLE`, dễ bị lịch cữ thuốc quay đè. | Có trạng thái `FSM_REFILL_WAITING`, chặn lệnh xoay đè và có timeout 5 phút tự đóng an toàn. |
| **Âm thanh khi bấm Hủy nạp** | Bị phát lặp 2 lần liên tiếp câu *"Đã hủy nạp thuốc..."*. | Chỉ phát đúng 1 lần dứt khoát nhờ tách topic và bộ lọc chống dội 1.5s. |
| **Cập nhật biểu tượng Wi-Fi** | Loa báo kết nối nhưng màn hình vẫn trơ trơ `DANG NOI`, phải bấm PUSH mới đổi. | Màn hình tự động chuyển sang `WiFi: OK` màu xanh ngay lập tức khi vừa bắt được sóng. |
| **Độ nhạy lúc mới bật nguồn** | Màn hình tĩnh 100% gây cảm giác đơ, socket connect có thể giật lag nút bấm. | Header cập nhật tức thì, kết nối mạng non-blocking, nút bấm nhạy ngay từ giây đầu tiên. |
| **Chống kẹt cơ khi nhận lệnh Web** | Nhận `cmd/dispense` khi đang kẹt cơ vẫn cố quay, nguy cơ hỏng motor. | Có Guard Clause chặn tuyệt đối lệnh xoay khi trạng thái không phải `SAFE_HOME_IDLE`. |
| **Hành trình cữ thuốc nhiều ngăn** | Quay về 0 sau mỗi ngăn: $1 \rightarrow 0 \rightarrow 2$ (tốn 1536 bước). | Quay trực tiếp giữa các ngăn: $1 \rightarrow 2 \rightarrow 0$ (tốn 1024 bước, giảm 33% thời gian). |
| **Đồng bộ Backend khi đến giờ** | Backend Scheduler ném lỗi 409 Conflict vì thấy ESP32 chuyển `ALERTING_DUE`. | Hai bên đồng bộ nhịp nhàng thông qua event `events/due_alert`. |

---

## 4. LỘ TRÌNH CODE THỰC CHIẾN CHI TIẾT TỪNG FILE

### Bước 1: Cập nhật Máy trạng thái FSM ([fsm_controller.h](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/include/fsm_controller.h) & [fsm_controller.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/fsm_controller.cpp))
1. Thêm `FSM_REFILL_WAITING = 7` vào enum `FsmState`.
2. Thêm chuỗi `"REFILL_WAITING"` vào `FSM_STATE_STRINGS`.
3. Bổ sung `goTo(S_DUE_ALERT)` trong `fsmTriggerDueAlert()`.
4. Bổ sung Guard Clause trong `fsmTriggerManualDispense()` và `fsmTriggerRefillStart()`.
5. Tối ưu thuật toán chuyển tiếp đa ngăn trong `case FSM_ROTATING_TO_HOME`: nếu `nextSlot > 0`, chuyển ngay sang `ROTATING_TO_DOSE` đến `nextSlot` mà không hồi vị 0 giữa chừng.
6. Thêm xử lý timeout 300 giây cho `FSM_REFILL_WAITING` trong `fsmUpdate()`.

### Bước 2: Cập nhật Luồng giao diện trong [main.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/main.cpp)
1. Đảm bảo khi FSM về `FSM_SAFE_HOME_IDLE` sau khi hoàn tất cữ thuốc, màn hình tự động quay về `goTo(S_HOME)`.
2. Trong `handleInput()`, củng cố `case S_DUE_ALERT` để nhận cả hai phím `PUSH` và `K0` làm nút xác nhận uống thuốc an toàn cho người cao tuổi.

### Bước 3: Cập nhật Đồng bộ MQTT & Mạng trong [mqtt_manager.cpp](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/firmware/src/mqtt_manager.cpp)
1. Bổ sung publish trạng thái `REFILL_WAITING` khi thiết bị đang ở chế độ nạp thuốc.
2. Kiểm tra `fsmGetState() == FSM_SAFE_HOME_IDLE` trước khi thực thi `cmd/dispense` và phản hồi lỗi MQTT nếu thiết bị đang bận.
3. Tách riêng nhánh `cmd/refill_cancel` và `cmd/home` với bộ lọc debounce 1.5s để triệt tiêu lỗi phát lặp 2 lần âm thanh.
4. Gọi `drawHomeHeader()` và set `homeDirty = true` ngay khi `wifiState` chuyển sang `WIFI_ST_CONNECTED` để biểu tượng Wi-Fi đổi màu xanh `WiFi: OK` tức thì.

### Bước 4: Tinh chỉnh Backend ([dispensing_engine.py](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/backend/services/dispensing_engine.py) & [backend/api/device.py](file:///c:/Users/User/OneDrive%20-%20The%20University%20of%20Technology/Desktop/new/backend/api/device.py))
1. Thêm callback xử lý sự kiện `DUE_ALERT` từ ESP32 để tự động tạo session mà không xung đột cờ trạng thái 409.
2. Trong `backend/api/device.py`, hàm `cancel_refill()` xóa bỏ lệnh publish thừa `cmd/home`, chỉ phát duy nhất lệnh `cmd/refill_cancel`.
