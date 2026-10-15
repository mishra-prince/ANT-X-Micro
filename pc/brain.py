"""ANT-X Micro: PC-side UDP command/telemetry bench prototype.

Python 3.9+, standard library only. PC receives robot UDP telemetry on 4210
and sends commands back to the robot's source IP/port (normally 4211).
This is NOT an AI agent yet. It is a communication and safety interface.
"""
import csv
import json
import socket
import threading
import time
from pathlib import Path

PORT = 4210
PERIOD = 0.2
requested = "STOP"
running = True


def command_input():
    global requested, running
    print("Commands: e=EXPLORE, s=STOP, h=RETURN_HOME, i=INVESTIGATE_MARKER, q=QUIT")
    while running:
        try:
            key = input("antx> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            running = False
            return
        modes = {"e": "EXPLORE", "s": "STOP", "h": "RETURN_HOME", "i": "INVESTIGATE_MARKER"}
        if key in modes:
            requested = modes[key]
            print("Requested mode:", requested)
        elif key == "q":
            requested = "STOP"
            running = False
            return


def main():
    global running
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", PORT))
    sock.settimeout(0.05)
    peer = None
    next_send = time.monotonic()
    last_print = 0
    seq = 0
    log = Path(__file__).with_name("telemetry.csv")
    threading.Thread(target=command_input, daemon=True).start()
    print(f"Listening for robot telemetry on UDP {PORT} (local LAN or Wokwi Private Gateway)")
    with log.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["pc_time", "robot_ip", "seq", "state", "mode_requested", "contact_l", "contact_r", "battery_mv", "link_ok"])
        try:
            while running:
                try:
                    data, address = sock.recvfrom(1024)
                    telemetry = json.loads(data.decode("utf-8"))
                    if telemetry.get("v") != 1 or telemetry.get("type") != "telemetry":
                        continue
                    peer = address
                    writer.writerow([time.time(), address[0], telemetry.get("seq"),
                                     telemetry.get("state"), telemetry.get("mode_requested"),
                                     telemetry.get("contact_l"), telemetry.get("contact_r"),
                                     telemetry.get("battery_mv"), telemetry.get("link_ok")])
                    f.flush()
                    if time.monotonic() - last_print > 1.0:
                        print(f"\nRobot {address}: {telemetry.get('state')}; "
                              f"contacts=({telemetry.get('contact_l')},{telemetry.get('contact_r')}); "
                              f"link={telemetry.get('link_ok')}")
                        last_print = time.monotonic()
                except socket.timeout:
                    pass
                except (UnicodeDecodeError, json.JSONDecodeError):
                    continue

                if peer and time.monotonic() >= next_send:
                    seq += 1
                    packet = {"v": 1, "type": "cmd", "seq": seq,
                              "mode": requested, "ttl_ms": 600}
                    sock.sendto(json.dumps(packet, separators=(",", ":")).encode(), peer)
                    next_send = time.monotonic() + PERIOD
        except KeyboardInterrupt:
            pass
        finally:
            if peer:
                packet = {"v": 1, "type": "cmd", "seq": seq + 1,
                          "mode": "STOP", "ttl_ms": 600}
                for _ in range(3):
                    sock.sendto(json.dumps(packet).encode(), peer)
            running = False
            sock.close()
            print(f"Stopped. Log saved to {log}")


if __name__ == "__main__":
    main()
