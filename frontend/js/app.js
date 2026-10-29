/**
 * app.js — SmartMed SPA Main Orchestrator
 * Coordinates Tabs, Real-time Dashboard, 3D Carousel, Refill Flow,
 * 4-Session Schedules (24h format), Hardware Diagnostics, and History.
 */

// Global Application State
const appState = {
  activeTab: 'tab-dashboard',
  device: { is_online: false, fsm_state: 'SAFE_HOME_IDLE', current_slot: 0, wifi_rssi: -58 },
  slots: [],
  schedules: [],
  recentLogs: [],
  activeSession: null,
  countdownInterval: null,
  remainingSeconds: 900, // 15:00
  carousel2D: null,
  carousel3D: null,
  consoleMode: 'IDLE', // 'IDLE' | 'REFILL' | 'EARLY_DISPENSE'
  selectedRefillSlot: 1,
};

// --- Initialization ---
document.addEventListener('DOMContentLoaded', async () => {
  console.log('[SmartMed] Khởi động ứng dụng...');

  // 1. Initialize 2D Interactive Dispenser (Top-down view, 6 o'clock fixed aperture)
  try {
    appState.carousel2D = new SmartMedCarousel2D('dispenser-2d-box');
    appState.carousel3D = appState.carousel2D; // Alias for seamless compatibility
  } catch (e) {
    console.error('[SmartMed] Lỗi khởi tạo khay 2D:', e);
  }

  // 2. Setup SPA Navigation
  try {
    setupNavigation();
  } catch (e) {
    console.error('[SmartMed] Lỗi setupNavigation:', e);
  }

  // 3. Immediately render initial UI (so sections next to 2D model appear instantly!)
  try {
    renderStatsCards();
    renderConsolePanel();
    renderSlotsGrid();
  } catch (e) {
    console.error('[SmartMed] Lỗi render ban đầu:', e);
  }

  // 4. Setup Modals & Event Listeners
  try {
    setupModals();
  } catch (e) {
    console.error('[SmartMed] Lỗi setupModals:', e);
  }

  // 5. Setup WebSocket Listeners & Connect
  try {
    setupWebSocket();
    if (window.wsClient && typeof window.wsClient.connect === 'function') {
      window.wsClient.connect();
    }
  } catch (e) {
    console.error('[SmartMed] Lỗi WebSocket:', e);
  }

  // 6. Initial Data Load from backend
  try {
    await loadDashboardData();
  } catch (e) {
    console.error('[SmartMed] Lỗi loadDashboardData:', e);
  }

  // 7. Start Clock
  try {
    startClock();
  } catch (e) {
    console.error('[SmartMed] Lỗi startClock:', e);
  }

  // 8. Fallback periodic polling every 10s
  setInterval(loadDashboardData, 10000);
});

function setupModals() {
  setupHardwareView();
  setupHistoryListeners();
}

// --- SPA Navigation ---
function setupNavigation() {
  const tabBtns = document.querySelectorAll('.tab-btn');
  tabBtns.forEach((btn) => {
    btn.addEventListener('click', () => {
      const targetId = btn.getAttribute('data-tab');
      switchTab(targetId);
    });
  });
}

function switchTab(tabId) {
  appState.activeTab = tabId;

  // Update tabs buttons
  document.querySelectorAll('.tab-btn').forEach((btn) => {
    btn.classList.toggle('active', btn.getAttribute('data-tab') === tabId);
  });

  // Update tab panes
  document.querySelectorAll('.tab-pane').forEach((pane) => {
    pane.classList.toggle('active', pane.id === tabId);
  });

  // Trigger re-render and 3D resize if returning to dashboard
  if (tabId === 'tab-dashboard') {
    renderStatsCards();
    renderConsolePanel();
    renderSlotsGrid();
    if (appState.carousel3D) {
      setTimeout(() => appState.carousel3D.onResize(), 50);
    }
  }

  // Refresh data for the specific view
  if (tabId === 'tab-schedules') {
    renderSchedulesView();
  } else if (tabId === 'tab-history') {
    loadHistoryData();
  }
}

// --- Data Fetching ---
async function loadDashboardData() {
  try {
    const data = await window.api.getDashboard();
    appState.device = data.device || appState.device;
    appState.slots = data.slots || [];
    appState.schedules = data.schedules || [];
    appState.recentLogs = data.recent_logs || [];
    appState.activeSession = data.active_session;

    // Update Header Status & Telemetry
    updateHeaderStatus();
    updateTelemetryView(appState.device);

    // Update 3D/2D Model Pills & Position
    if (appState.carousel3D) {
      appState.carousel3D.updateSlotsData(appState.slots);

      // QUAN TRỌNG: Nếu đang ở chế độ REFILL, TUYỆT ĐỐI KHÔNG để background polling
      // tự ý xoay khay về 0! Giữ nguyên ngăn đang nạp (selectedRefillSlot).
      if (appState.consoleMode === 'REFILL') {
        const refillSlot = appState.selectedRefillSlot || 1;
        appState.carousel3D.rotateToSlot(refillSlot);
      } else if (appState.consoleMode === 'EARLY_DISPENSE') {
        // Giữ nguyên trạng thái early dispense
      } else if (appState.device.current_slot !== undefined) {
        appState.carousel3D.rotateToSlot(appState.device.current_slot);
      }
    }

    // Render Dashboard UI
    renderStatsCards();
    // QUAN TRỌNG: Nếu đang ở chế độ REFILL hoặc EARLY_DISPENSE, TUYỆT ĐỐI KHÔNG
    // tự ý re-render lại consolePanel để tránh xóa trắng form nhập liệu của người dùng!
    if (appState.consoleMode !== 'REFILL' && appState.consoleMode !== 'EARLY_DISPENSE') {
      renderConsolePanel();
    }
    renderSlotsGrid();

    // If schedules view is active, update it
    if (appState.activeTab === 'tab-schedules') {
      renderSchedulesView();
    }
  } catch (err) {
    console.warn('[SmartMed] Không thể tải dữ liệu dashboard:', err);
  }
}

// --- Header Status ---
function updateHeaderStatus() {
  const statusChip = document.getElementById('hw-status-chip');
  const statusText = document.getElementById('hw-status-text');
  const fsmBadge = document.getElementById('header-fsm-badge');
  const wifiBadge = document.getElementById('header-wifi-badge');
  const envBadge = document.getElementById('header-env-badge');

  if (!statusChip) return;

  const isOnline = appState.device.is_online;
  statusChip.className = `status-chip ${isOnline ? 'online' : 'offline'}`;
  statusText.textContent = isOnline ? 'Trực tuyến' : 'Ngoại tuyến';

  if (fsmBadge) {
    fsmBadge.textContent = appState.device.fsm_state || 'IDLE';
  }

  if (wifiBadge) {
    const rssi = appState.device.wifi_rssi !== undefined ? appState.device.wifi_rssi : appState.device.rssi;
    wifiBadge.textContent = rssi !== undefined && rssi !== null ? `📶 ${rssi} dBm` : '📶 -- dBm';
  }

  if (envBadge) {
    const temp = appState.device.temperature !== undefined && appState.device.temperature !== null ? `${Math.round(appState.device.temperature)}°C` : '--°C';
    const humid = appState.device.humidity !== undefined && appState.device.humidity !== null ? `${Math.round(appState.device.humidity)}%` : '--%';
    envBadge.textContent = `🌡️ ${temp} | 💧 ${humid}`;
  }

  // Khóa nút đặt lịch nếu thiết bị ngoại tuyến (Offline)
  const addSchedBtn = document.getElementById('btn-open-add-schedule');
  if (addSchedBtn) {
    if (!isOnline) {
      addSchedBtn.disabled = true;
      addSchedBtn.style.opacity = '0.5';
      addSchedBtn.style.cursor = 'not-allowed';
      addSchedBtn.title = 'Hộp thuốc đang ngoại tuyến (Offline) - Không thể đặt lịch';
    } else {
      addSchedBtn.disabled = false;
      addSchedBtn.style.opacity = '1';
      addSchedBtn.style.cursor = 'pointer';
      addSchedBtn.title = 'Thêm cữ uống mới';
    }
  }
}

// --- Telemetry Diagnostics Details ---
function updateTelemetryView(data) {
  if (!data) return;
  const telemFsm = document.getElementById('telem-fsm');
  const telemAngle = document.getElementById('telem-angle');
  const telemRssi = document.getElementById('telem-rssi');
  const telemHeap = document.getElementById('telem-heap');
  const telemUptime = document.getElementById('telem-uptime');
  const telemTemp = document.getElementById('telem-temp');
  const telemHumid = document.getElementById('telem-humid');

  if (telemFsm && data.fsm_state) {
    telemFsm.textContent = data.fsm_state;
  }
  if (telemAngle && data.current_slot !== undefined && data.current_slot !== null) {
    const angle = (Number(data.current_slot) * 45).toFixed(1);
    telemAngle.textContent = `${angle}° (Slot ${data.current_slot})`;
  }
  const rssi = data.rssi !== undefined ? data.rssi : data.wifi_rssi;
  if (telemRssi && rssi !== undefined && rssi !== null) {
    let qual = rssi >= -60 ? 'Tốt' : rssi >= -75 ? 'Trung bình' : 'Yếu';
    telemRssi.textContent = `${rssi} dBm (${qual})`;
  }
  if (telemHeap && data.free_heap !== undefined && data.free_heap !== null) {
    const kb = (Number(data.free_heap) / 1024).toFixed(1);
    telemHeap.textContent = `${kb} KB`;
  }
  if (telemUptime && data.uptime !== undefined && data.uptime !== null) {
    const uptimeSec = Number(data.uptime);
    const hours = Math.floor(uptimeSec / 3600);
    const mins = Math.floor((uptimeSec % 3600) / 60);
    const secs = uptimeSec % 60;
    telemUptime.textContent = `${hours}h ${mins}m ${secs}s`;
  }
  if (telemTemp) {
    telemTemp.textContent = data.temperature !== undefined && data.temperature !== null ? `${Math.round(data.temperature)} °C` : '-- °C';
  }
  if (telemHumid) {
    telemHumid.textContent = data.humidity !== undefined && data.humidity !== null ? `${Math.round(data.humidity)} %` : '-- %';
  }
}

// --- Tab 1: Dashboard Stat Cards ---
function renderStatsCards() {
  // 1. Next Dose
  const nextDoseVal = document.getElementById('stat-next-dose-val');
  const nextDoseSub = document.getElementById('stat-next-dose-sub');
  const upcoming = getNextUpcomingSchedule();

  if (nextDoseVal) {
    if (upcoming) {
      nextDoseVal.textContent = upcoming.time_of_day || upcoming.time;
      const medName = upcoming.medicine_name || `Ngăn ${upcoming.slot_number}`;
      nextDoseSub.textContent = `${medName} (${upcoming.dose_quantity || upcoming.pill_count || 1} viên)`;
    } else {
      nextDoseVal.textContent = '--:--';
      nextDoseSub.textContent = 'Không có cữ thuốc';
    }
  }

  // 2. Compliance Rate
  const compVal = document.getElementById('stat-compliance-val');
  if (compVal) {
    const todayLogs = appState.recentLogs.filter((l) => isToday(l.dispensed_at || l.scheduled_time));
    const takenCount = todayLogs.filter((l) => l.status === 'TAKEN' || l.status === 'EARLY_TAKEN').length;
    const rate = todayLogs.length > 0 ? Math.round((takenCount / todayLogs.length) * 100) : 100;
    compVal.textContent = `${rate}%`;
  }

  // 3. Low Stock Count (<= 2 pills)
  const lowStockVal = document.getElementById('stat-low-stock-val');
  if (lowStockVal) {
    const lowCount = appState.slots.filter((s) => s.slot_number > 0 && s.current_quantity <= 2).length;
    lowStockVal.textContent = lowCount > 0 ? `${lowCount} ngăn` : 'Đầy đủ';
    lowStockVal.style.color = lowCount > 0 ? 'var(--warning-amber)' : 'var(--safe-green)';
  }

  // 4. Current Tray Position
  const posVal = document.getElementById('stat-tray-pos-val');
  if (posVal) {
    const cur = appState.device.current_slot || 0;
    posVal.textContent = cur === 0 ? 'Ngăn 0 (Home)' : `Ngăn ${cur}`;
  }
}

function getNextUpcomingSchedule() {
  const activeSchedules = appState.schedules.filter((s) => s.is_active);
  if (activeSchedules.length === 0) return null;

  const now = new Date();
  const currentHM = now.toTimeString().substring(0, 5); // "HH:MM"

  // Sort by time
  const sorted = [...activeSchedules].sort((a, b) => {
    const timeA = a.time_of_day || a.time || '';
    const timeB = b.time_of_day || b.time || '';
    return timeA.localeCompare(timeB);
  });
  const upcoming = sorted.find((s) => (s.time_of_day || s.time) >= currentHM);
  return upcoming || sorted[0];
}

function isToday(dateStr) {
  if (!dateStr) return false;
  const d = new Date(dateStr);
  const today = new Date();
  return d.getDate() === today.getDate() && d.getMonth() === today.getMonth() && d.getFullYear() === today.getFullYear();
}

