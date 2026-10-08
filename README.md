# Build Status Traffic Light

ESP32 firmware that shows the status of watched Jenkins jobs on a red/orange/green traffic light. It is the hardware counterpart of the JenkinsStatus desktop tray tool and uses the same rules.

| Light | Meaning |
|-------|---------|
| Orange | A watched job is building |
| Red | A watched job's last build failed or is unstable |
| Green | All watched jobs passed |
| Off | Nothing watched or no results yet |
| All blinking | Jenkins or WiFi problem (see the web page) |

## Setup

1. Set `WIFI_SSID` / `WIFI_PASSWORD` (and the lamp pins / `ACTIVE_LOW` for relay boards) at the top of `src/main.cpp`.
2. `pio run -t upload`, then open `http://buildstatus.local/` (or the IP printed by `pio device monitor`).
3. Under **Settings**, enter the Jenkins URL, your user and API token (Jenkins → your name → Security → API Token).
4. Tick the jobs to watch. **Lamp test** checks the wiring.

Settings and watched jobs are stored in NVS. The API token is stored unencrypted on the device and is never sent back to the browser; changing the Jenkins URL clears it.

## Development

- `pio run` builds the `esp32r4` (ESP32-S3) target; `pio run -e esp32dev` builds a classic ESP32.
- `pio test -e native` runs the host unit test for the Jenkins parser and light rule (`src/jenkins.h`).

## Howto Flash

1. Start the Upload in Platformio, it will detect the USB port automatically
2. Once you see the "Connecting ...." status
3. Hold down the PROG button and keep holding it
4. Press the RESET 
5. Release the PROG button
