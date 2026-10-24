"""
tests/mock_esp32_mqtt.py
─────────────────────────
Script giả lập ESP32 — dùng để kiểm thử MQTT integration không cần phần cứng thật.

Chạy script này TRƯỚC KHI khởi động backend server:
  python tests/mock_esp32_mqtt.py

Hoặc chạy song song với server:
  Terminal 1: uvicorn main:app --port 8000
  Terminal 2: python tests/mock_esp32_mqtt.py

YÊU CẦU: Mosquitto MQTT Broker phải đang chạy tại localhost:1883.
  Cài Mosquitto: https://mosquitto.org/download/
  Khởi động:    mosquitto -v

Hành vi mock:
  1. Kết nối Broker, gửi STATUS ONLINE
  2. Gửi TELEMETRY mỗi 10 giây
  3. Khi nhận DISPENSE_NOW → đợi 1.5s → gửi SLOT_REACHED
  4. Khi nhận RETURN_HOME_ZERO → đợi 1.5s → gửi HOME_REACHED
  5. Khi nhận CALIBRATE → đợi 3s (rà TCRT5000) → gửi HOME_REACHED
  6. Khi nhận MOVE → đợi 1s → gửi SLOT_REACHED

Dùng paho-mqtt (đã được cài qua aiomqtt's dependency).
"""

import json
import sys
import time
import threading
import logging
from datetime import datetime, timezone

import paho.mqtt.client as mqtt

# ── Cấu hình ─────────────────────────────────────────────────────────────────
BROKER_HOST  = "localhost"
BROKER_PORT  = 1883
DEVICE_ID    = "ESP32_PILLBOX_01"
CLIENT_ID    = "mock_esp32_simulator"

# Topics
CMD_TOPIC    = f"pillbox/{DEVICE_ID}/cmd/#"         # Subscribe nhận lệnh
EVENT_PREFIX = f"pillbox/{DEVICE_ID}/events"         # Prefix gửi event
LWT_TOPIC    = f"pillbox/{DEVICE_ID}/lwt"
STATUS_TOPIC = f"pillbox/{DEVICE_ID}/state/status"
TELEMETRY_TOPIC = f"pillbox/{DEVICE_ID}/state/telemetry"


# Trạng thái nội bộ mock
current_slot = 0
fsm_state    = "SAFE_HOME_IDLE"

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s | %(levelname)-7s | MOCK_ESP32 | %(message)s",
    datefmt="%H:%M:%S"
)
logger = logging.getLogger("mock_esp32")


# ── Helper: build payload ─────────────────────────────────────────────────────
def now_iso() -> str:
    return datetime.now(timezone.utc).isoformat()


def publish_event(client: mqtt.Client, event_type: str, extra: dict = {}) -> None:
    topic = f"{EVENT_PREFIX}/{event_type.lower()}"
    payload = {"event": event_type, "timestamp": now_iso(), **extra}
    client.publish(topic, json.dumps(payload), qos=1)
    logger.info(f"← EVENT  [{event_type}] {extra}")


# ── Callback: kết nối thành công ─────────────────────────────────────────────
def on_connect(client: mqtt.Client, userdata, flags, rc, properties=None):
    global fsm_state
    if rc == 0:
        logger.info(f"✅ Kết nối Broker thành công ({BROKER_HOST}:{BROKER_PORT})")
        # Subscribe nhận lệnh từ backend
        client.subscribe(CMD_TOPIC, qos=1)
        logger.info(f"   Subscribe: {CMD_TOPIC}")

        # Báo online
        fsm_state = "SAFE_HOME_IDLE"
        payload = json.dumps({
            "event": "STATUS",
            "status": "ONLINE",
            "state": fsm_state,
            "current_slot": current_slot,
            "timestamp": now_iso()
        })
        client.publish(STATUS_TOPIC, payload, qos=1, retain=True)
        logger.info("← STATUS ONLINE gửi lên backend")
    else:
        logger.error(f"❌ Kết nối thất bại: rc={rc}")


# ── Callback: mất kết nối ────────────────────────────────────────────────────
def on_disconnect(client, userdata, disconnect_flags, reason_code=None, properties=None):
    logger.warning(f"⚠️  Mất kết nối Broker (rc={reason_code})")