// --- Tab 1: Quick Upcoming Doses Widget (Xem thông tin nhanh những đơn thuốc tiếp theo trong ngăn nào) ---
function renderUpcomingDosesList() {
  const activeSchedules = appState.schedules.filter((s) => s.is_active);
  if (activeSchedules.length === 0) {
    return `
      <div class="upcoming-doses-wrapper">
        <div class="upcoming-doses-header">
          <div style="display:flex; align-items:center; gap:0.4rem;">
            <span style="font-size:1rem;">⏰</span>
            <span style="font-size:0.85rem; font-weight:800; color:var(--text-main);">ĐƠN THUỐC TIẾP THEO</span>
          </div>
          <span style="font-size:0.75rem; color:var(--text-muted); font-weight:600;">0 cữ</span>
        </div>
        <div class="upcoming-empty-card">
          <span style="font-size:1.25rem;">ℹ️</span>
          <div>
            <div style="font-weight:700; color:var(--text-main); font-size:0.82rem;">Chưa có lịch uống nào</div>
            <div style="font-size:0.75rem; color:var(--text-muted); margin-top:2px;">Bấm tab <em>Lịch uống 4 Buổi</em> để tạo cữ uống mới.</div>
          </div>
        </div>
      </div>
    `;
  }

  const now = new Date();
  const currentHM = now.toTimeString().substring(0, 5); // "HH:MM"

  // Sort by time
  const sorted = [...activeSchedules].sort((a, b) => {
    const timeA = a.time_of_day || a.time || '';
    const timeB = b.time_of_day || b.time || '';
    return timeA.localeCompare(timeB);
  });

  const upcomingToday = sorted.filter((s) => (s.time_of_day || s.time) >= currentHM);
  const passedEarlier = sorted.filter((s) => (s.time_of_day || s.time) < currentHM);

  // List upcoming today first, then cycle to earliest of tomorrow
  const displayList = upcomingToday.length > 0 ? [...upcomingToday, ...passedEarlier].slice(0, 3) : sorted.slice(0, 3);
  const isTomorrow = upcomingToday.length === 0;

  return `
    <div class="upcoming-doses-wrapper">
      <div class="upcoming-doses-header">
        <div style="display:flex; align-items:center; gap:0.4rem;">
          <span style="font-size:1rem;">⏰</span>
          <span style="font-size:0.85rem; font-weight:800; color:var(--text-main);">ĐƠN THUỐC TIẾP THEO</span>
        </div>
        <span style="font-size:0.75rem; color:${upcomingToday.length > 0 ? 'var(--primary)' : 'var(--text-muted)'}; font-weight:700;">
          ${upcomingToday.length > 0 ? `${upcomingToday.length} cữ còn lại hôm nay` : 'Cữ uống ngày mai'}
        </span>
      </div>

      <div class="upcoming-doses-list">
        ${displayList
      .map((sch, idx) => {
        const slot = appState.slots.find((s) => s.slot_number === sch.slot_number);
        const medName = sch.medicine_name || slot?.medicine?.name || `Thuốc Ngăn ${sch.slot_number}`;
        const timeVal = sch.time_of_day || sch.time || '00:00';
        const doseQty = sch.dose_quantity || sch.pill_count || 1;
        const isNext = idx === 0 && !isTomorrow;

        const colorCfg = window.SMARTMED_SLOT_COLORS ? window.SMARTMED_SLOT_COLORS[sch.slot_number] : null;
        const primaryColor = colorCfg?.primaryColor || '#2563eb';
        const colorName = colorCfg?.colorName?.split(' ')[0] || '';

        // Session Icon
        let sessionIcon = '☀️';
        const [h] = timeVal.split(':').map(Number);
        if (h >= 5 && h < 11) sessionIcon = '🌅';
        else if (h >= 11 && h < 14) sessionIcon = '☀️';
        else if (h >= 14 && h < 18) sessionIcon = '🌇';
        else sessionIcon = '🌙';

        return `
            <div class="upcoming-dose-item ${isNext ? 'is-next' : ''}">
              <div class="dose-time-col">
                <span class="dose-time tabular-nums">${timeVal}</span>
                <span class="dose-session-icon">${sessionIcon}</span>
              </div>

              <div class="dose-info-col">
                <div class="dose-med-title" title="${medName}">${medName}</div>
                <div class="dose-meta">
                  <span class="dose-slot-pill" style="background:${primaryColor}18; color:${primaryColor}; border:1px solid ${primaryColor}40;">
                    <span style="display:inline-block; width:7px; height:7px; border-radius:50%; background:${primaryColor}; margin-right:4px;"></span>
                    Ngăn ${sch.slot_number} (${colorName})
                  </span>
                  <span style="color:var(--text-muted); font-size:0.75rem;">• ${doseQty} viên / cữ</span>
                </div>
              </div>

              <button class="dose-rotate-btn" title="Xoay khay đến Ngăn ${sch.slot_number} để kiểm tra tại cửa 6h" onclick="appState.carousel2D && appState.carousel2D.rotateToSlot(${sch.slot_number})">
                🔄 Ngăn ${sch.slot_number}
              </button>
            </div>
          `;
      })
      .join('')}
      </div>
    </div>
  `;
}

// --- Tab 1: Inline Refill Panel (Thay thế modal làm mờ, xoay khay ngay lập tức) ---
function renderInlineRefillCard() {
  const currentSlot = appState.selectedRefillSlot || 1;
  const slotData = appState.slots.find((s) => s.slot_number === currentSlot);
  const cfg = window.SMARTMED_SLOT_COLORS ? window.SMARTMED_SLOT_COLORS[currentSlot] : null;
  const primaryColor = cfg?.primaryColor || '#2563eb';
  const colorName = cfg?.colorName ? cfg.colorName.split(' ')[0] : `Ngăn ${currentSlot}`;

  // Bảo toàn dữ liệu người dùng đang nhập dở (nếu có)
  const existingSlotSelect = document.getElementById('inline-refill-slot-select');
  const existingNameInput = document.getElementById('inline-refill-med-name');
  const existingQtyInput = document.getElementById('inline-refill-qty');
  const existingDoseInput = document.getElementById('inline-refill-dose');

  // Chỉ dùng giá trị đang gõ nếu ô input đó thuộc về chính ngăn hiện tại
  const isSameSlot = !existingSlotSelect || parseInt(existingSlotSelect.value, 10) === currentSlot;

  const medName = (isSameSlot && existingNameInput && existingNameInput.value !== undefined)
    ? existingNameInput.value
    : (slotData?.medicine?.name || '');
  const qty = (isSameSlot && existingQtyInput && existingQtyInput.value)
    ? existingQtyInput.value
    : (slotData?.current_quantity || 5);
  const dose = (isSameSlot && existingDoseInput && existingDoseInput.value)
    ? existingDoseInput.value
    : (slotData?.default_dose || 1);

  return `
    <div class="inline-task-card" id="inline-refill-panel">
      <div class="inline-task-header">
        <div class="inline-task-title">
          <span style="font-size:1.15rem;">📥</span>
          <span>NẠP / NHẬP THUỐC VÀO NGĂN</span>
        </div>
        <button type="button" class="inline-btn-close" onclick="cancelInlineTask()" title="Đóng & Về vị trí an toàn">✕</button>
      </div>

      <div class="inline-guidance-pill" style="border-left: 4px solid ${primaryColor};">
        <span>📍</span>
        <div>
          Mô hình đã xoay <strong>Ngăn ${currentSlot} (${colorName})</strong> ra cửa mở <strong>hướng 6 giờ</strong>.
        </div>
      </div>

      <form id="inline-refill-form" onsubmit="handleInlineRefillSubmit(event)">
        <!-- Slot Selector -->
        <div class="form-group" style="margin-bottom:0.75rem;">
          <label class="form-label" style="font-size:0.8rem; font-weight:700;">Chọn Ngăn nạp thuốc (Khay tự xoay ra 6h):</label>
          <select id="inline-refill-slot-select" class="form-select" onchange="handleInlineSlotChange(this.value)">
            ${[1, 2, 3, 4, 5, 6, 7]
      .map((s) => {
        const c = window.SMARTMED_SLOT_COLORS ? window.SMARTMED_SLOT_COLORS[s] : null;
        const sd = appState.slots.find((slot) => slot.slot_number === s);
        const med = sd?.medicine?.name ? `(${sd.medicine.name} - ${sd.current_quantity}v)` : '(Trống)';
        const cName = c?.colorName ? c.colorName.split(' ')[0] : '';
        return `<option value="${s}" ${s === currentSlot ? 'selected' : ''}>
                Ngăn ${s} - ${cName} ${med}
              </option>`;
      })
      .join('')}
          </select>
          <span class="form-hint" style="font-size:0.75rem; color:var(--text-muted); margin-top:3px; display:block;">
            💡 Đổi ngăn tại đây hoặc bấm trực tiếp lên vòng tròn 2D: Khay sẽ xoay ngay ngăn đó ra hướng 6h!
          </span>
        </div>

        <!-- Medicine Name -->
        <div class="form-group" style="margin-bottom:0.75rem;">
          <label class="form-label" style="font-size:0.8rem; font-weight:700;">Tên thuốc / Biệt dược:</label>
          <input type="text" id="inline-refill-med-name" class="form-input" value="${medName}" placeholder="Ví dụ: Panadol Extra, Vitamin C..." required>
        </div>

        <!-- Liều dùng mỗi lần uống (Dose per intake) -->
        <div class="form-group" style="margin-bottom:0.75rem;">
          <div style="display:flex; justify-content:space-between; align-items:center;">
            <label class="form-label" style="font-size:0.8rem; font-weight:700;">Liều dùng mỗi lần uống:</label>
            <span style="font-size:0.8rem; color:#2563eb; font-weight:700;" id="inline-refill-dose-badge">${dose} viên / cữ</span>
          </div>
          <div style="display:flex; gap:0.5rem; align-items:center;">
            <input type="number" id="inline-refill-dose" class="form-input tabular-nums" min="1" max="5" value="${dose}" required oninput="const b=document.getElementById('inline-refill-dose-badge'); if(b) b.textContent=this.value+' viên / cữ';">
            <div class="inline-quick-amount" style="margin-top:0;">
              <button type="button" class="inline-quick-btn" onclick="setInlineDose(1)">1 viên</button>
              <button type="button" class="inline-quick-btn" onclick="setInlineDose(2)">2 viên</button>
              <button type="button" class="inline-quick-btn" onclick="setInlineDose(3)">3 viên</button>
            </div>
          </div>
          <span class="form-hint" style="font-size:0.75rem; color:var(--text-muted); margin-top:3px; display:block;">
            💊 Số viên bệnh nhân cần uống mỗi lần. Liều này sẽ đồng bộ trực tiếp lên màn hình LCD và bộ nhớ NVS của hộp thuốc.
          </span>
        </div>

        <!-- Quantity (Số lượng viên nạp tồn kho) -->
        <div class="form-group" style="margin-bottom:1rem;">
          <label class="form-label" style="font-size:0.8rem; font-weight:700;">Số lượng viên nạp (Tồn kho khay):</label>
          <input type="number" id="inline-refill-qty" class="form-input tabular-nums" min="0" value="${qty}" required>
          <div class="inline-quick-amount">
            <button type="button" class="inline-quick-btn" onclick="setInlineQuickAmount(5)">+5 viên</button>
            <button type="button" class="inline-quick-btn" onclick="setInlineQuickAmount(10)">+10 viên</button>
            <button type="button" class="inline-quick-btn" onclick="setInlineQuickAmount(20)">+20 viên</button>
          </div>
        </div>

        <!-- Confirmation Button with Safe Return -->
        <div style="display:flex; flex-direction:column; gap:0.5rem;">
          <button type="submit" class="btn-confirm-safe" id="btn-confirm-refill-safe">
            ✅ Xác nhận đã bỏ thuốc vào & Về vị trí Safe 🔒
          </button>
          <button type="button" class="btn btn-secondary btn-sm" onclick="cancelInlineTask()" style="width:100%;">
            ✕ Hủy bỏ & Về Safe Home
          </button>
        </div>
      </form>
    </div>
  `;
}

// --- Tab 1: Inline Early Dispense Panel ---
function renderInlineEarlyDispenseCard() {
  const availableSlots = appState.slots.filter(
    (s) => s.slot_number >= 1 && s.slot_number <= 7 && s.current_quantity > 0 && s.medicine
  );

  return `
    <div class="inline-task-card" id="inline-early-panel">
      <div class="inline-task-header">
        <div class="inline-task-title">
          <span style="font-size:1.15rem;">⚡</span>
          <span>LẤY THUỐC SỚM THEO CỬ</span>
        </div>
        <button type="button" class="inline-btn-close" onclick="cancelInlineTask()" title="Đóng">✕</button>
      </div>

      <div class="inline-guidance-pill">
        <span>ℹ️</span>
        <div>Chọn ngăn thuốc cần lấy trước giờ hẹn (ví dụ khi ra ngoài). Hộp sẽ xoay từng ngăn ra cửa 6h.</div>
      </div>

      ${availableSlots.length === 0
      ? `<p style="font-size:0.85rem; color:var(--text-muted); padding:0.5rem 0;">Không có ngăn nào có thuốc để lấy sớm.</p>
             <button type="button" class="btn btn-secondary btn-sm" onclick="cancelInlineTask()" style="width:100%; margin-top:0.5rem;">Quay lại</button>`
      : `
          <form id="inline-early-form" onsubmit="handleInlineEarlyDispenseSubmit(event)">
            <div style="display:flex; flex-direction:column; gap:0.4rem; margin-bottom:1rem; max-height:220px; overflow-y:auto;">
              ${availableSlots
        .map((s) => {
          const cfg = window.SMARTMED_SLOT_COLORS ? window.SMARTMED_SLOT_COLORS[s.slot_number] : null;
          const primaryColor = cfg?.primaryColor || '#2563eb';
          const colorName = cfg?.colorName ? cfg.colorName.split(' ')[0] : '';
          return `
                  <label style="display:flex; align-items:center; gap:0.6rem; padding:0.55rem 0.7rem; border:1px solid var(--border-color); border-radius:var(--radius-md); background:#f8fafc; cursor:pointer;">
                    <input type="checkbox" name="inline-early-slot" value="${s.slot_number}" checked style="width:16px; height:16px;">
                    <div style="flex:1;">
                      <div style="font-weight:700; font-size:0.85rem; color:var(--text-main); display:flex; align-items:center; gap:6px;">
                        <span style="display:inline-block; width:8px; height:8px; border-radius:50%; background:${primaryColor};"></span>
                        Ngăn ${s.slot_number} (${colorName}): ${s.medicine.name}
                      </div>
                      <div style="font-size:0.75rem; color:var(--text-muted); margin-top:2px;">
                        Tồn kho: <strong>${s.current_quantity}</strong> viên • Liều: ${s.default_dose || 1} viên/lần
                      </div>
                    </div>
                  </label>
                `;
        })
        .join('')}
            </div>

            <div style="display:flex; flex-direction:column; gap:0.5rem;">
              <button type="submit" class="btn btn-primary" style="width:100%; font-weight:700;">
                🚀 Bắt đầu lấy thuốc sớm
              </button>
              <button type="button" class="btn btn-secondary btn-sm" onclick="cancelInlineTask()" style="width:100%;">
                ✕ Hủy bỏ
              </button>
            </div>
          </form>
        `
    }
    </div>
  `;
}

