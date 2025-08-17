# ESP32 Chat Webserver with OpenAI API Integration

This project demonstrates a **PlatformIO-based ESP32 web service** written in C++.  
The ESP32 runs a lightweight chat webserver that connects to [api.openai.com](https://platform.openai.com/) to generate AI-powered responses in real-time.

It demonstrates using the TFT display to show the IP address of the device and the WiFi status.  It was developed and tested using the onboard TFT display on TTGO T-Display development board.

The system is designed for security and ease of configuration, with no API keys or WiFi credentials hardcoded into the firmware.

---

## ✨ Features

- **ESP32 Webserver (ESPAsyncWebServer)**  
  Serves a simple web-based chat client where users can send messages and receive responses streamed back from OpenAI.

- **Streaming AI Responses**  
  AI responses are streamed incrementally from the ESP32 to the browser, improving responsiveness.

- **Secure Configuration Mode**  
  - The ESP32 is configured to include **Access Point (AP) mode**. The configuration webpage is available only at this address.  
  - From this page you can configure:
    - Local WiFi credentials (for STA mode)  
    - Your OpenAI API key (`openai_key`)  
  - This ensures **no secrets are hardcoded** in the source code or flashed firmware.
  - These secrets are stored in NVRAM, and subsequently available after reboots or code reloads.

- **Smoothing on Client Side**  
  Chat responses are smoothed in the browser for readability, avoiding the "stutter effect" of raw streaming.

- **HTML pages are auto generated via a Python script**  
  The two HTML pages are embedded into `.h` files and included in the `main.cpp` file.  This allows intellisense on the HTML files without requiring an embedded filesystem. 

---

## 🌈 Display Behavior

The TFT display shows an IP Address and a color-coded WiFi symbol to indicate the current network status:

- **Blue**: Waiting for initial configuration (AP mode only).  
- **Green**: WiFi STA connected successfully; Chat webpage available.  
- **Red**: WiFi STA configuration exists but not connected; AP active for reconfiguration.

Depending on the mode the IP address is:

- **AP Mode**: Shows the AP IP address (default: `192.168.4.1`).  
- **STA Mode**: Shows the assigned local network IP address if connected.

---

## 🔑 Prerequisites

1. **OpenAI Account**  
   - Create a free or paid account at [https://platform.openai.com/](https://platform.openai.com/).
   - Generate an API key under **View API Keys** in your account dashboard.
   - You will use this key on the ESP32 configuration page (not in the code).

2. **PlatformIO**  
   - Install [PlatformIO](https://platformio.org/install) in VS Code or via CLI.

3. **ESP32 Board Support**  
   - Any ESP32 development board supported by PlatformIO should work. This code has been tested on the TTGO T-Display development board.

---

## 📦 Dependencies

The following PlatformIO libraries are required:

- [`ESP Async WebServer`](https://github.com/ESP32Async/ESPAsyncWebServer)  
- [`AsyncTCP`](https://github.com/ESP32Async/AsyncTCP)  
- [`ArduinoJson`](https://arduinojson.org/)
- [`TFT_eSPI`](https://github.com/bodmer/TFT_eSPI)  
- `WiFi` (comes with ESP32 Arduino core)  
- `WiFiClientSecure` (comes with ESP32 Arduino core)

These dependencies are represented in the `platformio.ini`:

```ini
lib_deps =
    me-no-dev/ESP Async WebServer
    me-no-dev/AsyncTCP
    bblanchon/ArduinoJson
    bodmer/TFT_eSPI
```



## ⚙️ Build Flags
The TFT library requires configuration to specify what TFT device your hardware has.  Rather than editing the library code, this is configured with build flags.

To enable TFT support, configure `platformio.ini` with the correct build flags for your hardware.
This code has been tested with the TTGO T Display dev board.

```ini
build_flags =
    -DUSER_SETUP_LOADED
    -DTOUCH_CS=-1
	-include $PROJECT_LIBDEPS_DIR/$PIOENV/TFT_eSPI/User_Setups/Setup25_TTGO_T_Display.h
```

## ⚙️ Configuration Flow

1. **First Boot → Access Point Mode**  
   - On first startup (or if no WiFi credentials are stored), the ESP32 launches in **AP mode**.  
   - Connect to the ESP32 AP from your phone or laptop.
   - **AP mode** continues to be available to allow for later reconfiguration.  

2. **Open Configuration Webpage**  
   - The ESP32 serves a configuration page only accessible at the AP IP address.  
   - Here you can enter:  
     - WiFi SSID and password  
     - Your OpenAI API key (`openai_key`)  

3. **Switch to Station (STA) Mode**  
   - After saving, the ESP32 reboots with **STA mode** and connects to your WiFi.  
   - The chat webservice is now available on the ESP32’s STA IP address.  

4. **Chat with AI**  
   - Open the ESP32’s STA IP in your browser.  
   - Use the chat UI to send messages to OpenAI and receive streamed responses.  

---

## 🔒 Security

- **No secrets in code:** API keys and WiFi credentials are never hardcoded in firmware.  
- **Filters:** The configuration page is **only served in AP mode**, preventing accidental exposure on the STA network.  
- **TLS:** All communication with `api.openai.com` uses `WiFiClientSecure`.  

---

## 🚀 Usage

1. Build and upload the firmware via PlatformIO.  
2. Connect to the ESP32 AP and configure WiFi + OpenAI key.  
3. System automatically reboots with STA mode enabled using the saved credentials.  
4. Open the web chat UI in your browser at the ESP32’s STA IP address.  
5. Start chatting with OpenAI!  

## 📂 Project Structure

```text
ESP32_Copilot_Chat/
├── platformio.ini
├── README.md
├── src/
│ └── main.cpp
├── html/
│ ├── index.html
│ └── config.html
└── tools/
  └── embed_html.py
```

## 📝 Notes

- This is a demonstration project. For production use, consider:
    - More robust error handling on WiFi reconnects
    - Optional HTTPS for the web UI
