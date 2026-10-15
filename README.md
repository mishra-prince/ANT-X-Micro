# ANT-X Micro — PC + ESP32-C3 Communication Prototype

**Scope:** Wi-Fi link, command protocol, PC telemetry logging, contact reflexes and a link-loss stop. It does **not** simulate locomotion physics, gait phase, battery limits, tilt, stall current or real ant cognition yet.

## Architecture

- PC: `pc/brain.py` — receives JSON telemetry over UDP 4210 and sends commands at 5 Hz. Manual input stands in for higher-level AI during Stage A. Logs CSV.
- ESP32-C3: `wokwi/sketch.ino` — receives UDP on 4211, drives four GPIOs that would connect to a DRV8833 input stage, and stops when a command expires or contact is pressed.
- `pc/mock_robot.py` — emulates telemetry/commands with no board required.
- `wokwi/diagram.json` — ESP32-C3 development board, two antenna buttons, four PWM indicator LEDs. LEDs **represent H-bridge inputs**, not a motor or a complete hexapod.

## Stage 0 — prove PC protocol without hardware

Terminal A: `python pc/brain.py`

Terminal B: `python pc/mock_robot.py`

In Terminal A use `e` (explore), `s` (stop), `h` (return-home placeholder), `i` (marker placeholder) or `q` (quit). Quit PC script and observe the emulator's watchdog drop to `WAIT_PC` if instrumented; it keeps sending telemetry while running.

## Stage 1 — prove ESP32 and Wi-Fi on a real development board

1. Open the Wokwi project editor or use Arduino IDE with the ESP32-C3-DevKitM-1 board; add ArduinoJson 7.
2. Set `WOKWI_MODE = false` and enter Wi-Fi SSID/password and the PC's actual LAN IP in `sketch.ino`.
3. Run `python pc/brain.py` on the PC, allow UDP 4210 through the firewall for the **private network**.
4. Flash ESP32-C3. On power-up it must remain in `WAIT_PC` until valid commands arrive.
5. Press the two contact buttons (or wire GPIO0/GPIO1 momentary switches to ground). Either switch must stop motor outputs.
6. Stop the PC script or disable its network: outputs must stop within the 600 ms command expiry, plus scheduling latency.

**Electrical safety:** Do not connect a motor directly to an ESP32 GPIO. The diagram uses LEDs only. When using a DRV8833, connect GPIO4/5 and GPIO6/7 to its logic inputs, supply the driver and motors separately with the correct voltage, join grounds, limit motor current and use a current-limited bench supply. Verify DRV8833 truth table, actual motor voltage, cooling and PWM before motor tests. Never power the motors from a GPIO pin.

## Stage 1b — Wokwi Wi-Fi connection to a PC

Wokwi supports ESP32-C3. Its default **Public** IoT Gateway cannot access a service hosted on your local PC. To use `host.wokwi.internal` in the ESP32 sketch, you must enable Wokwi's **Private IoT Gateway** on a supported paid tier. Without it, the Wokwi diagram can still simulate the code and LEDs but does not complete the direct local PC networking experiment. Use `mock_robot.py` or a physical dev board instead.

1. Set `WOKWI_MODE = true` (default in included sketch).
2. Add `sketch.ino`, `diagram.json`, and `libraries.txt` in an ESP32-C3-DevKitM-1 Wokwi project.
3. Start the Private IoT Gateway on the PC, enable it in the simulator, then start `python pc/brain.py`.
4. Observe telemetry and command-driven LEDs and test contact buttons.

See https://docs.wokwi.com/guides/esp32-wifi for details and subscription requirements. Wi-Fi and UDP are simulated, but robot mechanics, battery behavior, RF reliability and electrical loads are not.

## Protocol v1

Robot to PC, 5 Hz:
```json
{"v":1,"type":"telemetry","seq":34,"uptime_ms":9210,"state":"WAIT_PC","mode_requested":"STOP","contact_l":false,"contact_r":false,"battery_mv":null,"link_ok":false,"last_cmd_seq":0}
```

PC to robot, every 200 ms:
```json
{"v":1,"type":"cmd","seq":12,"mode":"EXPLORE","ttl_ms":600}
```

Valid modes: `STOP`, `EXPLORE`, `RETURN_HOME`, `INVESTIGATE_MARKER`. Only `EXPLORE` produces forward drive in this prototype; the others stop. The ESP32 checks packet version, type, mode, TTL, and configured PC source IP before accepting commands. A missing command causes a local motor stop. `seq` is diagnostic in v1, not a replay defense. The design assumes an isolated trusted test network; it does **not** provide cryptographic authentication or internet-facing security.

## What comes next

1. Measure link timing, Wi-Fi current peaks, radio disconnections and fail-safe performance using a real dev board.
2. Test a single motor and cam/leg jig using separate current-limited motor supply.
3. Integrate motor current sensing, Hall phases, IMU tilt and low-battery ADC on the MCU. These must override PC commands locally.
4. Replace `brain.py` manual mode selection with Python state estimation and ant-inspired route planning.
5. Only then revisit the 30 × 18–22 mm custom PCB and overall 35–45 mm robot envelope.