// --- Tab 1: Right Console Panel ---
function renderConsolePanel() {
  const consoleArea = document.getElementById('dashboard-console-area');
  if (!consoleArea) return;

  // Bảo toàn focus và vị trí con trỏ chuột nếu người dùng đang nhập liệu
  const activeElId = document.activeElement ? document.activeElement.id : null;
  let selStart = 0, selEnd = 0;
  if (document.activeElement && (document.activeElement.tagName === 'INPUT' || document.activeElement.tagName === 'TEXTAREA')) {
    try {
      selStart = document.activeElement.selectionStart || 0;
      selEnd = document.activeElement.selectionEnd || 0;
    } catch (_) { }
  }

  const active = appState.activeSession;
  const isDispenseActive = (active && active.is_active) || appState.device.fsm_state === 'WAITING_CONFIRM' || appState.device.fsm_state === 'WAITING_TAKEN';
  const isDueAlerting = appState.device.fsm_state === 'ALERTING_DUE';

  if (isDueAlerting) {
    // Render Cảnh báo cữ thuốc đến giờ (ALERTING_DUE)
    consoleArea.innerHTML = `
      <div class="active-session-card" style="border: 2px solid #f59e0b; background: linear-gradient(135deg, rgba(245, 158, 11, 0.12), rgba(16, 185, 129, 0.05));">
        <div style="display:flex; justify-content:space-between; width:100%; align-items:center;">
          <span style="font-size:0.85rem; font-weight:800; color:#f59e0b; letter-spacing:0.05em; display:flex; align-items:center; gap:0.4rem;">
            <span>🔔</span> ĐÃ ĐẾN CỮ UỐNG THUỐC
          </span>
          <span class="status-chip" style="background:rgba(245, 158, 11, 0.2); color:#f59e0b; border:1px solid #f59e0b; font-weight:700;">
            15 Phút Chờ
          </span>
        </div>

        <div class="countdown-svg-wrapper">
          <svg class="countdown-svg" viewBox="0 0 160 160">
            <circle class="countdown-bg-circle" cx="80" cy="80" r="70"></circle>
            <circle id="countdown-progress-ring" class="countdown-progress-circle" cx="80" cy="80" r="70"
                    stroke-dasharray="440" stroke-dashoffset="0" style="stroke:#f59e0b;"></circle>
          </svg>
          <div class="countdown-text-overlay">
            <span id="countdown-digits-text" class="countdown-digits tabular-nums" style="color:#f59e0b;">15:00</span>
            <span class="countdown-subtext">Chuông báo thức cữ hẹn</span>
          </div>
        </div>

        <div class="session-info">
          <div class="session-pill-name">Hộp thuốc đang báo chuông nhắc nhở</div>
          <div class="session-pill-meta">Vui lòng bấm nút trên hộp hoặc click nút bên dưới để mở khay lấy thuốc</div>
        </div>

        <div style="display:flex; flex-direction:column; gap:0.6rem; width:100%;">
          <button id="btn-confirm-dose" class="btn btn-warning btn-lg" style="width:100%; font-weight:800; background:#f59e0b; color:#000; border:none; padding:0.9rem;" onclick="handleConfirmDose()">
            🔔 Xác nhận đến uống thuốc (Mở khay)
          </button>
        </div>
      </div>
    `;
    startActiveCountdown(null);
  } else if (isDispenseActive) {
    // Render Active Dispensing Session / Đang mở khay chờ lấy thuốc
    const slotNum = (active && active.slot_number) ? active.slot_number : (appState.device.current_slot || 1);
    const medName = (active && active.medicine_name) ? active.medicine_name : `Thuốc Ngăn ${slotNum}`;
    const pillCount = (active && active.pill_count) ? active.pill_count : 1;

    consoleArea.innerHTML = `
      <div class="active-session-card">
        <div style="display:flex; justify-content:space-between; width:100%; align-items:center;">
          <span style="font-size:0.85rem; font-weight:700; color:#10b981; letter-spacing:0.05em;">ĐANG MỞ KHAY LẤY THUỐC</span>
          <span class="status-chip" style="background:rgba(255,255,255,0.1); color:#fff; border:none;">
            Ngăn ${slotNum}
          </span>
        </div>

        <div class="countdown-svg-wrapper">
          <svg class="countdown-svg" viewBox="0 0 160 160">
            <circle class="countdown-bg-circle" cx="80" cy="80" r="70"></circle>
            <circle id="countdown-progress-ring" class="countdown-progress-circle" cx="80" cy="80" r="70"
                    stroke-dasharray="440" stroke-dashoffset="0"></circle>
          </svg>
          <div class="countdown-text-overlay">
            <span id="countdown-digits-text" class="countdown-digits tabular-nums">05:00</span>
            <span class="countdown-subtext">Đếm ngược lấy thuốc</span>
          </div>
        </div>

        <div class="session-info">
          <div class="session-pill-name">${medName}</div>
          <div class="session-pill-meta">${pillCount} viên • Hãy lấy thuốc qua cửa mở hướng 6 giờ</div>
        </div>

        <div style="display:flex; flex-direction:column; gap:0.6rem; width:100%;">
          <button id="btn-confirm-taken" class="btn-confirm-safe" onclick="handleConfirmTaken()">
            ✅ Xác nhận đã lấy thuốc ra & Chuyển tiếp / Về Safe 🔒
          </button>
          <button id="btn-emergency-home" class="btn btn-secondary btn-sm" style="width:100%;" onclick="handleEmergencyHome()">
            🏠 Hồi vị khẩn cấp về Safe Home
          </button>
        </div>
      </div>
    `;

    startActiveCountdown(active ? active.started_at : null);
  } else {
    // Render Idle Control Console with dynamic bottom content
    stopActiveCountdown();

    let dynamicBottomContent = '';
    if (appState.consoleMode === 'REFILL') {
      dynamicBottomContent = renderInlineRefillCard();
    } else if (appState.consoleMode === 'EARLY_DISPENSE') {
      dynamicBottomContent = renderInlineEarlyDispenseCard();
    } else {
      dynamicBottomContent = renderUpcomingDosesList();
    }

    consoleArea.innerHTML = `
      <div class="card-panel">
        <div class="panel-header">
          <div class="panel-title">
            <span>⚙️</span> Bảng Điều Khiển Tác Vụ
          </div>
          <span class="status-chip ${appState.device.is_online ? 'online' : 'offline'}">
            ${appState.device.fsm_state || 'SAFE_HOME_IDLE'}
          </span>
        </div>

        <div class="fsm-status-card" style="margin-bottom:1.25rem;">
          <div class="fsm-info">
            <span class="fsm-label">Vị trí khay hiện tại</span>
            <span class="fsm-val">Ngăn ${appState.device.current_slot || 0} ${appState.device.current_slot === 0 ? '(Safe Home 🔒)' : ''}</span>
          </div>
          <span style="font-size:1.6rem;">${appState.device.current_slot === 0 ? '🛡️' : '💊'}</span>
        </div>

        <div class="btn-action-group">
          <button id="btn-open-early-dispense" class="btn ${appState.consoleMode === 'EARLY_DISPENSE' ? 'btn-primary' : 'btn-secondary'} btn-lg" onclick="appState.consoleMode === 'EARLY_DISPENSE' ? cancelInlineTask() : openEarlyDispenseModal()">
            ⚡ Lấy thuốc sớm theo Buổi
          </button>
          <button id="btn-open-refill-modal" class="btn ${appState.consoleMode === 'REFILL' ? 'btn-success' : 'btn-secondary'} btn-lg" onclick="appState.consoleMode === 'REFILL' ? cancelInlineTask() : openRefillModal(appState.selectedRefillSlot || 1)">
            📥 Nạp / Nhập thuốc vào Ngăn
          </button>
        </div>

        <!-- MỤC DYNAMIC: NẠP / LẤY THUỐC SỚM INLINE HOẶC BẢN XEM NHANH ĐƠN THUỐC TIẾP THEO -->
        ${dynamicBottomContent}
      </div>
    `;
  }

  // Khôi phục lại focus và vị trí con trỏ nếu trước đó người dùng đang gõ
  if (activeElId) {
    const restoredEl = document.getElementById(activeElId);
    if (restoredEl) {
      restoredEl.focus();
      if (typeof restoredEl.setSelectionRange === 'function') {
        try { restoredEl.setSelectionRange(selStart, selEnd); } catch (_) { }
      }
    }
  }
}

// Active Session Countdown (15 minutes = 900 seconds)
function startActiveCountdown(startedAtStr) {
  stopActiveCountdown();

  const totalSeconds = 900;
  let elapsed = 0;

  if (startedAtStr) {
    const started = new Date(startedAtStr).getTime();
    elapsed = Math.floor((Date.now() - started) / 1000);
  }

  appState.remainingSeconds = Math.max(0, totalSeconds - elapsed);

  const updateRing = () => {
    const m = Math.floor(appState.remainingSeconds / 60);
    const s = appState.remainingSeconds % 60;
    const timeStr = `${String(m).padStart(2, '0')}:${String(s).padStart(2, '0')}`;

    const textEl = document.getElementById('countdown-digits-text');
    const ringEl = document.getElementById('countdown-progress-ring');

    if (textEl) textEl.textContent = timeStr;

    if (ringEl) {
      const circumference = 440; // 2 * PI * 70
      const offset = circumference - (appState.remainingSeconds / totalSeconds) * circumference;
      ringEl.style.strokeDashoffset = offset;

      if (appState.remainingSeconds <= 60) {
        ringEl.className = 'countdown-progress-circle critical';
        if (appState.carousel3D) appState.carousel3D.setLEDState('red');
      } else if (appState.remainingSeconds <= 300) {
        ringEl.className = 'countdown-progress-circle warning';
        if (appState.carousel3D) appState.carousel3D.setLEDState('amber');
      } else {
        ringEl.className = 'countdown-progress-circle';
      }
    }

    if (appState.remainingSeconds <= 0) {
      stopActiveCountdown();
      showToast('Cữ uống đã quá hạn 15 phút!', 'error');
      loadDashboardData();
    } else {
      appState.remainingSeconds--;
    }
  };

  updateRing();
  appState.countdownInterval = setInterval(updateRing, 1000);
}

function stopActiveCountdown() {
  if (appState.countdownInterval) {
    clearInterval(appState.countdownInterval);
    appState.countdownInterval = null;
  }
}

async function handleConfirmDose() {
  try {
    await window.api.confirmDose();
    showToast('🔔 Đã xác nhận đến uống thuốc! Hộp thuốc đang mở khay...', 'info');
    await loadDashboardData();
  } catch (err) {
    showToast(err.message || 'Lỗi khi xác nhận cữ thuốc', 'error');
  }
}

async function handleConfirmTaken() {
  try {
    await window.api.confirmTaken();
    showToast('✅ Xác nhận đã lấy thuốc ra thành công! Khay đang xoay chuyển tiếp / về Safe an toàn 🔒', 'success');
    if (appState.carousel2D) {
      appState.carousel2D.rotateToSlot(0);
      appState.carousel2D.setLEDState('green');
    }
    appState.consoleMode = 'IDLE';
    await loadDashboardData();
  } catch (err) {
    showToast(err.message || 'Lỗi khi xác nhận lấy thuốc', 'error');
  }
}

async function handleEmergencyHome() {
  try {
    await window.api.returnHome();
    showToast('Đang hồi vị an toàn về Ngăn 0...', 'info');
    if (appState.carousel3D) {
      appState.carousel3D.rotateToSlot(0);
      appState.carousel3D.setLEDState('green');
    }
    await loadDashboardData();
  } catch (err) {
    showToast(err.message || 'Lỗi khi hồi vị', 'error');
  }
}

// --- Tab 1: Slots Inventory Grid (Slots 1..7) ---
function renderSlotsGrid() {
  const grid = document.getElementById('slots-inventory-grid');
  if (!grid) return;

  const validSlots = appState.slots.filter((s) => s.slot_number >= 1 && s.slot_number <= 7);

  grid.innerHTML = validSlots
    .map((slot) => {
      const medName = slot.medicine?.name || 'Chưa nạp thuốc';
      const qty = slot.current_quantity || 0;
      const cap = slot.capacity || Math.max(qty, 20);
      const pct = qty > 0 ? Math.min(100, Math.max(15, Math.round((qty / Math.max(cap, qty, 20)) * 100))) : 0;

      let cardClass = 'slot-card';
      let fillClass = 'slot-stock-fill';

      if (qty === 0) {
        cardClass += ' empty-stock';
        fillClass += ' empty';
      } else if (qty <= 2) {
        cardClass += ' low-stock';
        fillClass += ' warning';
      }

      const slotColorCfg = window.SMARTMED_SLOT_COLORS ? window.SMARTMED_SLOT_COLORS[slot.slot_number] : null;
      const colorDot = slotColorCfg
        ? `<span style="display:inline-flex; align-items:center; gap:5px; font-size:0.75rem; font-weight:700; color:${slotColorCfg.primaryColor};">
             <span style="display:inline-block; width:9px; height:9px; border-radius:50%; background:${slotColorCfg.primaryColor}; box-shadow:0 0 6px ${slotColorCfg.accentColor};"></span>
             ${slotColorCfg.colorName.split(' ')[0]}
           </span>`
        : '';

      return `
      <div class="${cardClass}" data-slot="${slot.slot_number}">
        <div class="slot-card-header">
          <div style="display:flex; align-items:center; gap:0.4rem;">
            <span class="slot-num-badge">Ngăn ${slot.slot_number}</span>
            ${colorDot}
          </div>
          <span style="font-size:0.75rem; font-weight:700; color:${qty <= 2 ? 'var(--warning-amber)' : 'var(--safe-green)'};">
            ${qty === 0 ? 'Trống' : qty <= 2 ? '⚠️ Sắp hết' : 'Đầy đủ'}
          </span>
        </div>

        <div class="slot-med-name" title="${medName}">${medName}</div>

        <div class="slot-stock-bar">
          <div class="${fillClass}" style="width: ${pct}%;"></div>
        </div>

        <div class="slot-stock-numbers">
          <span>Tồn kho:</span>
          <span><strong>${qty}</strong> viên</span>
        </div>

        <div style="display:flex; justify-content:space-between; align-items:center; font-size:0.75rem; margin-top:0.35rem; padding:3px 8px; background:rgba(37,99,235,0.06); border-radius:6px; border:1px solid rgba(37,99,235,0.15);">
          <span style="color:var(--text-secondary); font-weight:600;">Liều dùng:</span>
          <span style="font-weight:700; color:#2563eb;">💊 ${slot.default_dose || 1} viên / cữ</span>
        </div>

        <div style="display:flex; gap:0.4rem; margin-top:0.25rem;">
          <button class="btn btn-secondary btn-sm" style="flex:1;" onclick="openRefillModal(${slot.slot_number})">
            📥 Nạp ngăn
          </button>
          <button class="btn btn-sm" style="background:#f1f5f9; color:#334155; border:1px solid #cbd5e1;" title="Xoay ngăn này về cửa 6 giờ" onclick="appState.carousel2D && appState.carousel2D.rotateToSlot(${slot.slot_number})">
            🔄 6h
          </button>
        </div>
      </div>
    `;
    })
    .join('');
}

