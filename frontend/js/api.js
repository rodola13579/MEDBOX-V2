/**
 * api.js — SmartMed REST API Client
 * Pure Vanilla JavaScript (Fetch API) wrapper for FastAPI endpoints
 */

const API_BASE = (window.location.protocol === 'file:' || !window.location.origin || window.location.origin === 'null')
  ? 'http://127.0.0.1:8000/api/v1'
  : '/api/v1';

class SmartMedAPI {
  async _fetch(endpoint, options = {}) {
    const defaultHeaders = {
      'Content-Type': 'application/json',
      'Accept': 'application/json',
    };

    const config = {
      ...options,
      headers: {
        ...defaultHeaders,
        ...options.headers,
      },
    };

    if (config.body && typeof config.body === 'object') {
      config.body = JSON.stringify(config.body);
    }

    try {
      const response = await fetch(`${API_BASE}${endpoint}`, config);
      const data = await response.json().catch(() => null);

      if (!response.ok) {
        const errorDetail = data?.detail || response.statusText || 'Lỗi kết nối máy chủ';
        const err = new Error(typeof errorDetail === 'string' ? errorDetail : JSON.stringify(errorDetail));
        err.status = response.status;
        err.data = data;
        throw err;
      }
      return data;
    } catch (err) {
      console.error(`[API Error] ${options.method || 'GET'} ${endpoint}:`, err);
      throw err;
    }
  }

  // --- Device & Dashboard ---
  async getDashboard() {
    return this._fetch('/device/dashboard');
  }

  async getDeviceStatus() {
    return this._fetch('/device/status');
  }

  async moveMotor(slot) {
    return this._fetch(`/device/move/${slot}`, { method: 'POST' });
  }

  async homing() {
    return this._fetch('/device/homing', { method: 'POST' });
  }

  // --- Dispensing ---
  async manualDispense(slotNumber, scheduleId = null) {
    return this._fetch('/dispense', {
      method: 'POST',
      body: { slot_number: slotNumber, schedule_id: scheduleId },
    });
  }

  async earlySessionDispense(slots) {
    return this._fetch('/dispense/early-session', {
      method: 'POST',
      body: { slots: slots },
    });
  }

  async confirmTaken() {
    return this._fetch('/dispense/confirm', { method: 'POST' });
  }

  async confirmDose() {
    return this._fetch('/dispense/confirm-dose', { method: 'POST' });
  }

  async returnHome() {
    return this._fetch('/dispense/home', { method: 'POST' });
  }

  async getActiveSession() {
    return this._fetch('/dispense/active');
  }

  async getHistory(limit = 50, status = null, date = null) {
    let url = `/dispense/history?limit=${limit}`;
    if (status) url += `&status=${encodeURIComponent(status)}`;
    if (date) url += `&date=${encodeURIComponent(date)}`;
    return this._fetch(url);
  }

  // --- Slots & Refill ---
  async getSlots() {
    return this._fetch('/slots');
  }

  async updateSlot(slotNumber, payload) {
    return this._fetch(`/slots/${slotNumber}`, {
      method: 'PUT',
      body: payload,
    });
  }

  // --- Medicines ---
  async getMedicines() {
    return this._fetch('/medicines');
  }

  async createMedicine(name, unit = 'viên') {
    return this._fetch('/medicines', {
      method: 'POST',
      body: { name, dosage_unit: unit },
    });
  }

  /**
   * Refill Flow: Gán thuốc & cập nhật số lượng trực tiếp vào Ngăn k
   */
  async refillSlot(slotNumber, { medicineName, quantity, dose = 1, capacity = null }) {
    if (slotNumber < 1 || slotNumber > 7) {
      throw new Error('Chỉ có thể nạp thuốc vào ngăn 1 đến 7 (Ngăn 0 là Safe Home).');
    }

    let targetMedId = null;

    if (medicineName && medicineName.trim()) {
      const cleanName = medicineName.trim();
      const allMeds = await this.getMedicines().catch(() => []);
      const existing = allMeds.find(m => m.name.toLowerCase() === cleanName.toLowerCase());

      if (existing) {
        targetMedId = existing.id;
      } else {
        const newMed = await this.createMedicine(cleanName, 'viên');
        targetMedId = newMed.id;
      }
    }

    const q = parseInt(quantity, 10) || 0;
    const cap = capacity !== null && capacity !== undefined ? parseInt(capacity, 10) : Math.max(q, 1000);

    const updatePayload = {
      current_quantity: q,
      default_dose: parseInt(dose, 10) || 1,
      capacity: Math.max(cap, q),
      is_active: true,
    };

    if (targetMedId !== null) {
      updatePayload.medicine_id = targetMedId;
    }

    return this.updateSlot(slotNumber, updatePayload);
  }

  /**
   * Kích hoạt xoay động cơ thực tế đến ngăn chỉ định để nạp thuốc
   */
  async startRefill(slotNumber) {
    return this._fetch(`/device/refill/start/${slotNumber}`, { method: 'POST' });
  }

  /**
   * Hủy nạp thuốc — Xoay động cơ thực tế trở về Safe Home (Slot 0)
   */
  async cancelRefill() {
    return this._fetch('/device/refill/cancel', { method: 'POST' });
  }

  /**
   * Hoàn tất nạp thuốc — Cập nhật Flash NVS ESP32 và tự động khóa về Safe Home
   */
  async finishRefill(slotNumber, { name, qty, dose = 1 }) {
    return this._fetch('/device/refill/done', {
      method: 'POST',
      body: { slot: slotNumber, name, qty, dose },
    });
  }

  // --- Schedules ---
  async getSchedules() {
    return this._fetch('/schedules');
  }

  async createSchedule({ slotNumber, time, pillCount = 1, daysOfWeek = [0, 1, 2, 3, 4, 5, 6] }) {
    return this._fetch('/schedules', {
      method: 'POST',
      body: {
        slot_number: slotNumber,
        time_of_day: time,
        time: time,
        dose_quantity: pillCount,
        pill_count: pillCount,
        days_of_week: daysOfWeek,
        is_active: true,
      },
    });
  }

  async createScheduleBatch({ time, slots }) {
    return this._fetch('/schedules/batch', {
      method: 'POST',
      body: {
        time_of_day: time,
        slots: slots.map((s) => ({
          slot_number: parseInt(s.slotNumber !== undefined ? s.slotNumber : s.slot_number, 10),
          dose_quantity: parseInt(s.doseQuantity !== undefined ? s.doseQuantity : (s.dose_quantity || s.pillCount || 1), 10),
        })),
      },
    });
  }

  async deleteSchedule(id) {
    return this._fetch(`/schedules/${id}`, { method: 'DELETE' });
  }

  async toggleSchedule(id) {
    return this._fetch(`/schedules/${id}/toggle`, { method: 'PATCH' });
  }

  async syncSchedules() {
    return this._fetch('/schedules/sync', { method: 'POST' });
  }
}

// Export singleton instance to window
window.api = new SmartMedAPI();
