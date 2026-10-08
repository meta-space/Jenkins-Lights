# AGENTS.md

ESP32 (Arduino, PlatformIO) traffic light for Jenkins builds. Behaviour mirrors the desktop tool in `D:\Sandbox\JenkinsStatus` (`src/JenkinsStatus/Jenkins.cs`); keep the two consistent.

## Commands

- Build: `pio run` (default env `esp32dev`, classic ESP32); ESP32-S3: `pio run -e esp32r4`
- Upload / monitor: `pio run -t upload`, `pio device monitor`
- Unit test (host): `pio test -e native`
- On Windows use `C:\Users\fda\.platformio\penv\Scripts\platformio.exe` if `pio` is not in PATH.

## Layout

| File | Purpose |
|------|---------|
| `src/jenkins.h` | Tree query, job list parsing, light rule. No Arduino deps, so it is host-testable. |
| `src/main.cpp` | WiFi, settings (NVS via `Preferences`), polling + lamps in `loop()`, web API, log buffer |
| `src/index.html` | The whole web UI (vanilla HTML/JS), embedded into the firmware via `board_build.embed_txtfiles` |
| `test/test_jenkins/` | Unity test for `jenkins.h` |

## How it works

- `loop()` polls `<url>/api/json?tree=jobs[...]` (3 folder levels) every `poll` seconds, and redraws the lamps every 500 ms.
- Light: building → orange, FAILURE/UNSTABLE → red, SUCCESS → green, else off; Jenkins/WiFi error → all blink.
- Job names are Jenkins `fullName`s kept escaped (`feature%2Ffoo`); the UI decodes them for display.
- Shared state is guarded by `mtx`; the web server runs on the AsyncTCP task.

## Web API

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/` | GET | Web UI |
| `/api/status` | GET | Light, error, all jobs, watched names |
| `/api/watch` | POST | `name`, `on=1/0` |
| `/api/config` | GET/POST | `url`, `user`, `token` (write-only), `poll` |
| `/api/light` | POST | Lamp test: `mode=auto/red/orange/green/off` |
| `/api/log` | GET | Recent log text (app + ESP-IDF) |

POSTs are form-encoded and must carry an `X-BuildStatus` header (CSRF guard).