# ── Callback: nhận lệnh từ backend ──────────────────────────────────────────
def on_message(client: mqtt.Client, userdata, msg: mqtt.MQTTMessage):
    global current_slot, fsm_state

    topic = msg.topic
    try:
        data = json.loads(msg.payload.decode("utf-8"))
    except Exception:
        logger.warning(f"Payload không hợp lệ từ {topic}")
        return

    cmd = data.get("cmd", "")
    logger.info(f"→ CMD    [{cmd}] {json.dumps(data)[:100]}")

    if cmd == "DISPENSE_NOW":
        target_slot = data.get("target_slot", 1)
        def _respond():
            time.sleep(1.5)  # Giả lập thời gian xoay motor
            current_slot = target_slot
            fsm_state = "WAITING_CONFIRM"
            publish_event(client, "SLOT_REACHED", {"slot": target_slot})
        threading.Thread(target=_respond, daemon=True).start()

    elif cmd == "RETURN_HOME_ZERO":
        def _respond():
            time.sleep(1.5)  # Giả lập thời gian hồi vị
            current_slot = 0
            fsm_state = "SAFE_HOME_IDLE"
            publish_event(client, "HOME_REACHED", {"slot": 0})
        threading.Thread(target=_respond, daemon=True).start()

    elif cmd == "CALIBRATE":
        def _respond():
            logger.info("   Đang rà TCRT5000 tìm vạch phản xạ...")
            time.sleep(3.0)  # Giả lập thời gian homing chậm hơn
            current_slot = 0
            fsm_state = "SAFE_HOME_IDLE"
            publish_event(client, "HOME_REACHED", {"slot": 0, "method": "TCRT5000"})
        threading.Thread(target=_respond, daemon=True).start()

    elif cmd == "MOVE":
        target_slot = data.get("target_slot", 0)
        def _respond():
            time.sleep(1.0)
            current_slot = target_slot
            publish_event(client, "SLOT_REACHED", {"slot": target_slot})
        threading.Thread(target=_respond, daemon=True).start()

    else:
        logger.warning(f"Lệnh không xác định: {cmd}")


# ── Gửi TELEMETRY định kỳ ────────────────────────────────────────────────────
def telemetry_loop(client: mqtt.Client):
    uptime = 0
    while True:
        time.sleep(10)
        uptime += 10
        payload = json.dumps({
            "event": "TELEMETRY",
            "rssi": -62,
            "free_heap": 245760,
            "ip": "192.168.1.100",
            "firmware": "1.0.0-mock",
            "temperature": 28.5,
            "humidity": 65.0,
            "uptime": uptime,
            "timestamp": now_iso()
        })
        client.publish(TELEMETRY_TOPIC, payload, qos=0)
        logger.debug(f"Telemetry sent (uptime={uptime}s, temp=28.5C, hum=65%)")


# ── Main ──────────────────────────────────────────────────────────────────────
def main():
    logger.info("=" * 55)
    logger.info("  MOCK ESP32 Simulator — Smart Pillbox")
    logger.info(f"  Device ID : {DEVICE_ID}")
    logger.info(f"  Broker    : {BROKER_HOST}:{BROKER_PORT}")
    logger.info("=" * 55)

    # LWT — gửi OFFLINE khi script bị tắt đột ngột
    lwt_payload = json.dumps({"status": "OFFLINE", "timestamp": now_iso()})

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=CLIENT_ID)
    client.will_set(LWT_TOPIC, lwt_payload, qos=1, retain=True)

    client.on_connect    = on_connect
    client.on_disconnect = on_disconnect
    client.on_message    = on_message

    try:
        client.connect(BROKER_HOST, BROKER_PORT, keepalive=60)
    except ConnectionRefusedError:
        logger.error(
            f"❌ Không thể kết nối Broker {BROKER_HOST}:{BROKER_PORT}.\n"
            f"   Hãy cài và khởi động Mosquitto: mosquitto -v"
        )
        sys.exit(1)

    # Gửi Telemetry định kỳ trong thread riêng
    threading.Thread(target=telemetry_loop, args=(client,), daemon=True).start()

    logger.info("Mock ESP32 đang chạy. Nhấn Ctrl+C để dừng.\n")
    try:
        client.loop_forever()
    except KeyboardInterrupt:
        logger.info("Đang tắt Mock ESP32...")
        # Gửi STATUS OFFLINE trước khi tắt
        client.publish(STATUS_TOPIC, json.dumps({
            "event": "STATUS",
            "status": "OFFLINE",
            "timestamp": now_iso()
        }), qos=1)
        client.disconnect()


if __name__ == "__main__":
    main()
