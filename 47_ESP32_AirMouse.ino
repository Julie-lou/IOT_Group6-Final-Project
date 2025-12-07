/* -------------------------------------------------
IMPORTANT!
This code works only till ESP32 core (boards) version 3.2.0.
Downgrade if you have compilation errors.

Original project:
Arduino project by Tech Talkies YouTube Channel.
https://www.youtube.com/@techtalkies1
-------------------------------------------------*/
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>               // for current time

#include <BleMouse.h>
#include <Adafruit_MPU6050.h>

#define LEFTBUTTON 18
#define RIGHTBUTTON 19
#define SPEED 10

// ====== WIFI CONFIG ======
const char* WIFI_SSID     = "Loulay-1";
const char* WIFI_PASSWORD = "lLbVyEjLbH@!998";

// ====== INFLUXDB CONFIG ======
const char* INFLUX_HOST   = "192.168.0.108";  // update if needed
const int   INFLUX_PORT   = 8086;
const char* INFLUX_DB     = "air_mouse";

// ====== TELEGRAM VIA LOCAL BRIDGE (Python on Mac) ======
// ESP32 calls your Mac bridge at: http://<MAC_IP>:5050/telegram?msg=...
const char* BRIDGE_HOST   = "192.168.1.101";  // 👈 your Mac's IP
const int   BRIDGE_PORT   = 5050;            // 👈 same as in telegram_bridge.py

// Timezone for Cambodia (UTC+7)
const long GMT_OFFSET_SEC      = 7 * 3600;
const int  DAYLIGHT_OFFSET_SEC = 0;

Adafruit_MPU6050 mpu;
BleMouse bleMouse;

bool sleepMPU = true;
long mpuDelayMillis;

// ========== INFLUXDB SEND ==========
void sendToInflux(const String& lineProtocol) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skip Influx write");
    return;
  }

  HTTPClient http;
  String url = String("http://") + INFLUX_HOST + ":" + String(INFLUX_PORT) +
               "/write?db=" + INFLUX_DB;

  Serial.print("POST to URL: ");
  Serial.println(url);
  Serial.print("Data: ");
  Serial.println(lineProtocol);

  http.begin(url);
  http.addHeader("Content-Type", "text/plain");

  int httpCode = http.POST(lineProtocol);
  Serial.print("HTTP status: ");
  Serial.println(httpCode);   // 204 = success in InfluxDB v1

  if (httpCode <= 0) {
    Serial.print("HTTP error: ");
    Serial.println(http.errorToString(httpCode));
  } else {
    String payload = http.getString();
    Serial.print("Response: ");
    Serial.println(payload);
  }

  http.end();
}

// ========== SIMPLE URL ENCODE (spaces -> %20) ==========
String simpleUrlEncode(const String &text) {
  String encoded = "";
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    if (c == ' ') {
      encoded += "%20";
    } else {
      encoded += c;
    }
  }
  return encoded;
}

// ========== GET CURRENT TIME AS STRING ==========
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("⚠️ Failed to obtain time");
    return "time unknown";
  }

  char buffer[25];
  // Format: YYYY-MM-DD HH:MM:SS
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buffer);
}

// ========== TELEGRAM SEND THROUGH LOCAL BRIDGE ==========
void sendTelegramMessage(const String &message) {
  Serial.print("sendTelegramMessage called with: ");
  Serial.println(message);

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ WiFi not connected, skip bridge call");
    return;
  }

  HTTPClient http;

  String encoded = simpleUrlEncode(message);

  // http://<MAC_IP>:<PORT>/telegram?msg=....
  String url = String("http://") + BRIDGE_HOST + ":" + String(BRIDGE_PORT) +
               "/telegram?msg=" + encoded;

  Serial.print("Calling bridge: ");
  Serial.println(url);

  http.begin(url);
  int httpCode = http.GET();

  Serial.print("Bridge HTTP code: ");
  Serial.println(httpCode);

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.print("Bridge response: ");
    Serial.println(payload);
  } else {
    Serial.print("Bridge GET failed: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ========== WIFI RECONNECT HELPER ==========
void ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) {
    return; // already connected
  }

  Serial.println("⚠️ WiFi lost, trying to reconnect...");

  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  const unsigned long timeout = 8000; // 8 seconds

  while (WiFi.status() != WL_CONNECTED && millis() - start < timeout) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("✅ WiFi reconnected");
  } else {
    Serial.println("❌ WiFi reconnection failed");
  }
}