// --- Tab 2: Schedules Timeline View (4 Sessions) ---
function renderSchedulesView() {
  const container = document.getElementById('schedules-sessions-container');
  if (!container) return;

  const isOnline = appState.device.is_online;
  const offlineBanner = !isOnline
    ? `
    <div class="schedule-offline-alert" style="background: rgba(239, 68, 68, 0.12); border: 1.5px solid rgba(239, 68, 68, 0.4); border-radius: 10px; padding: 0.85rem 1.25rem; margin-bottom: 1.25rem; display: flex; align-items: center; gap: 0.75rem;">
      <span style="font-size: 1.4rem;">🚫</span>
      <div>
        <div style="font-weight: 700; color: #ef4444; font-size: 0.95rem;">Hộp thuốc đang Ngoại Tuyến (Offline)</div>
        <div style="font-size: 0.82rem; color: var(--text-muted); margin-top: 0.15rem;">Không thể thêm mới, sửa đổi, bật/tắt hoặc xóa lịch uống thuốc. Vui lòng kết nối nguồn và Wi-Fi cho thiết bị để thao tác.</div>
      </div>
    </div>
  `
    : '';

  // 4 Sessions Definition
  const sessions = [
    { id: 'morning', name: 'Buổi Sáng', icon: '🌅', range: '05:00 – 10:59', start: '05:00', end: '10:59' },
    { id: 'noon', name: 'Buổi Trưa', icon: '☀️', range: '11:00 – 13:59', start: '11:00', end: '13:59' },
    { id: 'afternoon', name: 'Buổi Chiều', icon: '🌇', range: '14:00 – 17:59', start: '14:00', end: '17:59' },
    { id: 'evening', name: 'Buổi Tối', icon: '🌙', range: '18:00 – 04:59', start: '18:00', end: '04:59' },
  ];

  container.innerHTML = offlineBanner + sessions
    .map((sess) => {
      const sessSchedules = appState.schedules.filter((s) => {
        const t = s.time_of_day || s.time;
        return isTimeInSession(t, sess.id);
      });

      return `
      <div class="session-block">
        <div class="session-title-bar">
          <div class="session-badge ${sess.id}">
            <span>${sess.icon}</span> ${sess.name}
          </div>
          <span class="session-range">Khung giờ: ${sess.range} (${sessSchedules.length} cữ)</span>
        </div>

        ${sessSchedules.length === 0
          ? `<p style="font-size:0.85rem; color:var(--text-muted); font-style:italic;">Chưa có cữ thuốc nào trong buổi này.</p>`
          : `<div class="schedules-cards-list">
              ${sessSchedules.map((sch) => renderScheduleCard(sch)).join('')}
            </div>`
        }
      </div>
    `;
    })
    .join('');
}

function renderScheduleCard(sch) {
  const slot = appState.slots.find((s) => s.slot_number === sch.slot_number);
  const medName = sch.medicine_name || slot?.medicine?.name || `Thuốc Ngăn ${sch.slot_number}`;
  const timeVal = sch.time_of_day || sch.time || '00:00';
  const doseQty = sch.dose_quantity || sch.pill_count || 1;
  const isOnline = appState.device.is_online;

  return `
    <div class="schedule-card ${sch.is_active ? '' : 'disabled'} ${!isOnline ? 'offline-locked' : ''}">
      <div class="schedule-top">
        <span class="schedule-time tabular-nums">${timeVal}</span>
        <label class="schedule-switch" title="${!isOnline ? 'Hộp thuốc ngoại tuyến - Không thể đổi' : ''}">
          <input type="checkbox" ${sch.is_active ? 'checked' : ''} ${!isOnline ? 'disabled' : ''} onchange="toggleScheduleActive(${sch.id})">
          <span class="slider" style="${!isOnline ? 'opacity: 0.4; cursor: not-allowed;' : ''}"></span>
        </label>
      </div>

      <div class="schedule-med-details">
        <div class="schedule-med-name">${medName}</div>
        <div class="schedule-slot-tag">
          <span>Ngăn ${sch.slot_number}</span> • <span>${doseQty} viên / cữ</span>
        </div>
      </div>

      <div style="display:flex; justify-content:flex-end; align-items:center; margin-top:0.35rem;">
        <button class="btn-icon-danger" onclick="deleteScheduleItem(${sch.id})" ${!isOnline ? 'disabled' : ''} style="${!isOnline ? 'opacity: 0.25; cursor: not-allowed;' : ''}" title="${!isOnline ? 'Không thể xóa khi hộp thuốc Offline' : 'Xóa cữ uống'}">
          🗑️
        </button>
      </div>
    </div>
  `;
}

function isTimeInSession(timeStr, sessionId) {
  if (!timeStr) return false;
  const [h, m] = timeStr.split(':').map(Number);
  const minutes = h * 60 + m;

  switch (sessionId) {
    case 'morning':
      return minutes >= 5 * 60 && minutes <= 10 * 60 + 59;
    case 'noon':
      return minutes >= 11 * 60 && minutes <= 13 * 60 + 59;
    case 'afternoon':
      return minutes >= 14 * 60 && minutes <= 17 * 60 + 59;
    case 'evening':
      return minutes >= 18 * 60 || minutes < 5 * 60;
    default:
      return false;
  }
}

async function toggleScheduleActive(scheduleId) {
  if (!appState.device.is_online) {
    showToast('Hộp thuốc đang ngoại tuyến (Offline). Không thể bật/tắt lịch!', 'warning');
    return;
  }
  try {
    await window.api.toggleSchedule(scheduleId);
    showToast('Đã cập nhật trạng thái cữ uống!', 'success');
    await loadDashboardData();
  } catch (err) {
    showToast(err.message || 'Lỗi khi cập nhật cữ uống', 'error');
  }
}

async function deleteScheduleItem(scheduleId) {
  if (!appState.device.is_online) {
    showToast('Hộp thuốc đang ngoại tuyến (Offline). Không thể xóa lịch!', 'warning');
    return;
  }
  if (!confirm('Bạn có chắc muốn xóa cữ uống này không?')) return;
  try {
    await window.api.deleteSchedule(scheduleId);
    showToast('Đã xóa cữ uống!', 'success');
    await loadDashboardData();
  } catch (err) {
    showToast(err.message || 'Lỗi khi xóa cữ uống', 'error');
  }
}

// --- Tab 3: Hardware Diagnostics ---
function setupHardwareView() {
  // Motor dock 8 buttons
  const buttonsContainer = document.getElementById('motor-dock-buttons-container');
  if (buttonsContainer) {
    buttonsContainer.innerHTML = [0, 1, 2, 3, 4, 5, 6, 7]
      .map((slot) => {
        const cfg = window.SMARTMED_SLOT_COLORS ? window.SMARTMED_SLOT_COLORS[slot] : null;
        const colorBorder = cfg ? `border-top: 3.5px solid ${cfg.primaryColor};` : '';
        return `
          <button class="slot-test-btn ${slot === 0 ? 'slot-0' : ''}" style="${colorBorder}" onclick="testMoveMotor(${slot})">
            <span class="btn-num">${slot === 0 ? '🔒 0' : slot}</span>
            <span class="btn-sub">${slot === 0 ? 'Safe Home' : 'Ngăn ' + slot}</span>
          </button>
        `;
      })
      .join('');
  }

  // Jog buttons
  document.getElementById('btn-jog-back')?.addEventListener('click', () => {
    const nextSlot = (appState.device.current_slot - 1 + 8) % 8;
    testMoveMotor(nextSlot);
  });

  document.getElementById('btn-jog-next')?.addEventListener('click', () => {
    const nextSlot = (appState.device.current_slot + 1) % 8;
    testMoveMotor(nextSlot);
  });

  // Homing Button
  document.getElementById('btn-trigger-homing')?.addEventListener('click', async () => {
    try {
      showToast('Đang kích hoạt Auto-Homing TCRT5000...', 'info');
      await window.api.homing();
      if (appState.carousel3D) {
        appState.carousel3D.rotateToSlot(0);
        appState.carousel3D.setLEDState('green');
      }
    } catch (err) {
      showToast(err.message || 'Lỗi hiệu chuẩn homing', 'error');
    }
  });
}

async function testMoveMotor(slot) {
  try {
    showToast(`Đang xoay khay đưa Ngăn ${slot} về cửa 6 giờ...`, 'info');
    await window.api.moveMotor(slot);
    if (appState.carousel2D) {
      appState.carousel2D.rotateToSlot(slot);
    }
    appState.device.current_slot = slot;
    renderStatsCards();
  } catch (err) {
    showToast(err.message || 'Lỗi điều khiển motor', 'error');
  }
}

// --- Tab 4: Audit Dose History ---
// ============================================================================
// TAB 4: AUDIT DOSE HISTORY & ANALYTICS (TRA CỨU THEO TỪNG NGÀY)
// ============================================================================

const historyState = {
  selectedDate: getLocalDateString(new Date()),
  filter: 'ALL',
  searchTerm: '',
  viewMode: 'table',
  allLogs: [],
};

function getLocalDateString(d = new Date()) {
  const year = d.getFullYear();
  const month = String(d.getMonth() + 1).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  return `${year}-${month}-${day}`;
}

function parseDateSafe(dateStr) {
  if (!dateStr) return null;
  if (dateStr instanceof Date) return dateStr;
  const s = String(dateStr).trim().replace(' ', 'T');
  const d = new Date(s);
  return isNaN(d.getTime()) ? null : d;
}

function getDayOfWeekVN(dateInput) {
  const d = parseDateSafe(dateInput) || new Date();
  const days = ['Chủ Nhật', 'Thứ Hai', 'Thứ Ba', 'Thứ Tư', 'Thứ Năm', 'Thứ Sáu', 'Thứ Bảy'];
  return days[d.getDay()];
}

function formatDateOnlyVN(dateStr) {
  const d = parseDateSafe(dateStr);
  if (!d) return '--';
  const day = String(d.getDate()).padStart(2, '0');
  const month = String(d.getMonth() + 1).padStart(2, '0');
  const year = d.getFullYear();
  return `${day}/${month}/${year}`;
}

function formatTimeOnlyVN(dateStr) {
  const d = parseDateSafe(dateStr);
  if (!d) return '--:--';
  const hours = String(d.getHours()).padStart(2, '0');
  const minutes = String(d.getMinutes()).padStart(2, '0');
  return `${hours}:${minutes}`;
}

function formatFullDateTimeVN(dateStr) {
  const d = parseDateSafe(dateStr);
  if (!d) return '--';
  const hours = String(d.getHours()).padStart(2, '0');
  const minutes = String(d.getMinutes()).padStart(2, '0');
  const day = String(d.getDate()).padStart(2, '0');
  const month = String(d.getMonth() + 1).padStart(2, '0');
  const year = d.getFullYear();
  return `${hours}:${minutes} - ${day}/${month}/${year}`;
}

function normalizeActualDate(actualStr, schedStr) {
  if (!actualStr) return null;
  const dActual = parseDateSafe(actualStr);
  if (!dActual) return null;

  if (schedStr) {
    const dSched = parseDateSafe(schedStr);
    if (dSched) {
      const rawDiffMin = (dActual.getTime() - dSched.getTime()) / 60000;
      if (rawDiffMin <= -330 && rawDiffMin >= -510) {
        return new Date(dActual.getTime() + 7 * 3600 * 1000);
      }
    }
  }
  return dActual;
}

function formatDeltaDuration(diffMin) {
  const abs = Math.abs(diffMin);
  const h = Math.floor(abs / 60);
  const m = abs % 60;
  if (h > 0 && m > 0) return `${h}h ${m}p`;
  if (h > 0) return `${h} giờ`;
  return `${m} phút`;
}

const SESSIONS_CONFIG = {
  morning: {
    key: 'morning',
    order: 1,
    name: 'Buổi Sáng',
    icon: '🌅',
    timeRange: '05:00 - 10:59',
    badgeClass: 'session-morning',
  },
  noon: {
    key: 'noon',
    order: 2,
    name: 'Buổi Trưa',
    icon: '☀️',
    timeRange: '11:00 - 13:59',
    badgeClass: 'session-noon',
  },
  afternoon: {
    key: 'afternoon',
    order: 3,
    name: 'Buổi Chiều',
    icon: '🌤️',
    timeRange: '14:00 - 17:59',
    badgeClass: 'session-afternoon',
  },
  evening: {
    key: 'evening',
    order: 4,
    name: 'Buổi Tối',
    icon: '🌙',
    timeRange: '18:00 - 04:59',
    badgeClass: 'session-evening',
  },
};

