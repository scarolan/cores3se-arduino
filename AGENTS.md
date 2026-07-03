# AGENTS.md - AI Assistant Reference

This file contains essential information for AI coding assistants working on this M5Stack CoreS3 SE Arduino project.

## Project Overview

**Project:** CoreS3 SE Arduino Projects  
**Device:** M5Stack CoreS3 SE (ESP32-S3, 320x240 IPS, 16MB flash)  
**Framework:** PlatformIO + Arduino  
**Current App:** Classic Screensavers (6 modes with auto-cycling)

## Hardware Setup

- **USB Connection:** Device is connected at `/dev/ttyACM0`
- **Board:** M5Stack CoreS3 SE
- **Display:** 1.54" IPS, 320x240 pixels
- **Flash Memory:** 16MB
- **LEDs:** 10 NeoPixels on the side

**Note:** Ensure you have proper permissions to access `/dev/ttyACM0`. If needed:
```bash
sudo usermod -a -G dialout $USER
```
Then log out and back in.

## PlatformIO Setup

**Used in this repository:** PlatformIO 6.1.19 with Python venv

PlatformIO is installed using a virtual environment to avoid conflicts with system packages:

```bash
cd /home/scarolan/git_repos/cores3se-arduino
python3 -m venv .venv
source .venv/bin/activate
pip install platformio
pio platform install espressif32
```

### Build Commands (in .venv)

Always activate the virtual environment before running PlatformIO commands:

```bash
source .venv/bin/activate
pio run                    # Build the project
pio run -t upload         # Build and flash to device
pio device monitor        # Serial monitor (default port auto-detects)
```

### Manual Port Specification

If auto-detection fails, specify the port explicitly:

```bash
source .venv/bin/activate
pio run -t upload --upload-port /dev/ttyACM0
pio device monitor --port /dev/ttyACM0
```

### PlatformIO Environment Settings (`platformio.ini`)

```ini
[env:m5stack-cores3]
platform = espressif32
board = m5stack-cores3
framework = arduino
monitor_speed = 115200
upload_speed = 1500000
board_build.partitions = default_16MB.csv
lib_deps =
    m5stack/M5Unified@^0.2.2
    m5stack/M5GFX@^0.2.5
    fastled/FastLED@^3.9.0
    bblanchon/ArduinoJson@^7
    olikraus/U8g2@^2
```

## Current App: Classic Screensavers

**Source:** `src/main.cpp`

The app is already uploaded and running on the device.

### Modes (auto-cycles):
1. **Flying Toasters** - Animated wing sprites (homage to "After Dark")
2. **Pipes** - 3D-shaded pipes growing with elbow joints
3. **Starfield** - 500 stars with perspective projection and motion streaks
4. **Matrix Rain** - Falling green characters with glowing heads
5. **Mystify** - Bouncing quadrilaterals with ghost trails
6. **DVD Logo** - Classic bouncing logo with color changes on edge hits

### Controls:
- **Tap screen** - Skip to next mode
- **Auto-cycle** - Modes transition every 45-90 seconds with fade-to-black
- **BtnB** (bottom-center touch) - Take screenshot (when enabled)

## Project Structure

```
src/
  main.cpp              Active application source
  toaster_sprites.h     Generated flying toaster sprite data (RGB332 + alpha)
  dvd_logo.h            DVD logo alpha mask for runtime colorization
  homer_data.h          Embedded video frame data

apps/
  genart/               Generative art apps (plasma, flow fields, etc.)
  weather/              Weather station with OpenWeatherMap integration
  minitv/               SD card video player (RGB332 format)
  bubbles/              Bouncing balls physics demo
  clock/                Analog clock with digital readout
  vortex/               Hypnotic spiral zoom effect

screenshots/            Screensaver mode capture images
convert_sprites.py      Converts toaster sprite sheet to embedded data
convert_dvd_logo.py     Converts DVD logo to alpha mask
convert_video.py        Converts video files to RGB332 format
platformio.ini          PlatformIO build configuration
AGENTS.md              This file - AI assistant reference
```

## Sprite Conversion Tools

These scripts are in the root directory:

```bash
python3 convert_sprites.py    # Converts toasters_and_toast.png → src/toaster_sprites.h
python3 convert_dvd_logo.py   # Converts dvdlogo.png → src/dvd_logo.h
python3 convert_video.py input.mp4  # Converts video → RGB332 format for Mini TV
```

**Requirements:** Python 3 with Pillow library

## Development Workflow

### Flashing a New App (in .venv)

1. Activate the virtual environment:
   ```bash
   source /home/scarolan/git_repos/cores3se-arduino/.venv/bin/activate
   ```

2. Copy the desired app's `main.cpp` to `src/main.cpp`:
   ```bash
   cp apps/<appname>/main.cpp src/main.cpp
   ```

3. Build and upload:
   ```bash
   pio run -t upload --upload-port /dev/ttyACM0
   ```

### Weather Station Setup (in .venv)

1. Copy app files:
   ```bash
   source /home/scarolan/git_repos/cores3se-arduino/.venv/bin/activate
   cp apps/weather/main.cpp src/main.cpp
   cp apps/weather/weather_icons.h src/
   cp apps/weather/config.h src/
   ```

2. Edit `src/config.h` with WiFi credentials and OpenWeatherMap API key

3. Build and upload:
   ```bash
   pio run -t upload --upload-port /dev/ttyACM0
   ```

### Screenshot Feature

To enable screenshots in `src/main.cpp`:
1. Uncomment `#include <SD.h>`
2. Set `ENABLE_SCREENSHOTS` to `1`
3. Insert microSD card
4. Press **BtnB** (bottom-center touch area) to capture BMP files

**Note:** Rebuild using the virtual environment:
```bash
source /home/scarolan/git_repos/cores3se-arduino/.venv/bin/activate
pio run -t upload --upload-port /dev/ttyACM0
```

## Troubleshooting

### Connection Issues

If the device is not found at `/dev/ttyACM0`:
- Check USB connection
- Verify user has permissions: `ls -l /dev/ttyACM*`
- Try `sudo usermod -a -G dialout $USER` and re-login
- Ensure virtual environment is activated if using PlatformIO commands

### Build/Fetch Errors (use .venv)

```bash
source /home/scarolan/git_repos/cores3se-arduino/.venv/bin/activate
pio platform update           # Update PlatformIO platforms
pio pkg update               # Update all packages
pio cache clear              # Clear cache if corrupted
```

## Dependencies

- **M5Unified** (v0.2.2+) - Unified M5Stack driver
- **M5GFX** (v0.2.5+) - Graphics library with sprite support
- **FastLED** (v3.9.0+) - NeoPixel LED control
- **ArduinoJson** (v7+) - JSON parsing for weather app
- **U8g2** (v2+) - Additional graphics library

## References

- [M5Stack CoreS3 SE Docs](https://docs.m5stack.com/en/core/CoreS3%20SE)
- [PlatformIO Documentation](https://docs.platformio.org/)
- [LovyanGFX](https://github.com/lovyan03/LovyanGFX) - Base graphics library

## Notes for AI Assistants

- Use PlatformIO CLI commands (not Arduino IDE)
- The project uses 16-bit RGB565 or 8-bit RGB332 color formats
- Sprite data is pre-generated and stored in `.h` files
- Screen resolution: 320x240 pixels
- Default serial baud rate: 115200
