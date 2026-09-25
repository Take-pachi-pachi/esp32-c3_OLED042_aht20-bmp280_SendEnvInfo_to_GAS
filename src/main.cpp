#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include <U8g2lib.h>
#include <Adafruit_AHTX0.h>
#include <Adafruit_BMP280.h>
#include "config.h"

// 0.42インチ OLED ピン設定 (SDA: GPIO5, SCL: GPIO6)
#define OLED_SDA 5
#define OLED_SCL 6
#define OLED_RESET U8X8_PIN_NONE

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RESET, OLED_SCL, OLED_SDA);
Adafruit_AHTX0 aht20;
Adafruit_BMP280 bmp280;
bool aht20Ready = false;
bool bmp280Ready = false;

// 画面表示用キャリブレーション値 (72x40画面用)
const int xOffset = 28;
const int yOffset = OLED_ROTATED ? 0 : 24;

long lastScheduledHourKey = -1;
int lastScheduledMinute = -1;

int getDueScheduledMinute(int currentMinute) {
  int dueMinute = -1;
  const size_t sendMinuteCount = sizeof(SEND_MINUTES) / sizeof(SEND_MINUTES[0]);
  for (size_t index = 0; index < sendMinuteCount; index++) {
    const int scheduledMinute = SEND_MINUTES[index];
    if (scheduledMinute >= 0 && scheduledMinute <= currentMinute
        && scheduledMinute > dueMinute) {
      dueMinute = scheduledMinute;
    }
  }
  return dueMinute;
}

void scanI2cDevices() {
  Serial.println("I2C scan:");
  const uint8_t addresses[] = {0x38, 0x3C, 0x77};
  bool deviceFound = false;
  for (uint8_t address : addresses) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found device at 0x%02X\n", address);
      deviceFound = true;
    }
  }
  if (!deviceFound) {
    Serial.println("  No I2C devices found");
  }
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi already connected: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");

  unsigned long startMillis = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startMillis < 30000UL) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Wi-Fi connected: ");
    Serial.println(WiFi.localIP());
    return true;
  }

  Serial.println("Wi-Fi connection failed");
  return false;
}

void disconnectWiFi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  Serial.println("Wi-Fi disconnected");
}

bool synchronizeNtp() {
  configTzTime(TIME_ZONE, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 10000)) {
    Serial.println("NTP time synchronization failed");
    return false;
  }

  Serial.printf("NTP synchronized: %04d-%02d-%02d %02d:%02d:%02d\n",
                timeInfo.tm_year + 1900,
                timeInfo.tm_mon + 1,
                timeInfo.tm_mday,
                timeInfo.tm_hour,
                timeInfo.tm_min,
                timeInfo.tm_sec);
  return true;
}

bool getInternalTimestamp(char* timestamp, size_t timestampSize) {
  struct tm timeInfo;
  if (!getLocalTime(&timeInfo, 10000)) {
    return false;
  }

  strftime(timestamp, timestampSize, "%Y-%m-%d_%H:%M", &timeInfo);
  return true;
}

bool sendMeasurement(float temperature, float humidity, float pressure) {
  char timestamp[20];
  if (!getInternalTimestamp(timestamp, sizeof(timestamp))) {
    Serial.println("ESP32 internal time is not available; measurement was not sent");
    return false;
  }

  const float discomfortIndex = 0.81F * temperature
      + 0.01F * humidity * (0.99F * temperature - 14.3F)
      + 46.3F;

  String url = String(SHEET_URL)
      + "?p7=" + SHEET_NAME
      + "&p1=" + timestamp
      + "&p2=" + String(temperature, 2)
      + "&p3=" + String(humidity, 2)
      + "&p4=" + String(pressure, 2)
      + "&p5=&p6=" + String(discomfortIndex, 2);

  Serial.println("Sending URL:");
  Serial.println(url);

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(url)) {
    Serial.println("Could not start HTTP request");
    return false;
  }

  int httpCode = http.GET();
  Serial.printf("Measurement sent: HTTP %d\n", httpCode);
  bool sent = httpCode >= 200 && httpCode < 300;
  if (httpCode > 0) {
    Serial.println(http.getString());
  }
  http.end();
  return sent;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Initial Wi-Fi connection");
  connectWiFi();
  Serial.println("Initial NTP synchronization");
  synchronizeNtp();
  disconnectWiFi();

  const size_t sendMinuteCount = sizeof(SEND_MINUTES) / sizeof(SEND_MINUTES[0]);
  Serial.print("Configured send minutes: ");
  for (size_t index = 0; index < sendMinuteCount; index++) {
    if (index > 0) {
      Serial.print(", ");
    }
    Serial.print(SEND_MINUTES[index]);
    if (SEND_MINUTES[index] < 0 || SEND_MINUTES[index] >= 60) {
      Serial.printf("SEND_MINUTES[%u] must be between 0 and 59\n",
                    static_cast<unsigned int>(index));
    }
  }
  Serial.println();

  u8g2.setDisplayRotation(OLED_ROTATED ? U8G2_R2 : U8G2_R0);
  u8g2.begin();
  u8g2.setContrast(255);

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setTimeOut(100);
  scanI2cDevices();

  aht20Ready = aht20.begin(&Wire);
  bmp280Ready = bmp280.begin(0x77);

  Serial.printf("AHT20: %s, BMP280: %s\n",
                aht20Ready ? "OK" : "NG",
                bmp280Ready ? "OK" : "NG");

  if (!aht20Ready && !bmp280Ready) {
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.clearBuffer();
    u8g2.drawStr(xOffset + 2, yOffset + 14, "Sensor Error");
    u8g2.sendBuffer();
  }
}

