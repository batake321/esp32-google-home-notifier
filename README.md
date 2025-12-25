# esp32-google-home-notifier

Send notifications to Google Home from ESP32.

> [!WARNING]
> This library is a work in progress and has not been fully tested with Arduino IDE yet. 
> Arduino IDE での動作検証はまだ不十分です。

This is a fork of [esp8266-google-home-notifier](https://github.com/horihiro/esp8266-google-home-notifier) modified for ESP32 (ESP-IDF 5.x / Arduino Core 3.x).

## Key Changes
- Replaced `WiFiClientSecure` with ESP-IDF native `esp_tls` (MbedTLS)
- Replaced `MDNS` (ESP8266mDNS) with ESP-IDF `esp_mdns`
- Replaced dependency on Arduino String class with standard C++ `std::string`
- Added support for direct IP address connection to Google Home
- **No `Arduino.h` dependency**: Works in pure ESP-IDF environments as a component.
- **High compatibility**: Optimized for PlatformIO and ESP-IDF workflows.

## Requirement
- **ESP32** (ESP-IDF 5.x or Arduino Core 3.x)
- This library depends on Google Translate Service for TTS.
- **Config for ESP-IDF (Critical)**:
  - **Connection Timeout**: Google Home handshake can be slow. Ensure 30s timeout is allowed (default config now attempts 30s).
  - **Stack Size**: The pure ESP-IDF environment using mbedTLS requires a significant stack size.
    - If running in a separate Task, ensure at least **16384 bytes (16KB)** stack. 32KB is recommended for stability.
    - **Do NOT run in the main loop / user entrypoint** unless you configure the main task stack size to be >16KB in `sdkconfig`.
  - **Kconfig (sdkconfig)**:
    - `CONFIG_MBEDTLS_DYNAMIC_BUFFER=y` : **REQUIRED** to prevent stack overflow.
    - `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC` : **DISABLE** unless you have PSRAM.
    - `CONFIG_ESP_TLS_INSECURE=y` & `CONFIG_ESP_TLS_SKIP_SERVER_CERT_VERIFY=y` : Required for Google Home self-signed certs.
  - **Rebuild Requirements**:
    - When changing these `sdkconfig` / `menuconfig` settings, **Delete the `sdkconfig` file** in your project root and perform a **Full Clean** (e.g., `idf.py fullclean` or `ESP-IDF: Full Clean Project` in VSCode) to ensure settings are correctly propagated to the build system.


## Installation
1. Download the ZIP file from [Code] -> [Download ZIP] on GitHub.
2. In Arduino IDE, go to [Sketch] -> [Include Library] -> [Add .ZIP Library...].
3. Select the downloaded ZIP file.

## Usage

### Simple for ESP32

```cpp
#include <WiFi.h>
#include <esp32-google-home-notifier.h>

const char* ssid     = "<REPLACE_YOUR_WIFI_SSID>";
const char* password = "<REPLACE_YOUR_WIFI_PASSWORD>";

GoogleHomeNotifier ghn;

void setup() {
  Serial.begin(115200);
  Serial.println("");
  Serial.print("connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("connected.");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP()); 
  
  const char displayName[] = "Family Room";

  Serial.println("connecting to Google Home...");
  
  // Connect via mDNS discovery
  if (ghn.device(displayName, "en") != true) {
    Serial.println(ghn.getLastError());
    return;
  }
  
  // Or connect via Direct IP if mDNS fails or you know the IP
  /*
  IPAddress ghIP(192, 168, 1, 20);
  if (ghn.ip(ghIP, "en") != true) {
     Serial.println(ghn.getLastError());
     return;
  }
  */

  Serial.print("found Google Home(");
  Serial.print(ghn.getIPAddress().toString().c_str());
  Serial.print(":");
  Serial.print(ghn.getPort());
  Serial.println(")");
  
  if (ghn.notify("Hello, World!") != true) {
    Serial.println(ghn.getLastError());
    return;
  }
  Serial.println("Done.");
}

void loop() {
}
```

## Credits
Based on the excellent work by [horihiro](https://github.com/horihiro/esp8266-google-home-notifier).
