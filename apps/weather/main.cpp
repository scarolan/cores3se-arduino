#include <M5Unified.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <U8g2lib.h>
#include "config.h"
#include "weather_icons.h"

// --- U8g2 fonts via LovyanGFX wrapper ---
// _tf = full charset including degree symbol (U+00B0)
static const lgfx::U8g2font font_large(u8g2_font_helvB24_tf);   // big temperature
static const lgfx::U8g2font font_med(u8g2_font_helvR14_tf);     // description, labels
static const lgfx::U8g2font font_small(u8g2_font_helvR10_tf);   // small details
static const lgfx::U8g2font font_title(u8g2_font_helvB14_tf);   // section titles

// --- NeoPixels ---
CRGB leds[NEOPIXEL_NUM];
uint8_t neo_brightness = 0;
int8_t  neo_direction = 1;

// --- Weather data structures ---
struct CurrentWeather {
    float temp;
    float feels_like;
    float temp_min;
    float temp_max;
    int   humidity;
    float wind_speed;
    float wind_deg;
    int   pressure;
    int   visibility;
    char  description[48];
    char  icon[8];
    char  city[32];
    long  dt;
    long  sunrise;
    long  sunset;
    long  timezone;  // UTC offset in seconds from API
    bool  valid;
};

struct ForecastEntry {
    float temp_hi;
    float temp_lo;
    char  icon[8];
    char  day_str[6];  // "Mon" etc
    long  dt;
};

#define FORECAST_COUNT 5
CurrentWeather current = {};
ForecastEntry  forecast[FORECAST_COUNT] = {};
bool forecast_valid = false;

// --- Timing ---
unsigned long last_fetch_ms = 0;
unsigned long last_neo_ms = 0;

// --- Debug ---
char last_error[128] = "No fetch attempted";

// --- View state ---
enum View { VIEW_CURRENT, VIEW_FORECAST, VIEW_DETAILS };
View current_view = VIEW_CURRENT;

// --- Forward declarations ---
void connectWiFi();
bool fetchWeather();
bool fetchForecast();
void drawUI();
void drawCurrentWeather();
void drawForecastView();
void drawDetailsView();
void drawForecastStrip();
void drawStatusBar();
void updateNeoPixels();
const char* getWindDirection(float deg);
void formatTime(long epoch, char* buf, int bufLen);
void formatDayName(long epoch, char* buf, int bufLen);
int  localDay(long epoch);
void showSplash(const char* msg);

// ======================================================
// Setup
// ======================================================
void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);

    delay(2000);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);

    FastLED.addLeds<WS2812, NEOPIXEL_PIN, GRB>(leds, NEOPIXEL_NUM);
    FastLED.setBrightness(40);

    showSplash("Connecting to WiFi...");
    connectWiFi();

    if (WiFi.status() == WL_CONNECTED) {
        showSplash("Fetching weather...");
        bool ok1 = fetchWeather();
        bool ok2 = fetchForecast();
        Serial.printf("fetchWeather=%d fetchForecast=%d\n", ok1, ok2);
    } else {
        Serial.println("Skipping fetch - no WiFi");
    }

    drawUI();
    last_fetch_ms = millis();
}

// ======================================================
// Loop
// ======================================================
void loop() {
    M5.update();

    auto t = M5.Touch.getDetail();
    if (t.wasPressed()) {
        if (t.y < 200) {
            showSplash("Refreshing...");
            fetchWeather();
            fetchForecast();
            drawUI();
            last_fetch_ms = millis();
        }
    }

    if (M5.BtnA.wasPressed()) { current_view = VIEW_CURRENT;  drawUI(); }
    if (M5.BtnB.wasPressed()) { current_view = VIEW_FORECAST; drawUI(); }
    if (M5.BtnC.wasPressed()) { current_view = VIEW_DETAILS;  drawUI(); }

    if (millis() - last_fetch_ms >= WEATHER_REFRESH_MS) {
        fetchWeather();
        fetchForecast();
        drawUI();
        last_fetch_ms = millis();
    }

    if (millis() - last_neo_ms >= 30) {
        updateNeoPixels();
        last_neo_ms = millis();
    }

    delay(10);
}

