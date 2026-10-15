"""Software-only robot emulator: test brain.py even without Wokwi/ESP32."""
import json
import socket
import time

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("127.0.0.1", 4211))
sock.setblocking(False)
print("Mock robot started. Run `python brain.py` in another terminal. Ctrl+C to quit.")
seq = 0
mode = "STOP"
last_command = 0.0
last_status = 0.0
start = time.monotonic()
try:
    while True:
        try:
            raw, peer = sock.recvfrom(1024)
            obj = json.loads(raw.decode())
            if obj.get("v") == 1 and obj.get("type") == "cmd" and obj.get("mode") in (
                "STOP", "EXPLORE", "RETURN_HOME", "INVESTIGATE_MARKER"
            ):
                mode = obj["mode"]
                last_command = time.monotonic()
        except BlockingIOError:
            pass
        link_ok = last_command > 0 and (time.monotonic() - last_command < 0.6)
        state = "EXPLORE" if link_ok and mode == "EXPLORE" else ("STOP" if link_ok else "WAIT_PC")
        if time.monotonic() - last_status > 0.2:
            seq += 1
            t = {"v": 1, "type": "telemetry", "seq": seq,
                 "uptime_ms": int((time.monotonic() - start) * 1000),
                 "state": state, "mode_requested": mode,
                 "contact_l": False, "contact_r": False,
                 "battery_mv": None, "link_ok": link_ok}
            sock.sendto(json.dumps(t).encode(), ("127.0.0.1", 4210))
            last_status = time.monotonic()
        time.sleep(0.01)
except KeyboardInterrupt:
    pass
finally:
    sock.close()