// ========== SETUP ==========
void setup() {
  Serial.begin(115200);

  pinMode(LEFTBUTTON, INPUT_PULLUP);
  pinMode(RIGHTBUTTON, INPUT_PULLUP);

  // ✅ Start BLE mouse ASAP so you can pair it
  bleMouse.begin();

  // ====== WIFI CONNECT ======
  WiFi.mode(WIFI_STA);  // station mode
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");

  unsigned long startAttemptTime = millis();
  const unsigned long wifiTimeout = 15000; // 15 seconds timeout

  while (WiFi.status() != WL_CONNECTED &&
         millis() - startAttemptTime < wifiTimeout) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected!");
    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    // ====== NTP TIME SYNC ======
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC,
               "pool.ntp.org", "time.nist.gov");
    Serial.print("Syncing time");

    struct tm timeinfo;
    unsigned long tStart = millis();
    while (!getLocalTime(&timeinfo) &&
           millis() - tStart < 10000) { // 10s timeout
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    if (getLocalTime(&timeinfo)) {
      Serial.print("Time synced: ");
      Serial.println(getFormattedTime());
    } else {
      Serial.println("⚠️ Time sync failed");
    }

    // 🔹 startup message
    sendTelegramMessage("ESP32 started at " + getFormattedTime());
  } else {
    Serial.println("❌ WiFi NOT connected (timeout)");
  }

  // ====== MPU6050 INIT ======
  delay(1000);
  if (!mpu.begin()) {
    Serial.println("❌ Failed to find MPU6050 chip");
    while (1) {
      delay(10); // stop everything if MPU is missing
    }
  }
  Serial.println("MPU6050 Found!");

  // Sleep MPU until Bluetooth connects
  mpu.enableSleep(sleepMPU);
}

// ========== LOOP ==========
void loop() {
  ensureWiFiConnected();

  bool connected = bleMouse.isConnected();

  // ====== MOTION (only when BLE is connected) ======
  if (connected) {
    // Wake MPU once when Bluetooth connects
    if (sleepMPU) {
      delay(3000);
      Serial.println("MPU6050 awakened!");
      sleepMPU = false;
      mpu.enableSleep(sleepMPU);
      delay(500);
    }

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // Calculate mouse movement
    float dx = g.gyro.z * -SPEED;
    float dy = g.gyro.x * -SPEED;

    // Move mouse
    bleMouse.move((int8_t)dx, (int8_t)dy);

    // Print motion to Serial
    Serial.print("Motion -> dx: ");
    Serial.print(dx, 3);
    Serial.print(" , dy: ");
    Serial.println(dy, 3);

    // 🔹 Optional: log motion to InfluxDB
    String motionLine = "motion,device=esp32 dx=" + String(dx, 3) +
                        ",dy=" + String(dy, 3);
    // sendToInflux(motionLine);
  }

  // ====== BUTTON HANDLING (ALWAYS RUNS) ======
  int rightState = digitalRead(RIGHTBUTTON); // LOW when pressed
  int leftState  = digitalRead(LEFTBUTTON);

  // Right button
  if (rightState == LOW) {
    Serial.println("Right button PRESSED");

    if (connected) {
      Serial.println("Sending BLE right click");
      bleMouse.click(MOUSE_RIGHT);
    }

    String msg = "Right button clicked at " + getFormattedTime();
    Serial.println(msg);
    sendTelegramMessage(msg);
    // sendToInflux("click,button=right value=1");

    delay(500); // debounce
  }

  // Left button
  if (leftState == LOW) {
    Serial.println("Left button PRESSED");

    if (connected) {
      Serial.println("Sending BLE left click");
      bleMouse.click(MOUSE_LEFT);
    }

    String msg = "Left button clicked at " + getFormattedTime();
    Serial.println(msg);
    sendTelegramMessage(msg);
    // sendToInflux("click,button=left value=1");

    delay(500); // debounce
  }

  delay(50);  // small pacing delay
}