function getSessionInfo(log) {
  const textToCheck = `${log.schedule_label || ''} ${log.notes || ''}`.toLowerCase();
  if (textToCheck.includes('sáng') || textToCheck.includes('sang') || textToCheck.includes('morning')) {
    return SESSIONS_CONFIG.morning;
  }
  if (textToCheck.includes('trưa') || textToCheck.includes('trua') || textToCheck.includes('noon')) {
    return SESSIONS_CONFIG.noon;
  }
  if (textToCheck.includes('chiều') || textToCheck.includes('chieu') || textToCheck.includes('afternoon')) {
    return SESSIONS_CONFIG.afternoon;
  }
  if (textToCheck.includes('tối') || textToCheck.includes('toi') || textToCheck.includes('evening') || textToCheck.includes('night')) {
    return SESSIONS_CONFIG.evening;
  }

  let hour = -1;
  if (log.schedule_time && typeof log.schedule_time === 'string' && log.schedule_time.includes(':')) {
    hour = parseInt(log.schedule_time.split(':')[0], 10);
  }
  if (isNaN(hour) || hour < 0) {
    const dSched = parseDateSafe(log.scheduled_time);
    if (dSched) hour = dSched.getHours();
  }
  if (isNaN(hour) || hour < 0) {
    const dActual = parseDateSafe(log.confirmed_time || log.dispensed_time || log.created_at);
    if (dActual) hour = dActual.getHours();
  }

  if (hour >= 5 && hour < 11) return SESSIONS_CONFIG.morning;
  if (hour >= 11 && hour < 14) return SESSIONS_CONFIG.noon;
  if (hour >= 14 && hour < 18) return SESSIONS_CONFIG.afternoon;
  return SESSIONS_CONFIG.evening;
}

function calcAdherenceDelta(log) {
  if (log.status === 'MISSED') {
    return {
      label: 'Bỏ lỡ',
      badgeClass: 'missed',
      diffText: 'Không lấy',
    };
  }

  if (log.status === 'EARLY_TAKEN' || log.trigger_source === 'EARLY_MANUAL') {
    return {
      label: 'Lấy sớm',
      badgeClass: 'early',
      diffText: '⚡ Lấy sớm',
    };
  }

  const dActual = normalizeActualDate(log.confirmed_time || log.dispensed_time, log.scheduled_time);
  const dSched = parseDateSafe(log.scheduled_time);

  if (dActual && dSched) {
    const diffMin = Math.round((dActual.getTime() - dSched.getTime()) / 60000);
    if (Math.abs(diffMin) <= 15) {
      return {
        label: 'Đúng cữ',
        badgeClass: 'on-time',
        diffText: diffMin === 0 ? '✓ Đúng giờ' : (diffMin > 0 ? `+${diffMin}p` : `${diffMin}p`),
      };
    }
    if (diffMin > 15) {
      return {
        label: 'Uống trễ',
        badgeClass: 'late',
        diffText: `⏱️ Trễ ${formatDeltaDuration(diffMin)}`,
      };
    }
    return {
      label: 'Lấy sớm',
      badgeClass: 'early',
      diffText: `⚡ Sớm ${formatDeltaDuration(Math.abs(diffMin))}`,
    };
  }

  return { label: 'Theo cữ', badgeClass: 'on-time', diffText: '✓ Đúng giờ' };
}

function getUnifiedStatusBadge(log) {
  if (log.status === 'EARLY_TAKEN' || log.trigger_source === 'EARLY_MANUAL') {
    return {
      type: 'EARLY_TAKEN',
      text: 'Lấy sớm',
      badgeHtml: '<span class="status-badge early">🔵 Lấy sớm</span>',
      nodeClass: 'early',
    };
  }
  if (log.status === 'MISSED') {
    return {
      type: 'MISSED',
      text: 'Bỏ lỡ',
      badgeHtml: '<span class="status-badge missed">🟡 Bỏ lỡ</span>',
      nodeClass: 'missed',
    };
  }
  return {
    type: 'TAKEN',
    text: 'Theo cữ',
    badgeHtml: '<span class="status-badge taken">🟢 Theo cữ</span>',
    nodeClass: 'taken',
  };
}

function getSlotBadgeHtml(slotNumber) {
  const num = parseInt(slotNumber, 10);
  if (num === 0) return `<span class="status-badge" style="background:#e2e8f0; color:#334155;">Safe Home</span>`;
  return `<span class="status-chip" style="background:var(--primary-light); color:var(--primary); font-weight:800; border:1px solid var(--primary-border);">Ngăn ${num}</span>`;
}

function updateHistoryKPIs(logs) {
  const total = logs.length;
  const taken = logs.filter((l) => l.status === 'TAKEN' || l.status === 'EARLY_TAKEN' || l.trigger_source === 'EARLY_MANUAL').length;
  const missed = logs.filter((l) => l.status === 'MISSED').length;
  const rate = total > 0 ? Math.round((taken / total) * 100) : 100;

  const adherenceEl = document.getElementById('hist-kpi-adherence');
  const badgeEl = document.getElementById('hist-kpi-badge');
  const totalEl = document.getElementById('hist-kpi-total');
  const takenEl = document.getElementById('hist-kpi-taken');
  const missedEl = document.getElementById('hist-kpi-missed');
  const scopeEl = document.getElementById('hist-kpi-scope');

  if (adherenceEl) adherenceEl.textContent = `${rate}%`;
  if (badgeEl) {
    if (rate >= 85) {
      badgeEl.className = 'kpi-badge good';
      badgeEl.textContent = 'Tốt';
    } else if (rate >= 70) {
      badgeEl.className = 'kpi-badge warning';
      badgeEl.textContent = 'Khá';
    } else {
      badgeEl.className = 'kpi-badge danger';
      badgeEl.textContent = 'Cần nhắc';
    }
  }
  if (totalEl) totalEl.textContent = total;
  if (takenEl) takenEl.textContent = taken;
  if (missedEl) missedEl.textContent = missed;

  if (scopeEl) {
    const todayStr = getLocalDateString(new Date());
    const yestStr = getLocalDateString(new Date(Date.now() - 86400000));
    if (historyState.selectedDate === todayStr) {
      scopeEl.textContent = `Hôm nay (${formatDateOnlyVN(todayStr)})`;
    } else if (historyState.selectedDate === yestStr) {
      scopeEl.textContent = `Hôm qua (${formatDateOnlyVN(yestStr)})`;
    } else {
      scopeEl.textContent = `Ngày ${formatDateOnlyVN(historyState.selectedDate)}`;
    }
  }
}

function groupLogsBySession(logs) {
  const sessions = {
    morning: { ...SESSIONS_CONFIG.morning, doses: [] },
    noon: { ...SESSIONS_CONFIG.noon, doses: [] },
    afternoon: { ...SESSIONS_CONFIG.afternoon, doses: [] },
    evening: { ...SESSIONS_CONFIG.evening, doses: [] },
  };

  logs.forEach((log) => {
    const sessInfo = getSessionInfo(log);
    sessions[sessInfo.key].doses.push(log);
  });

  Object.values(sessions).forEach((sess) => {
    sess.doses.sort((a, b) => {
      const timeA = a.schedule_time || formatTimeOnlyVN(a.scheduled_time) || '99:99';
      const timeB = b.schedule_time || formatTimeOnlyVN(b.scheduled_time) || '99:99';
      if (timeA !== timeB) return timeA.localeCompare(timeB);
      return (a.id || 0) - (b.id || 0);
    });
  });

  return sessions;
}

function getMedicineInfoForLog(log) {
  let rawName = (log.medicine_name && String(log.medicine_name).trim()) ? String(log.medicine_name).trim() : '';
  let unit = (log.medicine_dosage_unit && String(log.medicine_dosage_unit).trim()) ? String(log.medicine_dosage_unit).trim() : '';

  if (!rawName && appState.slots && appState.slots.length > 0) {
    const matchedSlot = appState.slots.find((s) => s.slot_number === log.slot_number);
    if (matchedSlot && matchedSlot.medicine) {
      if (matchedSlot.medicine.name && String(matchedSlot.medicine.name).trim()) {
        rawName = String(matchedSlot.medicine.name).trim();
      }
      if (!unit && matchedSlot.medicine.dosage_unit) {
        unit = String(matchedSlot.medicine.dosage_unit).trim();
      }
    }
  }

  if (!rawName) {
    rawName = `Thuốc Ngăn ${log.slot_number}`;
  }

  const medName = rawName.charAt(0).toUpperCase() + rawName.slice(1);
  const medUnit = unit ? `(${unit})` : '(viên)';
  return { medName, medUnit, rawName };
}

function renderHistoryTable(logs) {
  const tbody = document.getElementById('history-table-body');
  if (!tbody) return;

  const curDateStr = historyState.selectedDate;
  const dayName = getDayOfWeekVN(curDateStr);
  const formattedDate = formatDateOnlyVN(curDateStr);

  if (logs.length === 0) {
    tbody.innerHTML = `
      <tr class="table-day-row">
        <td colspan="5">
          <div class="table-day-header-inner">
            <span>📅 ${dayName}, ${formattedDate}</span>
            <span class="day-badge-total">0 cữ uống</span>
          </div>
        </td>
      </tr>
      <tr>
        <td colspan="5" style="text-align:center; padding:3rem 1rem; color:var(--text-muted);">
          <div style="font-size:2rem; margin-bottom:0.5rem;">🔍</div>
          <div style="font-weight:700; color:var(--text-main); font-size:1rem;">Không có cữ uống nào trong ngày này</div>
          <div style="font-size:0.85rem; margin-top:0.35rem;">Bạn có thể bấm nút "Hôm nay" hoặc chọn ngày khác để xem nhật ký.</div>
        </td>
      </tr>
    `;
    return;
  }

  const sessions = groupLogsBySession(logs);
  let html = `
    <tr class="table-day-row">
      <td colspan="5">
        <div class="table-day-header-inner">
          <span>📅 ${dayName}, ${formattedDate}</span>
          <span class="day-badge-total">${logs.length} cữ ghi nhận</span>
        </div>
      </td>
    </tr>
    <tr class="table-col-header-row">
      <th style="width:130px;">⏰ Giờ Hẹn</th>
      <th>💊 Ngăn & Tên Thuốc</th>
      <th style="width:180px;">⏱️ Thời Gian Lấy Thực Tế</th>
      <th style="width:140px;">🏷️ Trạng Thái</th>
      <th style="width:110px; text-align:center;">🎯 Khay 2D</th>
    </tr>
  `;

  const sessionKeys = ['morning', 'noon', 'afternoon', 'evening'];
  sessionKeys.forEach((sessKey) => {
    const sess = sessions[sessKey];
    if (!sess.doses || sess.doses.length === 0) return;

    html += `
      <tr class="table-session-row ${sess.badgeClass}">
        <td colspan="5">
          <div class="table-session-header-inner">
            <span class="session-pill ${sess.badgeClass}">
              <span>${sess.icon}</span>
              <strong>${sess.name}</strong>
              <span class="session-timerange">(${sess.timeRange})</span>
            </span>
            <span class="session-count-tag">${sess.doses.length} cữ</span>
          </div>
        </td>
      </tr>
    `;

    sess.doses.forEach((log) => {
      let schedTime = log.schedule_time;
      if (!schedTime && log.scheduled_time) schedTime = formatTimeOnlyVN(log.scheduled_time);
      if (!schedTime) schedTime = '--:--';

      const schedLabel = log.schedule_label || '';
      const medInfo = getMedicineInfoForLog(log);
      const medName = medInfo.medName;
      const medUnit = medInfo.medUnit;

      const dActualNorm = normalizeActualDate(log.confirmed_time || log.dispensed_time, log.scheduled_time);
      const actualTimeStr = dActualNorm
        ? `${String(dActualNorm.getHours()).padStart(2, '0')}:${String(dActualNorm.getMinutes()).padStart(2, '0')}:${String(dActualNorm.getSeconds()).padStart(2, '0')}`
        : '--:--';

      const adherence = calcAdherenceDelta(log);
      const diffBadgeHtml = actualTimeStr !== '--:--'
        ? `<span class="time-diff-badge ${adherence.badgeClass}">${adherence.diffText}</span>`
        : (log.status === 'MISSED' ? `<span class="time-diff-badge missed">Không lấy</span>` : '');

      const unifiedStatus = getUnifiedStatusBadge(log);

      html += `
        <tr class="dose-row">
          <td>
            <div class="dose-sched-wrap">
              <span class="dose-sched-time tabular-nums">${schedTime}</span>
              <div class="dose-meta-sub">
                <span class="dose-id-tag">#${log.id}</span>
                ${schedLabel ? `<span style="font-size:0.7rem; color:var(--text-muted);">${schedLabel}</span>` : ''}
              </div>
            </div>
          </td>
          <td>
            <div class="dose-slot-med-wrap">
              ${getSlotBadgeHtml(log.slot_number)}
              <div class="dose-med-detail">
                <strong class="dose-med-name">${medName}</strong>
                <span class="dose-med-unit">${medUnit}</span>
              </div>
            </div>
          </td>
          <td>
            <div class="dose-actual-wrap">
              <span class="dose-actual-time tabular-nums ${actualTimeStr === '--:--' ? 'empty' : ''}">${actualTimeStr !== '--:--' ? actualTimeStr : (log.status === 'MISSED' ? 'Không lấy' : 'Chưa lấy')}</span>
              ${diffBadgeHtml}
            </div>
          </td>
          <td>
            ${unifiedStatus.badgeHtml}
          </td>
          <td style="text-align:center;">
            <button type="button" class="btn-inspect-slot" onclick="inspectSlotOnCarousel(${log.slot_number})" title="Xem ngăn ${log.slot_number} trên khay xoay 2D">
              Khay 2D ↗
            </button>
          </td>
        </tr>
      `;
    });
  });

  tbody.innerHTML = html;
}

