"""
audit_test.py
Comprehensive test suite auditing Frontend static files, Backend REST endpoints, and DB rules.
"""
import sys
import os

# Ensure backend root is in sys.path
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from fastapi.testclient import TestClient
from main import app

client = TestClient(app)
results = []

def run_test(name, fn):
    try:
        fn()
        results.append((name, "PASS", ""))
    except Exception as e:
        results.append((name, "FAIL", str(e)))

# 1. Health & Info
def test_health():
    r = client.get("/health")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"
    data = r.json()
    assert data["status"] == "ok"
    assert data["service"] == "Smart Pillbox Backend"

def test_info():
    r = client.get("/api/info")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"

# 2. Static files & SPA
def test_frontend_index():
    r = client.get("/")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"
    assert "SmartMed" in r.text, "SmartMed not in HTML"
    assert "dispenser-2d-box" in r.text, "dispenser-2d-box missing from HTML"
    assert "tab-dashboard" in r.text, "tab-dashboard missing from HTML"
    assert "tab-schedules" in r.text, "tab-schedules missing from HTML"
    assert "tab-device" in r.text, "tab-device missing from HTML"
    assert "tab-history" in r.text, "tab-history missing from HTML"

def test_frontend_css():
    r = client.get("/css/style.css")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"
    assert len(r.text) > 1000

def test_frontend_api_js():
    r = client.get("/js/api.js")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"
    assert "SmartMedAPI" in r.text

def test_frontend_ws_js():
    r = client.get("/js/ws.js")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"
    assert "SmartMedWS" in r.text

def test_frontend_carousel2d_js():
    r = client.get("/js/carousel2d.js")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"
    assert "SmartMedCarousel2D" in r.text

def test_frontend_app_js():
    r = client.get("/js/app.js")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"
    assert "setupWebSocket" in r.text

# 3. Slots API
def test_slots_list():
    r = client.get("/api/v1/slots")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"
    slots = r.json()
    assert len(slots) == 8, f"Expected 8 slots, got {len(slots)}"
    assert slots[0]["slot_number"] == 0, "Slot 0 missing"
    assert slots[0]["capacity"] == 0, "Slot 0 capacity must be 0"

def test_slot_0_safe_home_protection():
    r = client.put("/api/v1/slots/0", json={"current_quantity": 5})
    assert r.status_code == 400, f"Expected 400 for Slot 0 update, got {r.status_code}"

# 4. Medicines API
def test_medicines_list():
    r = client.get("/api/v1/medicines")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"

# 5. Device API
def test_device_status():
    r = client.get("/api/v1/device/status")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"
    data = r.json()
    assert "is_online" in data
    assert "fsm_state" in data

def test_device_dashboard():
    r = client.get("/api/v1/device/dashboard")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"
    dash = r.json()
    assert "device" in dash
    assert "slots" in dash
    assert "schedules" in dash
    assert "recent_logs" in dash
    assert len(dash["slots"]) == 8

# 6. Schedules API
def test_schedule_slot0_forbidden():
    r = client.post("/api/v1/schedules", json={"slot_number": 0, "time_of_day": "08:00"})
    assert r.status_code == 422, f"Expected 422 for slot 0 schedule, got {r.status_code}"

def test_schedules_list():
    r = client.get("/api/v1/schedules")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"

# 7. Dispensing API
def test_dispense_history():
    r = client.get("/api/v1/dispense/history")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"

def test_dispense_active():
    r = client.get("/api/v1/dispense/active")
    assert r.status_code == 200, f"Expected 200, got {r.status_code}"

if __name__ == "__main__":
    tests = [
        ("1. Health Endpoint", test_health),
        ("2. API Info Endpoint", test_info),
        ("3. SPA HTML Delivery", test_frontend_index),
        ("4. CSS Design System", test_frontend_css),
        ("5. API Client JS", test_frontend_api_js),
        ("6. WebSocket Client JS", test_frontend_ws_js),
        ("7. 2D Carousel Engine JS", test_frontend_carousel2d_js),
        ("8. Main Orchestrator app.js", test_frontend_app_js),
        ("9. Slots Configuration (8 slots)", test_slots_list),
        ("10. Slot 0 Safe Home Rule (400 Bad Request)", test_slot_0_safe_home_protection),
        ("11. Medicines Catalog", test_medicines_list),
        ("12. Device Status (RAM State)", test_device_status),
        ("13. Device Dashboard Aggregation", test_device_dashboard),
        ("14. Schedule Slot 0 Protection (422)", test_schedule_slot0_forbidden),
        ("15. Schedules Listing", test_schedules_list),
        ("16. Dispensing History", test_dispense_history),
        ("17. Active Dispensing Session", test_dispense_active),
    ]

    for name, fn in tests:
        run_test(name, fn)

    print("\n================ AUDIT TEST RESULTS ================")
    all_pass = True
    for name, status, err in results:
        print(f"[{status}] {name}" + (f" -> {err}" if err else ""))
        if status != "PASS":
            all_pass = False
    print("====================================================")
    print("SUMMARY:", "ALL 17 TESTS PASSED!" if all_pass else "FAILURES DETECTED!")
    sys.exit(0 if all_pass else 1)