// ======================================================
// WiFi
// ======================================================
void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("Connecting to %s", WIFI_SSID);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("Connected! IP: %s\n", WiFi.localIP().toString().c_str());
        snprintf(last_error, sizeof(last_error), "WiFi OK: %s", WiFi.localIP().toString().c_str());
    } else {
        Serial.printf("WiFi failed! status=%d\n", WiFi.status());
        snprintf(last_error, sizeof(last_error), "WiFi failed (status %d)", WiFi.status());
        showSplash(last_error);
    }
}

// ======================================================
// Fetch current weather
// ======================================================
bool fetchWeather() {
    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
        if (WiFi.status() != WL_CONNECTED) return false;
    }

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String("https://api.openweathermap.org/data/2.5/weather?q=")
        + OWM_CITY + "&appid=" + OWM_API_KEY + "&units=" + OWM_UNITS;

    Serial.printf("GET %s\n", url.c_str());
    http.begin(client, url);
    int code = http.GET();

    if (code <= 0) {
        snprintf(last_error, sizeof(last_error), "HTTP connect err: %d", code);
        Serial.println(last_error);
        http.end();
        return false;
    }
    if (code != 200) {
        String body = http.getString();
        snprintf(last_error, sizeof(last_error), "HTTP %d: %.60s", code, body.c_str());
        Serial.println(last_error);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();
    Serial.printf("Payload len: %d\n", payload.length());

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        snprintf(last_error, sizeof(last_error), "JSON: %s", err.c_str());
        Serial.println(last_error);
        return false;
    }

    current.temp       = doc["main"]["temp"];
    current.feels_like = doc["main"]["feels_like"];
    current.temp_min   = doc["main"]["temp_min"];
    current.temp_max   = doc["main"]["temp_max"];
    current.humidity   = doc["main"]["humidity"];
    current.pressure   = doc["main"]["pressure"];
    current.wind_speed = doc["wind"]["speed"];
    current.wind_deg   = doc["wind"]["deg"];
    current.visibility = doc["visibility"];
    current.dt         = doc["dt"];
    current.sunrise    = doc["sys"]["sunrise"];
    current.sunset     = doc["sys"]["sunset"];
    current.timezone   = doc["timezone"];  // UTC offset in seconds

    strlcpy(current.description, doc["weather"][0]["description"] | "N/A", sizeof(current.description));
    strlcpy(current.icon, doc["weather"][0]["icon"] | "01d", sizeof(current.icon));
    strlcpy(current.city, doc["name"] | "Unknown", sizeof(current.city));

    if (current.description[0] >= 'a' && current.description[0] <= 'z') {
        current.description[0] -= 32;
    }

    current.valid = true;
    Serial.printf("Weather: %.0fF %s (%s) tz=%ld\n", current.temp, current.description, current.icon, current.timezone);
    return true;
}