function renderHistoryTimeline(logs) {
  const container = document.getElementById('history-timeline-body');
  if (!container) return;

  const curDateStr = historyState.selectedDate;
  const dayName = getDayOfWeekVN(curDateStr);
  const formattedDate = formatDateOnlyVN(curDateStr);

  if (logs.length === 0) {
    container.innerHTML = `
      <div style="text-align:center; padding:3rem 1rem; color:var(--text-muted);">
        <div style="font-size:2.5rem; margin-bottom:0.5rem;">📅</div>
        <div style="font-weight:700; color:var(--text-main);">Không có cữ uống nào trong ngày ${formattedDate}</div>
        <div style="font-size:0.82rem; margin-top:0.25rem;">Bạn có thể chọn ngày khác để xem dòng thời gian.</div>
      </div>
    `;
    return;
  }

  const sessions = groupLogsBySession(logs);
  let html = `
    <div style="margin-bottom:0.75rem; display:flex; justify-content:space-between; align-items:center;">
      <strong style="font-size:1rem; color:var(--text-main);">📅 Dòng thời gian: ${dayName}, ${formattedDate}</strong>
      <span class="day-badge-total">${logs.length} cữ uống</span>
    </div>
  `;

  const sessionKeys = ['morning', 'noon', 'afternoon', 'evening'];
  sessionKeys.forEach((sessKey) => {
    const sess = sessions[sessKey];
    if (!sess.doses || sess.doses.length === 0) return;

    html += `
      <div class="timeline-session-panel ${sess.badgeClass}">
        <div class="timeline-session-header">
          <div class="timeline-session-title">
            <span class="session-icon">${sess.icon}</span>
            <strong class="session-name">${sess.name}</strong>
            <span class="session-timerange-chip">${sess.timeRange}</span>
          </div>
          <span class="session-doses-badge">${sess.doses.length} cữ</span>
        </div>
        <div class="timeline-session-items">
    `;

    sess.doses.forEach((log) => {
      let schedTime = log.schedule_time;
      if (!schedTime && log.scheduled_time) schedTime = formatTimeOnlyVN(log.scheduled_time);
      if (!schedTime) schedTime = '--:--';

      const schedLabel = log.schedule_label || '';
      const medInfo = getMedicineInfoForLog(log);
      const medName = medInfo.medName;
      const medUnit = medInfo.medUnit;

      const dActualNorm = normalizeActualDate(log.confirmed_time || log.dispensed_time, log.scheduled_time);
      const actualTimeStr = dActualNorm
        ? `${String(dActualNorm.getHours()).padStart(2, '0')}:${String(dActualNorm.getMinutes()).padStart(2, '0')}:${String(dActualNorm.getSeconds()).padStart(2, '0')}`
        : '--:--';

      const adherence = calcAdherenceDelta(log);
      const diffBadgeHtml = actualTimeStr !== '--:--'
        ? `<span class="time-diff-badge ${adherence.badgeClass}">${adherence.diffText}</span>`
        : (log.status === 'MISSED' ? `<span class="time-diff-badge missed">Không lấy</span>` : '');

      const unifiedStatus = getUnifiedStatusBadge(log);

      html += `
        <div class="timeline-dose-card ${unifiedStatus.nodeClass}">
          <div class="dose-step-cell">
            <span class="step-label">⏰ Giờ Hẹn</span>
            <span class="step-val sched-time tabular-nums">${schedTime}</span>
            <span style="font-size:0.7rem; color:var(--text-muted);">#${log.id} ${schedLabel}</span>
          </div>
          <div class="dose-step-cell">
            <span class="step-label">💊 Ngăn & Thuốc</span>
            <div style="display:flex; align-items:center; gap:0.4rem;">
              ${getSlotBadgeHtml(log.slot_number)}
              <strong style="font-size:0.92rem; color:#0f172a;">${medName}</strong>
            </div>
            <span style="font-size:0.72rem; color:var(--text-muted);">${medUnit}</span>
          </div>
          <div class="dose-step-cell">
            <span class="step-label">⏱️ Thời Gian Lấy</span>
            <span class="tabular-nums" style="font-weight:700; font-size:0.95rem; color:#0f172a;">${actualTimeStr !== '--:--' ? actualTimeStr : (log.status === 'MISSED' ? 'Không lấy' : 'Chưa lấy')}</span>
            ${diffBadgeHtml}
          </div>
          <div class="dose-step-cell">
            <span class="step-label">🏷️ Trạng Thái</span>
            <div>${unifiedStatus.badgeHtml}</div>
          </div>
          <div style="text-align:right;">
            <button type="button" class="btn-inspect-slot" onclick="inspectSlotOnCarousel(${log.slot_number})">
              Khay 2D ↗
            </button>
          </div>
        </div>
      `;
    });

    html += `
        </div>
      </div>
    `;
  });

  container.innerHTML = html;
}

function applyHistoryFilterAndSearch() {
  let logs = historyState.allLogs;

  if (historyState.filter && historyState.filter !== 'ALL') {
    if (historyState.filter === 'TAKEN') {
      logs = logs.filter((l) => l.status === 'TAKEN' || l.trigger_source === 'LOCAL_BUTTON');
    } else if (historyState.filter === 'EARLY_TAKEN') {
      logs = logs.filter((l) => l.status === 'EARLY_TAKEN' || l.trigger_source === 'EARLY_MANUAL');
    } else if (historyState.filter === 'MISSED') {
      logs = logs.filter((l) => l.status === 'MISSED');
    }
  }

  const term = (historyState.searchTerm || '').trim().toLowerCase();
  if (term) {
    logs = logs.filter((l) => {
      const idMatch = String(l.id).includes(term);
      const slotMatch = `ngăn ${l.slot_number}`.includes(term) || String(l.slot_number) === term;
      const medInfo = getMedicineInfoForLog(l);
      const medMatch = medInfo.medName.toLowerCase().includes(term);
      const notesMatch = (l.notes || '').toLowerCase().includes(term);
      return idMatch || slotMatch || medMatch || notesMatch;
    });
  }

  logs.sort((a, b) => {
    const timeA = a.schedule_time || formatTimeOnlyVN(a.scheduled_time) || '99:99';
    const timeB = b.schedule_time || formatTimeOnlyVN(b.scheduled_time) || '99:99';
    if (timeA !== timeB) return timeA.localeCompare(timeB);
    return (a.id || 0) - (b.id || 0);
  });

  updateHistoryKPIs(historyState.allLogs);

  if (historyState.viewMode === 'timeline') {
    renderHistoryTimeline(logs);
  } else {
    renderHistoryTable(logs);
  }
}

async function loadHistoryData(targetDate = null, targetFilter = null, isSilent = false) {
  const tbody = document.getElementById('history-table-body');

  if (targetDate) historyState.selectedDate = targetDate;
  if (targetFilter) historyState.filter = targetFilter;

  // Sync date picker input
  const datePicker = document.getElementById('history-date-picker');
  if (datePicker && datePicker.value !== historyState.selectedDate) {
    datePicker.value = historyState.selectedDate;
  }

  // Update quick buttons active state
  const todayStr = getLocalDateString(new Date());
  const yestStr = getLocalDateString(new Date(Date.now() - 86400000));
  document.getElementById('btn-quick-today')?.classList.toggle('active', historyState.selectedDate === todayStr);
  document.getElementById('btn-quick-yesterday')?.classList.toggle('active', historyState.selectedDate === yestStr);

  const nextBtn = document.getElementById('btn-next-day');
  if (nextBtn) {
    nextBtn.disabled = (historyState.selectedDate >= todayStr);
    nextBtn.style.opacity = (historyState.selectedDate >= todayStr) ? '0.4' : '1';
  }

  if (tbody && !isSilent && (!historyState.allLogs || historyState.allLogs.length === 0)) {
    tbody.innerHTML = `<tr><td colspan="5" style="text-align:center; padding:2.5rem; color:var(--text-muted);"><div style="font-size:1.5rem;">⏳</div><div style="font-weight:700; margin-top:0.35rem;">Đang tải nhật ký ngày ${formatDateOnlyVN(historyState.selectedDate)}...</div></td></tr>`;
  }

  // Ensure slot medicine information is loaded
  if (!appState.slots || appState.slots.length === 0) {
    try {
      const slotsData = await window.api.getSlots();
      if (Array.isArray(slotsData)) appState.slots = slotsData;
    } catch (_) {}
  }

  try {
    const logs = await window.api.getHistory(
      100,
      historyState.filter !== 'ALL' ? historyState.filter : null,
      historyState.selectedDate
    );
    historyState.allLogs = Array.isArray(logs) ? logs : [];
    applyHistoryFilterAndSearch();
  } catch (err) {
    if (tbody && !isSilent) {
      tbody.innerHTML = `<tr><td colspan="5" style="text-align:center; color:var(--danger-red); padding:1.5rem;">⚠️ Lỗi tải nhật ký: ${err.message}</td></tr>`;
    }
  }
}

function exportHistoryToCSV() {
  const logs = historyState.allLogs;
  if (!logs || logs.length === 0) {
    showToast(`Không có dữ liệu cữ uống nào trong ngày ${formatDateOnlyVN(historyState.selectedDate)} để xuất.`, 'warning');
    return;
  }

  const headers = ['Mã Cữ', 'Ngày Uống', 'Buổi', 'Giờ Hẹn', 'Ngăn Khay', 'Tên Thuốc', 'Liều Lượng', 'Thời Gian Lấy', 'Trạng Thái', 'Ghi Chú'];
  const escapeCSV = (val) => `"${String(val || '').replace(/"/g, '""')}"`;

  const rows = logs.map((l) => {
    const schedDate = formatDateOnlyVN(l.scheduled_time || l.created_at);
    let schedTime = l.schedule_time || formatTimeOnlyVN(l.scheduled_time) || '--:--';
    const dActualNorm = normalizeActualDate(l.confirmed_time || l.dispensed_time, l.scheduled_time);
    const actualTimeStr = dActualNorm ? formatTimeOnlyVN(dActualNorm) : '--:--';
    const unifiedStatus = getUnifiedStatusBadge(l);
    const sess = getSessionInfo(l);
    const medInfo = getMedicineInfoForLog(l);

    return [
      escapeCSV(l.id),
      escapeCSV(schedDate),
      escapeCSV(sess.name),
      escapeCSV(schedTime),
      escapeCSV(`Ngăn ${l.slot_number}`),
      escapeCSV(medInfo.medName),
      escapeCSV(medInfo.medUnit.replace(/[()]/g, '')),
      escapeCSV(actualTimeStr),
      escapeCSV(unifiedStatus.text),
      escapeCSV(l.notes || ''),
    ].join(',');
  });

  const csvContent = '\uFEFF' + [headers.join(','), ...rows].join('\r\n');
  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `SmartMed_Lich_Su_${historyState.selectedDate}.csv`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
  showToast(`📥 Đã xuất báo cáo ngày ${formatDateOnlyVN(historyState.selectedDate)} (${logs.length} cữ) sang CSV!`, 'success');
}

window.inspectSlotOnCarousel = function (slotNumber) {
  const num = parseInt(slotNumber, 10);
  if (isNaN(num)) return;
  switchTab('tab-dashboard');
  if (appState.carousel2D) appState.carousel2D.rotateToSlot(num);
  if (appState.carousel3D && appState.carousel3D !== appState.carousel2D) appState.carousel3D.rotateToSlot(num);
  window.scrollTo({ top: 0, behavior: 'smooth' });
  setTimeout(() => {
    const slotCard = document.querySelector(`.slot-card[data-slot="${num}"]`);
    if (slotCard) {
      slotCard.scrollIntoView({ behavior: 'smooth', block: 'center' });
      slotCard.style.outline = '3px solid var(--primary)';
      setTimeout(() => { slotCard.style.outline = ''; }, 2500);
    }
  }, 200);
  showToast(`🎯 Đang xoay mô hình 2D về Ngăn ${num} tại cửa mở 6 giờ`, 'info');
};

function shiftSelectedDate(days) {
  const cur = parseDateSafe(historyState.selectedDate) || new Date();
  cur.setDate(cur.getDate() + days);
  const nextDateStr = getLocalDateString(cur);
  const todayStr = getLocalDateString(new Date());
  if (nextDateStr > todayStr) return; // Do not go into the future
  loadHistoryData(nextDateStr);
}

function setupHistoryListeners() {
  document.getElementById('btn-refresh-history')?.addEventListener('click', () => {
    loadHistoryData(null, null, false);
    showToast('Đã làm mới dữ liệu cữ uống.', 'info');
  });

  document.getElementById('btn-export-history-csv')?.addEventListener('click', exportHistoryToCSV);

  // Single Day Navigation
  document.getElementById('btn-prev-day')?.addEventListener('click', () => shiftSelectedDate(-1));
  document.getElementById('btn-next-day')?.addEventListener('click', () => shiftSelectedDate(1));

  const datePicker = document.getElementById('history-date-picker');
  if (datePicker) {
    datePicker.value = historyState.selectedDate;
    datePicker.addEventListener('change', (e) => {
      if (e.target.value) loadHistoryData(e.target.value);
    });
  }

  document.getElementById('btn-quick-today')?.addEventListener('click', () => {
    loadHistoryData(getLocalDateString(new Date()));
  });

  document.getElementById('btn-quick-yesterday')?.addEventListener('click', () => {
    loadHistoryData(getLocalDateString(new Date(Date.now() - 86400000)));
  });

  // Search Input
  const searchInput = document.getElementById('history-search-input');
  const clearBtn = document.getElementById('btn-clear-search');
  if (searchInput) {
    searchInput.addEventListener('input', (e) => {
      historyState.searchTerm = e.target.value;
      if (clearBtn) clearBtn.style.display = e.target.value ? 'block' : 'none';
      applyHistoryFilterAndSearch();
    });
  }
  if (clearBtn) {
    clearBtn.addEventListener('click', () => {
      if (searchInput) searchInput.value = '';
      historyState.searchTerm = '';
      clearBtn.style.display = 'none';
      applyHistoryFilterAndSearch();
    });
  }

  // Dual View Mode Toggle
  const btnTable = document.getElementById('btn-view-table');
  const btnTimeline = document.getElementById('btn-view-timeline');
  const tableContainer = document.getElementById('history-table-container');
  const timelineContainer = document.getElementById('history-timeline-container');

  btnTable?.addEventListener('click', () => {
    btnTable.classList.add('active');
    btnTimeline?.classList.remove('active');
    if (tableContainer) tableContainer.style.display = 'block';
    if (timelineContainer) timelineContainer.style.display = 'none';
    historyState.viewMode = 'table';
    applyHistoryFilterAndSearch();
  });

  btnTimeline?.addEventListener('click', () => {
    btnTimeline.classList.add('active');
    btnTable?.classList.remove('active');
    if (tableContainer) tableContainer.style.display = 'none';
    if (timelineContainer) timelineContainer.style.display = 'block';
    historyState.viewMode = 'timeline';
    applyHistoryFilterAndSearch();
  });

  // 3-Status Filter Chips
  document.querySelectorAll('.filter-chip').forEach((chip) => {
    chip.addEventListener('click', () => {
      document.querySelectorAll('.filter-chip').forEach((c) => c.classList.remove('active'));
      chip.classList.add('active');
      const filter = chip.getAttribute('data-filter');
      historyState.filter = filter;
      loadHistoryData(null, filter);
    });
  });
}

