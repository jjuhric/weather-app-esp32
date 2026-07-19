# Weather App - ESP32 (PlatformIO)

This project displays weather information on an SPI TFT using an ESP32.

## Overview
- Platform: ESP32 (tested with `esp32dev`/`esp32_cyd` PlatformIO environment)
- Display: ILI9341-compatible SPI TFT using `TFT_eSPI`
- Weather API: OpenWeather (current weather)
- Message Display: HTTP POST `/message` endpoint for custom messages with animations

## Quick start
1. Install PlatformIO in VS Code or use the PlatformIO Core CLI.
2. Copy `include/secrets.example.h` to `include/secrets.h` and fill in your values (WiFi and OpenWeather API key).

   ```sh
   cp include/secrets.example.h include/secrets.h
   # edit include/secrets.h and add your values
   ```

3. Build and upload (uses environment `esp32_cyd`):

   ```sh
   # build
   python -m platformio run -e esp32_cyd

   # upload (auto-detect port)
   python -m platformio run --target upload --environment esp32_cyd
   ```

4. Open the serial monitor for debug output (115200):

   ```sh
   python -m platformio device monitor --environment esp32_cyd
   ```

## Message Endpoint

The device hosts a simple HTTP server on port 80 with a `/message` endpoint that allows you to send custom messages to the display.

### POST /message

Send a JSON or plain-text message to display on the screen:

```powershell
# JSON format (recommended)
Invoke-RestMethod -Method Post -Uri "http://<device-ip>/message" `
  -ContentType "application/json" `
  -Body '{"message":"Hello, World!"}'

# Plain text format
Invoke-RestMethod -Method Post -Uri "http://<device-ip>/message" `
  -ContentType "text/plain" `
  -Body "Hello, World!"
```

### Message Requirements
- **Max length:** 240 characters
- **Line wrapping:** 24 characters per line, max 10 lines
- **Word wrapping:** Messages are word-wrapped (no mid-word breaks)

### Display Behavior
1. **Flash sequence (1.5 seconds):** Full screen flashes RED → WHITE → BLUE → RED → WHITE → BLUE
2. **Message display:** Message appears on black background with white text
   - Duration: 3 seconds base + 1 second per line (e.g., 4 lines = 7 seconds)
3. **Exit sequence (0.75 seconds):** Full screen flashes RED → WHITE → BLUE
4. **Return to weather:** Automatically returns to weather display

### Screen Touch Behavior
Touch anywhere on the screen to toggle the display backlight off or on.

- While the screen is off, new messages are queued in memory instead of being displayed.
- When the screen turns back on, queued messages begin displaying immediately in arrival order.
- While the screen is off and queued messages are waiting, the green LED blinks 500 ms on / 500 ms off for 5 seconds, then stays off for 2 seconds before repeating.
- While the screen is off with no queued messages, the LED stays off unless WiFi is unreachable, in which case it flashes red.
- Normal status lighting is dimmed to 50% brightness; message and error notifications use full brightness.

### Response Codes
- `200 OK`: Message accepted and will be displayed
- `413 Payload Too Large`: Message exceeds max length or exceeds max lines when word-wrapped

## Board support
- This project is configured for `board = esp32dev` in `platformio.ini` (environment `esp32_cyd`).
- It should work as-is on most ESP32 dev boards that expose the same SPI pins.

Pins configured in `platformio.ini` build flags (may need to change for other boards):
- `TFT_MOSI` (default 13)
- `TFT_MISO` (default 12)
- `TFT_SCLK` (default 14)
- `TFT_CS` (default 15)
- `TFT_DC` (default 2)
- `TFT_RST` (default -1)
- `TFT_BL` (default 21)
- `TOUCH_CS` (default 33)

To adapt for different boards, update `platformio.ini` or your board-specific `user_setup.h` and verify the wiring matches.

## Secrets handling
- `include/secrets.h` is listed in `.gitignore` and should never be committed.
- Use `include/secrets.example.h` as a template to create your local `include/secrets.h`.

## Contributing / Publishing
To publish this repository to GitHub (make public):

1. Initialize a git repo (done locally by this project):

```sh
# (run in project root)
git init
git add .
git commit -m "Initial commit"
```

2. Create a GitHub repository (either via web UI or `gh` CLI) and push:

```sh
# if you have GitHub CLI configured
gh repo create <your-username>/weather-app-esp32 --public --source=. --remote=origin
# or create repo on github.com and then
git remote add origin https://github.com/<your-username>/<repo>.git
git push -u origin main
```

## License
Pick a license and add a `LICENSE` file before publishing.

## Notes
- If you change the display driver or pinout, re-run the project and verify fonts/sizes.
- The bitmaps are embedded as monochrome arrays in `src/weather_service.cpp`.