// ======================================================
// Fetch forecast
// ======================================================
bool fetchForecast() {
    if (WiFi.status() != WL_CONNECTED) return false;

    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String("https://api.openweathermap.org/data/2.5/forecast?q=")
        + OWM_CITY + "&appid=" + OWM_API_KEY + "&units=" + OWM_UNITS + "&cnt=40";

    http.begin(client, url);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("Forecast HTTP error: %d\n", code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("Forecast JSON error: %s\n", err.c_str());
        return false;
    }

    // Aggregate 3-hour entries into daily forecasts
    // Skip today, collect up to FORECAST_COUNT future days
    JsonArray list = doc["list"].as<JsonArray>();
    int todayDay = localDay(current.dt);
    int fc = 0;
    int prevDay = todayDay;

    for (JsonObject item : list) {
        long dt = item["dt"];
        int d = localDay(dt);
        if (d == todayDay) continue;  // skip today

        float t = item["main"]["temp"];
        float tmin = item["main"]["temp_min"];
        float tmax = item["main"]["temp_max"];

        if (d != prevDay && fc > 0) {
            // Moved to a new day, finalize previous if we have room
            if (fc >= FORECAST_COUNT) break;
        }

        if (fc == 0 || d != prevDay) {
            // Start a new day entry
            if (fc > 0 && fc >= FORECAST_COUNT) break;
            if (d != prevDay && fc > 0) {
                // previous day already stored, start next
            }
            if (fc == 0 || d != prevDay) {
                forecast[fc].dt = dt;
                forecast[fc].temp_hi = tmax;
                forecast[fc].temp_lo = tmin;
                strlcpy(forecast[fc].icon, item["weather"][0]["icon"] | "01d", sizeof(forecast[fc].icon));
                formatDayName(dt, forecast[fc].day_str, sizeof(forecast[fc].day_str));
                prevDay = d;
                fc++;
            }
        } else {
            // Same day — update hi/lo and pick midday icon
            int idx = fc - 1;
            if (tmax > forecast[idx].temp_hi) forecast[idx].temp_hi = tmax;
            if (tmin < forecast[idx].temp_lo) forecast[idx].temp_lo = tmin;
            // Prefer daytime icon (ends with 'd')
            const char* ic = item["weather"][0]["icon"] | "01d";
            if (ic[2] == 'd') {
                strlcpy(forecast[idx].icon, ic, sizeof(forecast[idx].icon));
            }
        }
    }

    forecast_valid = (fc > 0);
    Serial.printf("Forecast: %d daily entries\n", fc);
    return true;
}

// ======================================================
// Time formatting — uses timezone from API response
// ======================================================
void formatTime(long epoch, char* buf, int bufLen) {
    long local = epoch + current.timezone;
    int hour = (local / 3600) % 24;
    if (hour < 0) hour += 24;
    bool pm = hour >= 12;
    int h12 = hour % 12;
    if (h12 == 0) h12 = 12;
    snprintf(buf, bufLen, "%d%s", h12, pm ? "PM" : "AM");
}

void formatDayName(long epoch, char* buf, int bufLen) {
    long local = epoch + current.timezone;
    // Days since epoch (Jan 1 1970 was a Thursday = 4)
    int day = ((local / 86400) + 4) % 7;
    if (day < 0) day += 7;
    static const char* days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    strlcpy(buf, days[day], bufLen);
}

int localDay(long epoch) {
    long local = epoch + current.timezone;
    return (local / 86400);
}

// ======================================================
// Wind direction
// ======================================================
const char* getWindDirection(float deg) {
    static const char* dirs[] = {"N","NE","E","SE","S","SW","W","NW"};
    int idx = (int)((deg + 22.5f) / 45.0f) % 8;
    return dirs[idx];
}

// ======================================================
// Splash screen
// ======================================================
void showSplash(const char* msg) {
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setFont(&font_med);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(msg, SCREEN_W / 2, SCREEN_H / 2);
    M5.Display.setTextDatum(top_left);
}

// ======================================================
// Draw full UI
// ======================================================
void drawUI() {
    M5.Display.fillScreen(TFT_BLACK);

    switch (current_view) {
        case VIEW_CURRENT:  drawCurrentWeather(); break;
        case VIEW_FORECAST: drawForecastView();   break;
        case VIEW_DETAILS:  drawDetailsView();    break;
    }

    drawStatusBar();
}

