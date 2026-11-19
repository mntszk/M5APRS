# M5APRS

M5Stack Basic GPS APRS tracker/receiver (1200bps AFSK)

English | [日本語](README.md)

M5APRS is amateur radio firmware for the M5Stack Basic V2.6. It turns the GPS position into APRS packets and sends them to a transceiver as 1200 bps AFSK, and it decodes the APRS packets received by the transceiver and shows them on the screen. It is a personal hobby project and is not affiliated with M5Stack.

## Features
- Builds APRS position reports (uncompressed format) from GPS (NMEA) and transmits them (long press A for one packet, or automatic beaconing)
- Automatic beaconing: off / ack only / every 1-60 minutes / Flexible (selected in the settings menu and kept across power cycles). Waits while another station is transmitting
- Flexible: changes the beacon interval with speed and turns. It works like the SmartBeaconing of Yaesu and Kenwood radios, with the same parameters, which are set in config.txt
- PTT is switched on and off automatically when transmitting (GPIO5 → transistor). Maximum PTT time is 10 seconds
- Receives and decodes 1200 bps AFSK (Bell 202). Shows uncompressed, compressed and Mic-E positions, messages and more in the received list
- Messaging: receives messages addressed to you and bulletins, sends acks automatically, and sends messages (retried until acked). Pick the addressee from received stations and the text from preset texts. Free text can be typed with a CardKB2
- Chimes: different sounds for received packets, your own packets repeated by a digipeater, acks and messages (volume set separately from the monitor)
- Erasing records: the received list, WX stations, messages and radar stations can be erased from the settings menu, one list at a time or all at once (with confirmation)
- WX (weather) screen: temperature, humidity, pressure, wind and rain of stations that sent weather data (up to 8 stations), as a list and in detail
- Receive status screen: signal / receiving data / packet OK / CRC error, input level, MARK/SPACE tone levels and their ratio (also for the last received station), counts of good packets, CRC errors and noise
- Position source selected manually: GPS / last position / fixed position (up to 8 in config.txt)
- Monitor of the transmitted audio (internal speaker, 5 levels)
- Calibration tone: from the settings menu, sends a continuous tone (1200 Hz / 2200 Hz / alternating) with PTT, to adjust the attenuator and the transmit level (use a dummy load)
- Diagnostic output to the serial monitor (115200 bps): PTT timing, sent and received packets, receive status, periodic status
- Saves a receive log (CSV) and an operation log to microSD
- Station settings such as your callsign are written in `config.txt` on the microSD card (no files to edit before building)
- Symbol (21 kinds) and SSID (0-15) are set separately. Symbol icons are shown in the received list, the detail view and the own station screen
- Radar screen: shows received stations around your position with their symbol icons and callsigns (range 1-100 km or auto, time 10 minutes to 24 hours)
- Screen language: Japanese / English (settings menu "言語 / Language")
- Version information: the last item of the settings menu shows the version (`version.h`), build date and supported board revision

## Hardware
| Item | Details |
|---|---|
| Main unit | M5Stack Basic V2.6 |
| GPS | GPS module for M5Stack (UART2: RX=GPIO16, TX=GPIO17, 115200 bps) |
| Transmit | DAC GPIO26 → attenuator → transceiver microphone input |
| Monitor | DAC GPIO25 (internal speaker) |
| PTT | GPIO5 → LED (also PTT indicator) + resistor → NPN transistor (2SC2712 on IF v1). Pulls the microphone line to GND through a resistor |
| Receive | Transceiver earphone output → ADC GPIO36 (with DC bias) |
| Storage | Internal microSD (CS=GPIO4) |

The attenuator, receive input circuit and PTT circuit are on an external board. The design data of the IF v1 board (schematic, PCB, BOM, Gerber) is in `hardware/`. Use the transceiver in analog FM. See "Status" and `hardware/radio_compat.md` (Japanese) for the transceivers that have been tested.

> Caution: The DAC output is about 2.6 Vpp. Always attenuate it before feeding it to the transceiver's microphone input. Do not connect a GPIO directly to the PTT line.

## Getting started
1. Install the **ESP32 board package 2.x** in the Arduino IDE (expected version 2.1.4; 3.x is not supported) and the **M5Unified** library
2. Put this repository in a folder named `M5APRS` (the Arduino IDE requires the sketch name and the folder name to match)
3. Open `M5APRS.ino`, select a board such as "M5Stack-Core-ESP32" and upload (there are no files to edit)
4. Insert a microSD card (formatted as FAT32, e.g. SDHC 32 GB or smaller) and start the unit. A sample `/aprs/config.txt` is created. Edit it on a computer, put the card back and restart
   ```
   callsign = N0CALL
   comment  = M5Stack APRS
   fixed1   = Home, 35.68123, 139.76712, 12
   ```
   At minimum, change `callsign` to your own callsign (choose the SSID in the settings menu of the unit). Nothing is transmitted while it is `NOCALL`

