/**
 * ws.js — SmartMed Native WebSocket Client
 * Real-time event synchronization with auto-reconnect & event emitter
 */

class SmartMedWS {
  constructor() {
    this.ws = null;
    this.reconnectAttempts = 0;
    this.maxReconnectDelay = 10000;
    this.listeners = new Map();
    this.statusListeners = new Set();
    this.isConnected = false;
    this.heartbeatTimer = null;
  }

  connect() {
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const host = window.location.host || '127.0.0.1:8000';
    const wsUrl = `${protocol}//${host}/ws`;

    console.log(`[WebSocket] Đang kết nối tới ${wsUrl}...`);
    this._notifyStatus('reconnecting');

    try {
      this.ws = new WebSocket(wsUrl);

      this.ws.onopen = () => {
        console.log('[WebSocket] Đã kết nối thành công.');
        this.isConnected = true;
        this.reconnectAttempts = 0;
        this._notifyStatus('online');
        this._startHeartbeat();
      };

      this.ws.onmessage = (event) => {
        try {
          const message = JSON.parse(event.data);
          this._handleMessage(message);
        } catch (e) {
          console.warn('[WebSocket] Nhận tin nhắn không hợp lệ:', event.data);
        }
      };

      this.ws.onclose = (event) => {
        console.warn(`[WebSocket] Mất kết nối (code: ${event.code}). Chuẩn bị kết nối lại...`);
        this.isConnected = false;
        this._stopHeartbeat();
        this._notifyStatus('offline');
        this._scheduleReconnect();
      };

      this.ws.onerror = (err) => {
        console.error('[WebSocket] Lỗi kết nối:', err);
        this.ws.close();
      };
    } catch (err) {
      console.error('[WebSocket] Lỗi khởi tạo socket:', err);
      this._scheduleReconnect();
    }
  }

  _scheduleReconnect() {
    this.reconnectAttempts++;
    const delay = Math.min(1000 * Math.pow(1.5, this.reconnectAttempts), this.maxReconnectDelay);
    console.log(`[WebSocket] Thử kết nối lại sau ${Math.round(delay)}ms (Lần ${this.reconnectAttempts})...`);
    this._notifyStatus('reconnecting');
    setTimeout(() => this.connect(), delay);
  }

  _startHeartbeat() {
    this._stopHeartbeat();
    this.heartbeatTimer = setInterval(() => {
      if (this.ws && this.ws.readyState === WebSocket.OPEN) {
        this.ws.send(JSON.stringify({ type: 'ping' }));
      }
    }, 25000);
  }

  _stopHeartbeat() {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
  }

  _handleMessage(msg) {
    const { type, data, ts } = msg;

    if (type === 'ping') {
      this.ws.send(JSON.stringify({ type: 'pong' }));
      return;
    }

    // Trigger all specific listeners
    if (this.listeners.has(type)) {
      this.listeners.get(type).forEach((fn) => {
        try {
          fn(data, ts);
        } catch (e) {
          console.error(`[WebSocket] Lỗi xử lý callback cho event '${type}':`, e);
        }
      });
    }

    // Trigger wildcard listeners
    if (this.listeners.has('*')) {
      this.listeners.get('*').forEach((fn) => fn(type, data, ts));
    }
  }

  on(type, callback) {
    if (!this.listeners.has(type)) {
      this.listeners.set(type, new Set());
    }
    this.listeners.get(type).add(callback);
    return () => this.off(type, callback);
  }

  off(type, callback) {
    if (this.listeners.has(type)) {
      this.listeners.get(type).delete(callback);
    }
  }

  onStatusChange(callback) {
    this.statusListeners.add(callback);
    return () => this.statusListeners.delete(callback);
  }

  _notifyStatus(status) {
    this.statusListeners.forEach((fn) => fn(status));
  }
}

// Export singleton instance to window
window.wsClient = new SmartMedWS();
