# CrowPanel 1.46" HMI ESP32 Rotary Display — ESP-IDF (v5.3+) SquareLine project

Pure ESP-IDF project (no Arduino) for the Elecrow **CrowPanel 1.46inch HMI ESP32
Rotary Display**. LVGL is initialized and bound to the panel; the SquareLine UI
under `main/GUI/` is rendered on the round display, with capacitive touch input.

## Board hardware

| | |
|---|---|
| **MCU** | ESP32-S3R8 (dual-core LX7, 8 MB octal PSRAM, 16 MB flash) |
| **Display** | 1.46" round IPS LCD, **360 × 360** |
| **Display controller** | ST77916 over standard SPI (the product page labels it "JD9855", but the shipping firmware drives it as ST77916/ST77961) |
| **Touch** | CST816T capacitive, I2C @ `0x15` |
| **Rotary knob** | quadrature encoder + push button; turning it switches the Home background; holding it 5 s opens WiFi setup |
| **Extras (unused here)** | RGB LEDs — not required for the UI |

### Pin map

| Function | GPIO |
|---|---|
| LCD SCLK / MOSI / CS / DC / RST | 10 / 11 / 9 / 3 / 14 |
| LCD backlight | 46 |
| Touch SDA / SCL / RST / INT | 6 / 7 / 13 / 5 |
| Rotary encoder A / B / button | 45 / 42 / 41 |

## WiFi setup

On first boot (or when the saved WiFi can't connect within 60 s) the dashboard
opens a hotspot `EnergyDashboard-XXXX`. Scan the QR code on the screen with your
phone camera to join it (the hotspot name and password are also printed on the
serial monitor). The setup page opens automatically, or go to
`http://192.168.4.1`. Pick your WiFi, enter the password and optionally your
Tibber API token, and press **Opslaan**.

Prices come from the Tibber API (quarter-hour prices, fetched about hourly);
without a token the price shows `--`. To change the token, run the WiFi setup
again and pick the same network.

To change the WiFi later: gear button → **WiFi opnieuw instellen**, or hold the
knob pressed for 5 s.

## Build & flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

Requires **ESP-IDF v5.3 or newer**. Display + touch drivers are pulled from the
Espressif Component Registry (`esp_lcd_st77916`, `esp_lcd_touch_cst816s`) into
`managed_components/` during the first build.

## Notes

- The ST77916 init sequence in `main.cpp` is ported from the manufacturer's
  LovyanGFX panel definition for this exact 360×360 round panel.
- Touch is brought up non-fatally: if the CST816T does not answer on I2C, the
  display still comes up and the touch input device is simply not registered.

Board: <https://www.elecrow.com/crowpanel-1-46inch-hmi-esp32-rotary-display-360-360-ips-round-touch-knob-screen.html>

## License

[EUPL-1.2](LICENSE). Third-party code keeps its own license: LVGL (MIT), the
Espressif display/touch components in `components/` (Apache-2.0) and
`components/dns_server` from the ESP-IDF captive portal example (Unlicense/CC0).