### Settings file config.txt
- One setting per line, as `key = value`. Lines starting with `#` are comments (a `#` in the middle of a value is part of the value)
- You can set: callsign, comment, fixed positions (8), digipeater path choices (3), message preset texts and addressee candidates (8 each), and transceiver-dependent values such as transmit level and PTT timing. The list of keys is in section 3.1 of `docs/APRS_M5Stack_spec.md` (Japanese) and in the comments of the sample file
- The file is read only at startup. Lines with errors are ignored, and their line numbers are shown on the startup screen and in "Version" of the settings menu
- The contents are also kept in the unit, so it keeps working with the last settings even without the microSD card (if config.txt is missing on the card, it is written back from the unit)
- Note that giving the edited card to someone else also gives them your callsign

## Operation
| Button | Short press | Long press |
|---|---|---|
| A | Previous (screen / item) | **Send beacon** |
| B | Select (open / change / start) | Back (one level up) |
| C | Next (screen / item) | Settings menu (open / close) |

A short press acts within the current screen. A long press does the same thing on every screen.

The received list, WX stations, messages and radar stations are saved to the microSD card and remain after a power cycle (a message that was being retried becomes "no response").

At the bottom of the screen, the function of each button is shown in a box (upper row = short press, lower row = long press). At the top right, icons show the battery, SD card, volume, chime, GPS/position, automatic beaconing, transmit/receive and unread messages.

There are 7 screens: own station / received list / radar / messages / WX / receive status / transmit & settings. In the settings menu you can change position ambiguity, digipeater path, symbol, SSID, course and speed, altitude, monitor volume, chime volume, screen brightness, position source, screen language and more, and erase records. See `docs/` for details (Japanese).

## Tests
The computational parts (AFSK, AX.25/APRS, NMEA, settings, logs, screen layout, etc.) can be tested on a PC without an ESP32 (requires g++).
```
./tests/run_tests.sh          # test with the default settings
./tests/run_tests.sh fixed    # test with fixed positions set
```
Timing, DAC/ADC characteristics and the external circuit must be checked on the real hardware.

## Status
Tested with ICOM ID-31 / ID-52 and Standard C601 / C701 together with the IF v1 board. The default transmit level and other values in config.txt were confirmed with these combinations.

## Documents
- `CHANGELOG.md` : changes in each version (Japanese and English)

In Japanese:
- `docs/APRS_M5Stack_spec.md` : specification
- `docs/UI_button_spec.xlsx` : button assignments, screens, settings menu, symbols and SSIDs, and the Japanese-English text table
- `hardware/` : design data of the IF v1 board (KiCad, Gerber, BOM) and the transceiver compatibility notes (`radio_compat.md`)

## Notes
- Use within the terms of your amateur radio license and operating rules. Use at your own risk
- The firmware is intended for the 1200 bps AFSK APRS frequency on the 430 MHz band in Japan. Set the frequency on the transceiver

## Libraries
- [M5Unified](https://github.com/m5stack/M5Unified) / [M5GFX](https://github.com/m5stack/M5GFX) (MIT). The Japanese font is the converted IPAex Gothic included in M5GFX (IPA Font License)
- Arduino core for the ESP32 (LGPL 2.1)
- The device name tables (Mic-E and TOCALL) are data from [aprs-deviceid](https://github.com/aprsorg/aprs-deviceid) (CC BY-SA 2.0)

The full list and the terms of each license are in `THIRD_PARTY_LICENSES.md`.

## License
Copyright (c) 2026 Minato Suzuki

- Software (firmware): [MIT License](LICENSE)
- Interface board design data (`hardware/`): [CERN-OHL-P v2](hardware/LICENSE) (a permissive open hardware license; see `hardware/README.md`)

- All M5APRS code was written independently. No source code from other projects is used, other than the libraries listed under "Libraries"
- Only the device name tables (`MICE_DEV` in `packet_parse.cpp` and `tocalls_table.h`) are based on aprs-deviceid data and are CC BY-SA 2.0
- When distributing a built binary, include `LICENSE` and `THIRD_PARTY_LICENSES.md`

Requests (not license conditions):
- If you distribute a modified version, please give it a name other than M5APRS
- Modified versions should not use the M5APRS destination callsign (TOCALL `APM5AP`); please obtain your own (you can request one from aprs-deviceid)