// ======================================================
// Current weather view
// ======================================================
void drawCurrentWeather() {
    if (!current.valid) {
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setFont(&font_med);
        M5.Display.setTextColor(TFT_RED);
        M5.Display.setCursor(4, 18);
        M5.Display.println("No weather data");
        M5.Display.setFont(&font_small);
        M5.Display.setTextColor(TFT_YELLOW);
        M5.Display.println();
        M5.Display.println(last_error);
        M5.Display.println();
        M5.Display.setTextColor(TFT_DARKGREY);
        M5.Display.printf("WiFi: %s\n", WiFi.status() == WL_CONNECTED ? "connected" : "disconnected");
        M5.Display.printf("IP: %s\n", WiFi.localIP().toString().c_str());
        M5.Display.printf("SSID: %s\n", WIFI_SSID);
        M5.Display.println("\nTap screen to retry");
        return;
    }

    // Header: city + time
    M5.Display.setFont(&font_med);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setCursor(4, 16);
    M5.Display.print(current.city);

    char timebuf[12];
    formatTime(current.dt, timebuf, sizeof(timebuf));
    int tw = M5.Display.textWidth(timebuf);
    M5.Display.setCursor(SCREEN_W - tw - 4, 16);
    M5.Display.print(timebuf);

    // Divider
    M5.Display.drawLine(0, 37, SCREEN_W, 37, TFT_DARKGREY);

    // Weather icon (100x100) on left
    drawWeatherIcon(M5.Display, 6, 43, 100, current.icon);

    // Temperature (large)
    int textX = 116;
    M5.Display.setFont(&font_large);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(textX, 45);
    char tempbuf[16];
    snprintf(tempbuf, sizeof(tempbuf), "%.0f\xc2\xb0""F", current.temp);
    M5.Display.print(tempbuf);

    // Description
    M5.Display.setFont(&font_med);
    M5.Display.setTextColor(0xBDF7);
    M5.Display.setCursor(textX, 80);
    M5.Display.print(current.description);

    // High / Low
    M5.Display.setFont(&font_med);
    M5.Display.setTextColor(TFT_ORANGE);
    M5.Display.setCursor(textX, 100);
    char hlbuf[24];
    snprintf(hlbuf, sizeof(hlbuf), "H:%.0f\xc2\xb0  L:%.0f\xc2\xb0", current.temp_max, current.temp_min);
    M5.Display.print(hlbuf);

    // Divider
    M5.Display.drawLine(0, 138, SCREEN_W, 138, TFT_DARKGREY);

    // Details row below
    int detY = 150;
    M5.Display.setFont(&font_med);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(8, detY);
    M5.Display.printf("Humidity: %d%%", current.humidity);

    M5.Display.setCursor(170, detY);
    M5.Display.printf("Wind: %.0f mph %s", current.wind_speed, getWindDirection(current.wind_deg));

    M5.Display.setFont(&font_small);
    M5.Display.setTextColor(TFT_DARKGREY);
    M5.Display.setCursor(8, detY + 24);
    char flbuf[24];
    snprintf(flbuf, sizeof(flbuf), "Feels like: %.0f\xc2\xb0""F", current.feels_like);
    M5.Display.print(flbuf);
}

// ======================================================
// Forecast strip (bottom of current view)
// ======================================================
void drawForecastStrip() {
    // unused on current view now, kept for reference
}

// ======================================================
// Full forecast view (BtnB)
// ======================================================
void drawForecastView() {
    M5.Display.setFont(&font_title);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setCursor(4, 16);
    M5.Display.print("Forecast");
    M5.Display.drawLine(0, 37, SCREEN_W, 37, TFT_DARKGREY);

    if (!forecast_valid) {
        M5.Display.setFont(&font_small);
        M5.Display.setCursor(4, 44);
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.print("No forecast data");
        return;
    }

    int slotW = SCREEN_W / FORECAST_COUNT;
    int y0 = 82;

    for (int i = 0; i < FORECAST_COUNT; i++) {
        int cx = i * slotW + slotW / 2;

        // Day name
        M5.Display.setFont(&font_med);
        M5.Display.setTextColor(TFT_CYAN);
        M5.Display.setTextDatum(top_center);
        M5.Display.drawString(forecast[i].day_str, cx, y0);

        // Icon
        drawWeatherIcon(M5.Display, cx - 28, y0 + 17, 58, forecast[i].icon);

        // Hi / Lo temps
        M5.Display.setFont(&font_small);
        M5.Display.setTextColor(TFT_WHITE);
        char tbuf[16];
        snprintf(tbuf, sizeof(tbuf), "%.0f\xc2\xb0/%.0f\xc2\xb0", forecast[i].temp_hi, forecast[i].temp_lo);
        M5.Display.drawString(tbuf, cx, y0 + 68);
    }
    M5.Display.setTextDatum(top_left);
}

