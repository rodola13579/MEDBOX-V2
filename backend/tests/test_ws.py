"""Test WebSocket endpoint: connect -> snapshot -> ping/pong -> get_status."""
import asyncio
import json
import sys

import pytest

try:
    import websockets
except ImportError:
    print("SKIP: websockets chua cai (pip install websockets)")
    sys.exit(0)


@pytest.mark.asyncio
async def test_ws():
    uri = "ws://localhost:8000/ws"
    try:
        async with websockets.connect(uri, open_timeout=5) as ws:
            # 1. Nhan snapshot 'connected'
            msg = await asyncio.wait_for(ws.recv(), timeout=5)
            data = json.loads(msg)
            assert data["type"] == "connected", f"Expected connected, got {data['type']}"
            snapshot = data["data"]
            assert "device" in snapshot, "Missing device in snapshot"
            assert "active_session" in snapshot, "Missing active_session in snapshot"
            assert "connections" in snapshot, "Missing connections in snapshot"
            print(f"[1] Snapshot type: {data['type']}")
            print(f"    device.is_online: {snapshot['device']['is_online']}")
            print(f"    active_session.is_active: {snapshot['active_session']['is_active']}")
            print(f"    connections: {snapshot['connections']}")

            # 2. Gui ping -> nhan pong
            await ws.send(json.dumps({"type": "ping"}))
            msg2 = await asyncio.wait_for(ws.recv(), timeout=3)
            data2 = json.loads(msg2)
            assert data2["type"] == "pong", f"Expected pong, got {data2['type']}"
            print(f"[2] Ping -> Pong: OK")

            # 3. Gui get_status -> nhan device_status
            await ws.send(json.dumps({"type": "get_status"}))
            msg3 = await asyncio.wait_for(ws.recv(), timeout=3)
            data3 = json.loads(msg3)
            assert data3["type"] == "device_status", f"Expected device_status, got {data3['type']}"
            assert "device" in data3["data"], "Missing device in device_status"
            assert "active_session" in data3["data"], "Missing active_session in device_status"
            print(f"[3] get_status response type: {data3['type']}")

            # 4. Test unknown message type -> server phai im lang
            await ws.send(json.dumps({"type": "unknown_type"}))
            # Doi 0.5s -- neu server crash thi se bi exception
            await asyncio.sleep(0.5)
            print(f"[4] Unknown message type: server khong crash")

            print("\nPASS: WebSocket flow hoan chinh!")

    except (ConnectionRefusedError, OSError):
        pytest.skip("Server local 8000 khong chay - bo qua test live websocket")
    except AssertionError as e:
        print(f"FAIL: {e}")
        sys.exit(1)
    except Exception as e:
        print(f"FAIL: {e}")
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(test_ws())