void loop() {
  sensors_event_t humidity;
  sensors_event_t temperature;
  char line[24];
  float ahtTemperature = NAN;
  float bmpTemperature = NAN;
  float displayTemperature = NAN;
  float rawHumidity = NAN;
  float humidityValue = NAN;
  float rawPressure = NAN;
  float pressure = NAN;

  if (aht20Ready) {
    aht20.getEvent(&humidity, &temperature);
    ahtTemperature = temperature.temperature;
    rawHumidity = humidity.relative_humidity;
    humidityValue = rawHumidity * (1.0F + HUM_OFFSET_RATE);
  }

  if (bmp280Ready) {
    bmpTemperature = bmp280.readTemperature();
    rawPressure = bmp280.readPressure() / 100.0F;
    pressure = rawPressure + PRESS_OFFSET;
  }

  if (aht20Ready && bmp280Ready) {
    displayTemperature = ((ahtTemperature + TEMP_OFFSET_aht)
        + (bmpTemperature + TEMP_OFFSET_bmp)) / 2.0F;
  } else if (aht20Ready) {
    displayTemperature = ahtTemperature + TEMP_OFFSET_aht;
  } else if (bmp280Ready) {
    displayTemperature = bmpTemperature + TEMP_OFFSET_bmp;
  }

  if (DEBUG_MODE) {
    struct tm currentTime;
    const bool hasCurrentTime = getLocalTime(&currentTime, 100);

    Serial.println();
    Serial.println("--- Sensor data ---");
    if (hasCurrentTime) {
      Serial.printf("ESP32 time: %04d-%02d-%02d %02d:%02d:%02d\n",
                    currentTime.tm_year + 1900,
                    currentTime.tm_mon + 1,
                    currentTime.tm_mday,
                    currentTime.tm_hour,
                    currentTime.tm_min,
                    currentTime.tm_sec);
    } else {
      Serial.println("ESP32 time: unavailable");
    }
    Serial.println("Temperature:");
    Serial.printf("  AHT20 raw: %.2f C\n", ahtTemperature);
    Serial.printf("  AHT20 corrected: %.2f C\n",
                  ahtTemperature + TEMP_OFFSET_aht);
    Serial.printf("  BMP280 raw: %.2f C\n", bmpTemperature);
    Serial.printf("  BMP280 corrected: %.2f C\n",
                  bmpTemperature + TEMP_OFFSET_bmp);
    Serial.printf("  Display: %.2f C\n", displayTemperature);
    Serial.println("Humidity:");
    Serial.printf("  AHT20 raw: %.2f %%\n", rawHumidity);
    Serial.printf("  AHT20 corrected: %.2f %%\n", humidityValue);
    Serial.println("Pressure:");
    Serial.printf("  BMP280 raw: %.2f hPa\n", rawPressure);
    Serial.printf("  BMP280 corrected: %.2f hPa\n", pressure);
    Serial.println("-------------------");
  }

  u8g2.setFont(u8g2_font_6x13_tr);
  u8g2.clearBuffer();

  if (!isnan(displayTemperature)) {
    snprintf(line, sizeof(line), "%.1f C", displayTemperature);
    u8g2.drawStr(xOffset + 2, yOffset + 12, line);
  } else {
    u8g2.drawStr(xOffset + 2, yOffset + 12, "NG");
  }

  if (aht20Ready) {
    snprintf(line, sizeof(line), "%.1f %%", humidityValue);
    u8g2.drawStr(xOffset + 2, yOffset + 25, line);
  } else {
    u8g2.drawStr(xOffset + 2, yOffset + 25, "NG");
  }

  if (bmp280Ready) {
    snprintf(line, sizeof(line), "%.0f hPa", pressure);
    u8g2.drawStr(xOffset + 2, yOffset + 38, line);
  } else {
    u8g2.drawStr(xOffset + 2, yOffset + 38, "NG");
  }

  u8g2.sendBuffer();

  struct tm currentTime;
  const bool hasCurrentTime = getLocalTime(&currentTime, 100);
  if (hasCurrentTime) {
    const int scheduledMinute = getDueScheduledMinute(currentTime.tm_min);
    if (scheduledMinute < 0) {
      delay(5000);
      return;
    }

    const long currentHourKey = (currentTime.tm_year + 1900L) * 1000000L
        + (currentTime.tm_mon + 1L) * 10000L
        + currentTime.tm_mday * 100L
        + currentTime.tm_hour;

    if (currentHourKey != lastScheduledHourKey
        || scheduledMinute != lastScheduledMinute) {
      lastScheduledHourKey = currentHourKey;
      lastScheduledMinute = scheduledMinute;
      Serial.println("Scheduled measurement started");

      if (!isnan(displayTemperature) && !isnan(humidityValue) && !isnan(pressure)) {
        if (!connectWiFi()) {
          Serial.println("Measurement skipped: Wi-Fi connection failed");
        } else if (!sendMeasurement(displayTemperature, humidityValue, pressure)) {
          Serial.println("Measurement failed");
        }
        disconnectWiFi();
      } else {
        Serial.printf("Measurement skipped: incomplete sensor data (temperature=%.2f, humidity=%.2f, pressure=%.2f)\n",
                      displayTemperature, humidityValue, pressure);
      }
    }
  }

  delay(5000);
}