// ======================================================
// Details view (BtnC)
// ======================================================
void drawDetailsView() {
    if (!current.valid) {
        showSplash("No data");
        return;
    }

    M5.Display.setFont(&font_title);
    M5.Display.setTextColor(TFT_CYAN);
    M5.Display.setCursor(4, 16);
    M5.Display.print("Details");
    M5.Display.drawLine(0, 37, SCREEN_W, 37, TFT_DARKGREY);

    M5.Display.setFont(&font_small);
    int y = 45;
    int dy = 18;

    auto row = [&](const char* label, const char* value) {
        M5.Display.setTextColor(TFT_DARKGREY);
        M5.Display.setCursor(8, y);
        M5.Display.print(label);
        M5.Display.setTextColor(TFT_WHITE);
        M5.Display.setCursor(130, y);
        M5.Display.print(value);
        y += dy;
    };

    char buf[32];
    snprintf(buf, sizeof(buf), "%.1f\xc2\xb0""F", current.temp);
    row("Temperature", buf);

    snprintf(buf, sizeof(buf), "%.1f\xc2\xb0""F", current.feels_like);
    row("Feels Like", buf);

    snprintf(buf, sizeof(buf), "H:%.0f\xc2\xb0 / L:%.0f\xc2\xb0", current.temp_max, current.temp_min);
    row("High / Low", buf);

    snprintf(buf, sizeof(buf), "%d%%", current.humidity);
    row("Humidity", buf);

    snprintf(buf, sizeof(buf), "%.1f mph %s", current.wind_speed, getWindDirection(current.wind_deg));
    row("Wind", buf);

    snprintf(buf, sizeof(buf), "%d hPa", current.pressure);
    row("Pressure", buf);

    snprintf(buf, sizeof(buf), "%.1f mi", current.visibility / 1609.34f);
    row("Visibility", buf);

    char sr[8], ss[8];
    formatTime(current.sunrise, sr, sizeof(sr));
    formatTime(current.sunset, ss, sizeof(ss));
    snprintf(buf, sizeof(buf), "%s / %s", sr, ss);
    row("Sunrise/Set", buf);

    row("Conditions", current.description);
}

// ======================================================
// Status bar (bottom)
// ======================================================
void drawStatusBar() {
    int y = SCREEN_H - 14;
    M5.Display.drawLine(0, y - 4, SCREEN_W, y - 4, TFT_DARKGREY);

    M5.Display.setFont(&font_small);
    M5.Display.setTextColor(TFT_DARKGREY);

    M5.Display.setCursor(4, y);
    const char* labels[] = {"[Current]  Forecast   Details",
                            " Current  [Forecast]  Details",
                            " Current   Forecast  [Details]"};
    M5.Display.print(labels[(int)current_view]);

    M5.Display.setCursor(SCREEN_W - 30, y);
    M5.Display.setTextColor(WiFi.status() == WL_CONNECTED ? TFT_GREEN : TFT_RED);
    M5.Display.print(WiFi.status() == WL_CONNECTED ? "WiFi" : "----");
}

// ======================================================
// NeoPixel ambient (temperature-mapped color with breathe)
// ======================================================
void updateNeoPixels() {
    neo_brightness += neo_direction * 3;
    if (neo_brightness >= 200) { neo_brightness = 200; neo_direction = -1; }
    if (neo_brightness <= 20)  { neo_brightness = 20;  neo_direction = 1;  }

    CRGB color;
    if (!current.valid) {
        color = CRGB(0, 0, neo_brightness / 3);
    } else {
        float t = current.temp;
        if (t < 32)       color = CRGB(0, 0, neo_brightness);
        else if (t < 50)  color = CRGB(0, neo_brightness, neo_brightness);
        else if (t < 70)  color = CRGB(0, neo_brightness, 0);
        else if (t < 85)  color = CRGB(neo_brightness, neo_brightness, 0);
        else              color = CRGB(neo_brightness, 0, 0);
    }

    for (int i = 0; i < NEOPIXEL_NUM; i++) {
        leds[i] = color;
    }
    FastLED.show();
}