// --- Inline Refill & Early Dispense Functions (Không làm mờ màn hình) ---
function setConsoleMode(mode, slotNumber = 1) {
  appState.consoleMode = mode;
  if (mode === 'REFILL') {
    appState.selectedRefillSlot = slotNumber;
    if (appState.carousel2D) {
      appState.carousel2D.rotateToSlot(slotNumber);
    }
  } else if (mode === 'IDLE') {
    if (appState.carousel2D) {
      appState.carousel2D.rotateToSlot(0);
      appState.carousel2D.setLEDState('green');
    }
  }
  renderConsolePanel();
}

async function openRefillModal(slotNumber = 1) {
  const targetSlot = parseInt(slotNumber, 10) || 1;
  if (targetSlot === 0) {
    await cancelInlineTask();
    return;
  }
  setConsoleMode('REFILL', targetSlot);

  // Kích hoạt xoay động cơ thực tế ESP32 đến đúng ngăn nạp thuốc
  if (window.api && typeof window.api.startRefill === 'function') {
    showToast(`🔄 Đang xoay khay thực tế đến Ngăn ${targetSlot} để nạp thuốc...`, 'info');
    try {
      await window.api.startRefill(targetSlot);
    } catch (err) {
      console.warn('Không thể gửi lệnh xoay khay thực tế:', err);
      showToast(`⚠️ Không thể kết nối xoay khay: ${err.message}`, 'warning');
    }
  }
}

function openEarlyDispenseModal() {
  setConsoleMode('EARLY_DISPENSE');
}

async function cancelInlineTask() {
  if (appState.consoleMode === 'REFILL') {
    // Hồi vị động cơ thực tế về Slot 0 Safe Home
    if (window.api && typeof window.api.cancelRefill === 'function') {
      try {
        await window.api.cancelRefill();
        showToast('🔒 Khay thuốc đã đóng về vị trí Safe an toàn.', 'info');
      } catch (err) {
        console.warn('Lỗi gửi lệnh hủy nạp:', err);
      }
    }
  }
  setConsoleMode('IDLE');
}

async function handleInlineSlotChange(slotValue) {
  const slotNum = parseInt(slotValue, 10);
  if (!slotNum || slotNum < 1 || slotNum > 7) return;

  appState.selectedRefillSlot = slotNum;

  // 1. Immediately rotate 2D carousel to slotNum at 6 o'clock!
  if (appState.carousel2D) {
    appState.carousel2D.rotateToSlot(slotNum);
  }

  // 2. Kích hoạt xoay động cơ thực tế ESP32 sang ngăn vừa chọn!
  if (window.api && typeof window.api.startRefill === 'function') {
    showToast(`🔄 Đang xoay khay thực tế sang Ngăn ${slotNum}...`, 'info');
    try {
      await window.api.startRefill(slotNum);
    } catch (err) {
      console.warn('Không thể gửi lệnh xoay khay thực tế:', err);
    }
  }

  // 3. Populate form fields
  const slot = appState.slots.find((s) => s.slot_number === slotNum);
  const nameInput = document.getElementById('inline-refill-med-name');
  const qtyInput = document.getElementById('inline-refill-qty');

  if (nameInput) nameInput.value = slot?.medicine?.name || '';
  if (qtyInput) qtyInput.value = slot?.current_quantity || 5;

  const doseInput = document.getElementById('inline-refill-dose');
  const doseBadge = document.getElementById('inline-refill-dose-badge');
  const currentDose = slot?.default_dose || 1;
  if (doseInput) doseInput.value = currentDose;
  if (doseBadge) doseBadge.textContent = `${currentDose} viên / cữ`;

  // 4. Update guidance pill
  const guidancePill = document.querySelector('#inline-refill-panel .inline-guidance-pill');
  const cfg = window.SMARTMED_SLOT_COLORS ? window.SMARTMED_SLOT_COLORS[slotNum] : null;
  const primaryColor = cfg?.primaryColor || '#2563eb';
  const colorName = cfg?.colorName ? cfg.colorName.split(' ')[0] : '';
  if (guidancePill) {
    guidancePill.style.borderLeft = `4px solid ${primaryColor}`;
    guidancePill.innerHTML = `
      <span>📍</span>
      <div>Mô hình & khay thực tế đã xoay <strong>Ngăn ${slotNum} (${colorName})</strong> ra cửa mở <strong>hướng 6 giờ</strong>.</div>
    `;
  }
}

function setInlineDose(val) {
  const doseInput = document.getElementById('inline-refill-dose');
  if (!doseInput) return;
  doseInput.value = val;
  const badge = document.getElementById('inline-refill-dose-badge');
  if (badge) badge.textContent = `${val} viên / cữ`;
}
window.setInlineDose = setInlineDose;

function setInlineQuickAmount(val) {
  const qtyInput = document.getElementById('inline-refill-qty');
  if (!qtyInput) return;
  let cur = parseInt(qtyInput.value, 10) || 0;
  qtyInput.value = Math.max(0, cur + val);
}

async function handleInlineRefillSubmit(e) {
  if (e) e.preventDefault();

  const slotSelect = document.getElementById('inline-refill-slot-select');
  const slotNum = slotSelect ? parseInt(slotSelect.value, 10) : appState.selectedRefillSlot || 1;
  const medName = document.getElementById('inline-refill-med-name')?.value.trim();
  const qty = parseInt(document.getElementById('inline-refill-qty')?.value, 10) || 5;
  const dose = parseInt(document.getElementById('inline-refill-dose')?.value, 10) || 1;

  if (!medName) {
    showToast('Vui lòng nhập tên thuốc!', 'error');
    return;
  }

  try {
    showToast(`Đang lưu thông tin nạp Ngăn ${slotNum}...`, 'info');

    // 1. Lưu vào Database
    await window.api.refillSlot(slotNum, {
      medicineName: medName,
      quantity: qty,
      dose: dose,
      capacity: Math.max(qty, 1000),
    });

    // 2. Thông báo ESP32 lưu NVS Flash và xoay động cơ về Slot 0 Safe Home
    if (window.api && typeof window.api.finishRefill === 'function') {
      await window.api.finishRefill(slotNum, {
        name: medName,
        qty: qty,
        dose: dose,
      }).catch((err) => console.warn('Lỗi gọi finishRefill:', err));
    }

    // 3. Tự động xoay mô hình 2D về vị trí Safe Home
    if (appState.carousel2D) {
      appState.carousel2D.rotateToSlot(0);
      appState.carousel2D.setLEDState('green');
    }

    showToast(`✅ Đã nạp ${qty} viên vào Ngăn ${slotNum} (Liều: ${dose} viên/lần)! Khay thuốc đã xoay về vị trí Safe an toàn 🔒`, 'success');

    // Switch console back to IDLE
    appState.consoleMode = 'IDLE';

    // Reload data & update dashboard
    await loadDashboardData();
  } catch (err) {
    showToast(err.message || 'Lỗi khi nạp thuốc vào ngăn', 'error');
  }
}

async function handleInlineEarlyDispenseSubmit(e) {
  if (e) e.preventDefault();

  const selected = Array.from(document.querySelectorAll('input[name="inline-early-slot"]:checked')).map((cb) =>
    parseInt(cb.value, 10)
  );

  if (selected.length === 0) {
    showToast('Vui lòng chọn ít nhất 1 ngăn thuốc!', 'error');
    return;
  }

  try {
    showToast('Đang kích hoạt cấp thuốc sớm...', 'info');
    await window.api.earlySessionDispense(selected);
    appState.consoleMode = 'IDLE';
    showToast('Đã bắt đầu phiên cấp thuốc sớm!', 'success');
    await loadDashboardData();
  } catch (err) {
    showToast(err.message || 'Lỗi khi kích hoạt cấp thuốc sớm', 'error');
  }
}

// --- Add Schedule Modal (24h Timepicker Giờ 00-23 & Ràng buộc chỉ chọn ngăn có thuốc) ---
function setupTimePicker24h() {
  const hourSelect = document.getElementById('new-schedule-hour');
  const minuteSelect = document.getElementById('new-schedule-minute');
  const timeHidden = document.getElementById('new-schedule-time');
  const displayPill = document.getElementById('time-24h-display-pill');

  if (!hourSelect || !minuteSelect) return;

  // Điền danh sách Giờ từ 00 đến 23
  if (hourSelect.options.length === 0) {
    const hours = [];
    for (let h = 0; h < 24; h++) {
      const hStr = h.toString().padStart(2, '0');
      let note = '';
      if (h >= 0 && h < 5) note = ' (Khuya)';
      else if (h >= 5 && h < 11) note = ' (Sáng)';
      else if (h >= 11 && h < 14) note = ' (Trưa)';
      else if (h >= 14 && h < 18) note = ' (Chiều)';
      else note = ' (Tối)';
      hours.push(`<option value="${hStr}">${hStr}:00${note}</option>`);
    }
    hourSelect.innerHTML = hours.join('');
    hourSelect.value = '08';
  }

  // Điền danh sách Phút từ 00 đến 59
  if (minuteSelect.options.length === 0) {
    const minutes = [];
    for (let m = 0; m < 60; m++) {
      const mStr = m.toString().padStart(2, '0');
      minutes.push(`<option value="${mStr}">${mStr} phút</option>`);
    }
    minuteSelect.innerHTML = minutes.join('');
    minuteSelect.value = '00';
  }

  const syncTime = () => {
    const h = hourSelect.value || '08';
    const m = minuteSelect.value || '00';
    const timeStr = `${h}:${m}`;
    if (timeHidden) timeHidden.value = timeStr;
    if (displayPill) displayPill.textContent = timeStr;
    updateTimeSessionPreview(timeStr);
  };

  hourSelect.onchange = syncTime;
  minuteSelect.onchange = syncTime;
}

function openAddScheduleModal() {
  if (!appState.device.is_online) {
    showToast('Hộp thuốc đang ngoại tuyến (Offline)! Bạn không thể cài đặt lịch lúc này.', 'warning');
    return;
  }

  const modal = document.getElementById('modal-add-schedule');
  if (!modal) return;

  setupTimePicker24h();

  const hourSelect = document.getElementById('new-schedule-hour');
  const minuteSelect = document.getElementById('new-schedule-minute');
  const timeHidden = document.getElementById('new-schedule-time');
  const displayPill = document.getElementById('time-24h-display-pill');

  if (hourSelect) hourSelect.value = '08';
  if (minuteSelect) minuteSelect.value = '00';
  if (timeHidden) timeHidden.value = '08:00';
  if (displayPill) displayPill.textContent = '08:00';
  updateTimeSessionPreview('08:00');

  // Lọc danh sách ngăn 1..7 có thuốc (current_quantity > 0)
  const availableSlots = appState.slots.filter(
    (s) => s.slot_number >= 1 && s.slot_number <= 7 && s.current_quantity > 0 && s.medicine
  );

  const alertNoMeds = document.getElementById('alert-no-meds-available');
  const submitBtn = document.getElementById('btn-submit-schedule');
  const container = document.getElementById('schedule-slots-selector-container');
  const countBadge = document.getElementById('schedule-selected-count-badge');

  if (availableSlots.length === 0) {
    if (alertNoMeds) alertNoMeds.style.display = 'block';
    if (container) container.innerHTML = '<p style="font-size:0.85rem; color:var(--text-muted); font-style:italic; padding:0.5rem 0;">Không có ngăn nào có thuốc để đặt lịch.</p>';
    if (submitBtn) submitBtn.disabled = true;
    if (countBadge) countBadge.textContent = '0 ngăn đã chọn';
  } else {
    if (alertNoMeds) alertNoMeds.style.display = 'none';
    if (submitBtn) submitBtn.disabled = false;

    // Render danh sách chọn ngăn tương ứng
    if (container) {
      container.innerHTML = availableSlots
        .map((s, idx) => {
          const cfg = window.SMARTMED_SLOT_COLORS ? window.SMARTMED_SLOT_COLORS[s.slot_number] : null;
          const isDefaultChecked = idx === 0; // Mặc định chọn ngăn đầu tiên
          const colorHex = cfg ? cfg.color : '#2563eb';
          const icon = cfg ? cfg.icon : '💊';

          return `
            <div class="slot-picker-row ${isDefaultChecked ? 'selected' : ''}" id="row-slot-${s.slot_number}" data-slot="${s.slot_number}">
              <div class="slot-picker-info">
                <input type="checkbox" class="slot-picker-checkbox" id="chk-slot-${s.slot_number}" data-slot="${s.slot_number}" ${isDefaultChecked ? 'checked' : ''}>
                <span class="slot-picker-badge" style="background: ${colorHex}18; color: ${colorHex}; border: 1px solid ${colorHex}40;">
                  <span>${icon}</span> Ngăn ${s.slot_number}
                </span>
                <div class="slot-picker-med">
                  <span class="slot-picker-med-name">${s.medicine.name}</span>
                  <span class="slot-picker-med-sub">Còn <strong>${s.current_quantity}</strong> viên trong khay</span>
                </div>
              </div>
              <div class="slot-picker-dose-control" onclick="event.stopPropagation()">
                <span style="font-size:0.75rem; color:var(--text-muted); font-weight:600;">Liều:</span>
                <input type="number" class="slot-picker-dose-input tabular-nums" id="dose-slot-${s.slot_number}" min="1" value="${s.default_dose || 1}" ${isDefaultChecked ? '' : 'disabled'}>
                <span style="font-size:0.75rem; color:var(--text-muted);">viên</span>
              </div>
            </div>
          `;
        })
        .join('');

      // Gắn sự kiện cho từng dòng ngăn và ô nhập liều
      availableSlots.forEach((s) => {
        const row = document.getElementById(`row-slot-${s.slot_number}`);
        const chk = document.getElementById(`chk-slot-${s.slot_number}`);
        const doseInput = document.getElementById(`dose-slot-${s.slot_number}`);

        const updateRowState = (checked) => {
          if (chk) chk.checked = checked;
          if (row) {
            if (checked) row.classList.add('selected');
            else row.classList.remove('selected');
          }
          if (doseInput) doseInput.disabled = !checked;
          updateSelectedSlotsCountBadge();
        };

        if (row) {
          row.onclick = (e) => {
            if (e.target.tagName.toLowerCase() === 'input') return;
            const newChecked = !chk.checked;
            updateRowState(newChecked);
          };
        }

        if (chk) {
          chk.onchange = () => {
            updateRowState(chk.checked);
          };
        }
      });
    }

    // Nút Chọn tất cả và Bỏ chọn
    const btnSelectAll = document.getElementById('btn-select-all-slots');
    const btnDeselectAll = document.getElementById('btn-deselect-all-slots');

    if (btnSelectAll) {
      btnSelectAll.onclick = () => {
        availableSlots.forEach((s) => {
          const chk = document.getElementById(`chk-slot-${s.slot_number}`);
          const row = document.getElementById(`row-slot-${s.slot_number}`);
          const doseInput = document.getElementById(`dose-slot-${s.slot_number}`);
          if (chk) chk.checked = true;
          if (row) row.classList.add('selected');
          if (doseInput) doseInput.disabled = false;
        });
        updateSelectedSlotsCountBadge();
      };
    }

    if (btnDeselectAll) {
      btnDeselectAll.onclick = () => {
        availableSlots.forEach((s) => {
          const chk = document.getElementById(`chk-slot-${s.slot_number}`);
          const row = document.getElementById(`row-slot-${s.slot_number}`);
          const doseInput = document.getElementById(`dose-slot-${s.slot_number}`);
          if (chk) chk.checked = false;
          if (row) row.classList.remove('selected');
          if (doseInput) doseInput.disabled = true;
        });
        updateSelectedSlotsCountBadge();
      };
    }

    updateSelectedSlotsCountBadge();
  }

  modal.classList.add('active');
}

function updateSelectedSlotsCountBadge() {
  const checkboxes = document.querySelectorAll('.slot-picker-checkbox:checked');
  const countBadge = document.getElementById('schedule-selected-count-badge');
  if (countBadge) {
    countBadge.textContent = `${checkboxes.length} ngăn đã chọn`;
    countBadge.style.color = checkboxes.length > 0 ? 'var(--primary)' : 'var(--text-muted)';
  }
}

function updateTimeSessionPreview(timeStr) {
  const previewBadge = document.getElementById('time-session-preview-badge');
  if (!previewBadge || !timeStr) return;

  const [h, m] = timeStr.split(':').map(Number);
  const minutes = h * 60 + m;

  if (minutes >= 5 * 60 && minutes <= 10 * 60 + 59) {
    previewBadge.innerHTML = '🌅 Buổi Sáng (05:00 - 10:59)';
    previewBadge.className = 'session-badge morning';
  } else if (minutes >= 11 * 60 && minutes <= 13 * 60 + 59) {
    previewBadge.innerHTML = '☀️ Buổi Trưa (11:00 - 13:59)';
    previewBadge.className = 'session-badge noon';
  } else if (minutes >= 14 * 60 && minutes <= 17 * 60 + 59) {
    previewBadge.innerHTML = '🌇 Buổi Chiều (14:00 - 17:59)';
    previewBadge.className = 'session-badge afternoon';
  } else {
    previewBadge.innerHTML = '🌙 Buổi Tối (18:00 - 04:59)';
    previewBadge.className = 'session-badge evening';
  }
}

async function handleAddScheduleSubmit(e) {
  e.preventDefault();

  if (!appState.device.is_online) {
    showToast('Hộp thuốc đang ngoại tuyến (Offline)! Không thể cài đặt lịch.', 'error');
    return;
  }

  const timeVal = document.getElementById('new-schedule-time').value || '08:00';
  const checkedBoxes = Array.from(document.querySelectorAll('.slot-picker-checkbox:checked'));

  if (checkedBoxes.length === 0) {
    showToast('Vui lòng tích chọn ít nhất 1 ngăn thuốc tương ứng với cữ giờ này!', 'error');
    return;
  }

  const selectedSlots = checkedBoxes.map((chk) => {
    const slotNumber = parseInt(chk.getAttribute('data-slot'), 10);
    const doseInput = document.getElementById(`dose-slot-${slotNumber}`);
    const doseQuantity = parseInt(doseInput?.value, 10) || 1;
    return {
      slotNumber,
      doseQuantity,
    };
  });

  const submitBtn = document.getElementById('btn-submit-schedule');
  const originalText = submitBtn ? submitBtn.innerHTML : '';
  if (submitBtn) {
    submitBtn.disabled = true;
    submitBtn.innerHTML = '⏳ Đang lưu & Đồng bộ...';
  }

  try {
    await window.api.createScheduleBatch({
      time: timeVal,
      slots: selectedSlots,
    });

    document.getElementById('modal-add-schedule')?.classList.remove('active');
    const slotListStr = selectedSlots.map((s) => `Ngăn ${s.slotNumber}`).join(', ');
    showToast(`Đã lưu cữ ${timeVal} (${slotListStr}) & Đồng bộ ESP32 thành công!`, 'success');
    await loadDashboardData();
  } catch (err) {
    showToast(err.message || 'Lỗi khi tạo và đồng bộ cữ uống', 'error');
  } finally {
    if (submitBtn) {
      submitBtn.disabled = false;
      submitBtn.innerHTML = originalText;
    }
  }
}



// --- WebSocket Event Handlers ---
function setupWebSocket() {
  window.wsClient.onStatusChange((status) => {
    if (status === 'online') {
      loadDashboardData();
    }
    updateHeaderStatus();
  });

  window.wsClient.on('device_online', (data) => {
    console.log('[WS] device_online:', data);
    appState.device.is_online = true;
    if (data) Object.assign(appState.device, data);
    updateHeaderStatus();
    updateTelemetryView(appState.device);
    if (appState.activeTab === 'tab-schedules') renderSchedulesView();
    showToast('Hộp thuốc ESP32 đã trực tuyến!', 'success');
  });

  window.wsClient.on('device_offline', (data) => {
    console.warn('[WS] device_offline:', data);
    appState.device.is_online = false;
    if (data) Object.assign(appState.device, data);
    updateHeaderStatus();
    if (appState.activeTab === 'tab-schedules') renderSchedulesView();
    showToast('Hộp thuốc ESP32 đã ngắt kết nối!', 'warning');
  });

  window.wsClient.on('slot_reached', (data) => {
    console.log('[WS] slot_reached:', data);
    const slot = data.slot !== undefined ? data.slot : data.slot_number;
    if (appState.carousel3D) {
      appState.carousel3D.rotateToSlot(slot);
      appState.carousel3D.setLEDState('blue');
    }
    appState.device.current_slot = slot;
    if (appState.consoleMode === 'REFILL') {
      appState.device.fsm_state = 'REFILL_MODE';
    } else {
      appState.device.fsm_state = 'WAITING_CONFIRM';
    }
    renderStatsCards();
    if (appState.consoleMode !== 'REFILL' && appState.consoleMode !== 'EARLY_DISPENSE') {
      renderConsolePanel();
    }
  });

  window.wsClient.on('home_reached', () => {
    console.log('[WS] home_reached');
    // Chỉ tự động xoay về 0 nếu người dùng KHÔNG đang mở chế độ nạp thuốc
    if (appState.consoleMode !== 'REFILL' && appState.carousel3D) {
      appState.carousel3D.rotateToSlot(0);
      appState.carousel3D.setLEDState('green');
    }
    appState.device.current_slot = 0;
    appState.device.fsm_state = 'SAFE_HOME_IDLE';
    renderStatsCards();
    if (appState.consoleMode !== 'REFILL' && appState.consoleMode !== 'EARLY_DISPENSE') {
      renderConsolePanel();
    }
  });

  window.wsClient.on('refill_ready', (data) => {
    console.log('[WS] refill_ready:', data);
    const slot = data.slot !== undefined ? data.slot : (appState.selectedRefillSlot || 1);
    appState.device.current_slot = slot;
    appState.device.fsm_state = 'REFILL_MODE';
    if (appState.carousel3D) {
      appState.carousel3D.rotateToSlot(slot);
    }
    showToast(`✅ Động cơ đã xoay mở Ngăn ${slot} sẵn sàng nạp thuốc!`, 'success');
    renderStatsCards();
    // TUYỆT ĐỐI KHÔNG gọi renderConsolePanel() ở đây vì form nạp thuốc đã hiển thị,
    // gọi lại sẽ làm reset dữ liệu đang gõ của người dùng!
  });

  window.wsClient.on('refill_done', (data) => {
    console.log('[WS] refill_done:', data);
    const slot = data.slot !== undefined ? data.slot : (data.slot_number || '?');
    const qty = data.qty !== undefined ? data.qty : '';
    const added = data.added ? ` (+${data.added} viên)` : '';
    showToast(`✅ Đã đồng bộ số lượng Ngăn ${slot}: ${qty} viên${added}!`, 'success');
    if (appState.consoleMode === 'REFILL') {
      appState.consoleMode = 'IDLE';
    }
    loadDashboardData();
  });

  window.wsClient.on('slot_updated', (data) => {
    console.log('[WS] slot_updated:', data);
    loadDashboardData();
  });

  window.wsClient.on('slot_cleared', (data) => {
    console.log('[WS] slot_cleared:', data);
    const slot = data.slot_number || data.slot || '?';
    showToast(`🗑️ Ngăn ${slot} đã được xóa và đồng bộ lịch an toàn!`, 'info');
    loadDashboardData();
  });

  window.wsClient.on('dispense_started', (data) => {
    console.log('[WS] dispense_started:', data);
    showToast(`Đang cấp thuốc Ngăn ${data.slot_number}...`, 'info');
    loadDashboardData();
  });

  window.wsClient.on('taken', () => {
    console.log('[WS] taken');
    showToast('Người dùng đã lấy thuốc!', 'success');
    loadDashboardData();
  });

  window.wsClient.on('missed', () => {
    console.log('[WS] missed');
    showToast('Cữ uống bị bỏ lỡ!', 'error');
    loadDashboardData();
  });

  window.wsClient.on('jammed', () => {
    console.error('[WS] jammed');
    showToast('CẢNH BÁO: Kẹt cơ hộp thuốc!', 'error');
    if (appState.carousel3D) appState.carousel3D.setLEDState('red');
    loadDashboardData();
  });

  window.wsClient.on('telemetry', (data) => {
    appState.device.is_online = true;
    if (data.wifi_rssi !== undefined) appState.device.wifi_rssi = data.wifi_rssi;
    if (data.rssi !== undefined) appState.device.rssi = data.rssi;
    if (data.temperature !== undefined) appState.device.temperature = data.temperature;
    if (data.humidity !== undefined) appState.device.humidity = data.humidity;
    if (data.free_heap !== undefined) appState.device.free_heap = data.free_heap;
    if (data.uptime !== undefined) appState.device.uptime = data.uptime;
    if (data.fsm_state) appState.device.fsm_state = data.fsm_state;
    if (data.current_slot !== undefined) appState.device.current_slot = data.current_slot;

    updateHeaderStatus();
    updateTelemetryView(data);
  });

  window.wsClient.on('schedule_updated', (data) => {
    console.log('[WS] schedule_updated:', data);
    const slotInfo = data && data.slot_number ? `Ngăn ${data.slot_number}` : '';
    const timeInfo = data && data.time_of_day ? `lúc ${data.time_of_day}` : '';
    showToast(`Đã nhận lịch mới từ thiết bị (EC11): ${slotInfo} ${timeInfo}`, 'info');
    loadDashboardData();
  });

  window.wsClient.on('dose_due', (data) => {
    console.log('[WS] dose_due event:', data);
    const med = data.medicine_name || data.label || 'Vui lòng kiểm tra khay';
    showToast(`🔔 ĐÃ ĐẾN CỮ UỐNG THUỐC: ${med}`, 'warning');
    if (appState.carousel3D) appState.carousel3D.setLEDState('amber');
    appState.device.fsm_state = 'ALERTING_DUE';
    renderStatsCards();
    renderConsolePanel();
    loadDashboardData();
  });
}

// --- Real-time Digital Clock ---
function startClock() {
  const clockEl = document.getElementById('header-live-clock');
  const update = () => {
    if (clockEl) {
      const now = new Date();
      clockEl.textContent = now.toLocaleTimeString('vi-VN', { hour12: false });
    }
  };
  update();
  setInterval(update, 1000);
}

// --- Toast System ---
function showToast(message, type = 'info') {
  const container = document.getElementById('toast-container');
  if (!container) return;

  const toast = document.createElement('div');
  toast.className = `toast ${type}`;

  const icon = type === 'success' ? '✅' : type === 'error' ? '❌' : 'ℹ️';
  toast.innerHTML = `<span>${icon}</span> <span>${message}</span>`;

  container.appendChild(toast);

  setTimeout(() => {
    toast.style.animation = 'toastIn 0.3s ease reverse forwards';
    setTimeout(() => toast.remove(), 300);
  }, 4000);
}

// Make functions globally available for onclick / onchange / onsubmit attributes
window.appState = appState;
window.setConsoleMode = setConsoleMode;
window.openRefillModal = openRefillModal;
window.openEarlyDispenseModal = openEarlyDispenseModal;
window.cancelInlineTask = cancelInlineTask;
window.handleInlineSlotChange = handleInlineSlotChange;
window.setInlineQuickAmount = setInlineQuickAmount;
window.setInlineDose = setInlineDose;
window.handleInlineRefillSubmit = handleInlineRefillSubmit;
window.handleInlineEarlyDispenseSubmit = handleInlineEarlyDispenseSubmit;
window.handleConfirmDose = handleConfirmDose;
window.handleConfirmTaken = handleConfirmTaken;
window.handleEmergencyHome = handleEmergencyHome;
window.toggleScheduleActive = toggleScheduleActive;
window.deleteScheduleItem = deleteScheduleItem;
window.testMoveMotor = testMoveMotor;
