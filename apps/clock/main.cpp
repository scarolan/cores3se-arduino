// LovyanGFX ClockSample — ported to M5Unified
// Original by lovyan03: https://github.com/lovyan03/LovyanGFX
// Analog clock with smooth hands, shadows, and digital readout
// NTP time sync added

#include <M5Unified.h>
#include <WiFi.h>
#include <time.h>

#define WIFI_SSID     "2532 Guest"
#define WIFI_PASSWORD "aloha808"
#define NTP_SERVER    "pool.ntp.org"
#define TZ_OFFSET     (-5 * 3600)  // Eastern Standard Time
#define DST_OFFSET    (3600)       // DST +1h (auto-handled by POSIX TZ)

static bool ntp_synced = false;

static M5GFX& lcd = M5.Display;
static LGFX_Sprite canvas(&lcd);
static LGFX_Sprite clockbase(&canvas);
static LGFX_Sprite needle1(&canvas);
static LGFX_Sprite shadow1(&canvas);
static LGFX_Sprite needle2(&canvas);
static LGFX_Sprite shadow2(&canvas);

static constexpr uint64_t oneday = 86400000;
static uint64_t count = rand() % oneday;
static int32_t width = 239;
static int32_t halfwidth = width >> 1;
static auto transpalette = 0;
static float zoom;

#ifdef min
#undef min
#endif

void update7Seg(int32_t hour, int32_t min);
void drawDot(int pos, int palette);
void drawClock(uint64_t time);

void setup(void)
{
    auto cfg = M5.config();
    M5.begin(cfg);

    lcd.setRotation(1);
    lcd.fillScreen(TFT_BLACK);

    zoom = (float)(std::min(lcd.width(), lcd.height())) / width;

    lcd.setPivot(lcd.width() >> 1, lcd.height() >> 1);

    canvas.setColorDepth(lgfx::palette_4bit);
    clockbase.setColorDepth(lgfx::palette_4bit);
    needle1.setColorDepth(lgfx::palette_4bit);
    shadow1.setColorDepth(lgfx::palette_4bit);
    needle2.setColorDepth(lgfx::palette_4bit);
    shadow2.setColorDepth(lgfx::palette_4bit);

    canvas.createSprite(width, width);
    clockbase.createSprite(width, width);
    needle1.createSprite(9, 119);
    shadow1.createSprite(9, 119);
    needle2.createSprite(3, 119);
    shadow2.createSprite(3, 119);

    canvas.fillScreen(transpalette);
    clockbase.fillScreen(transpalette);
    needle1.fillScreen(transpalette);
    shadow1.fillScreen(transpalette);

    clockbase.setTextFont(4);
    clockbase.setTextDatum(lgfx::middle_center);
    clockbase.fillCircle(halfwidth, halfwidth, halfwidth, 6);
    clockbase.drawCircle(halfwidth, halfwidth, halfwidth - 1, 15);
    for (int i = 1; i <= 60; ++i) {
        float rad = i * 6 * -0.0174532925;
        float cosy = -cos(rad) * (halfwidth * 10 / 11);
        float sinx = -sin(rad) * (halfwidth * 10 / 11);
        bool flg = 0 == (i % 5);
        clockbase.fillCircle(halfwidth + sinx + 1, halfwidth + cosy + 1, flg * 3 + 1, 4);
        clockbase.fillCircle(halfwidth + sinx, halfwidth + cosy, flg * 3 + 1, 12);
        if (flg) {
            cosy = -cos(rad) * (halfwidth * 10 / 13);
            sinx = -sin(rad) * (halfwidth * 10 / 13);
            clockbase.setTextColor(1);
            clockbase.drawNumber(i / 5, halfwidth + sinx + 1, halfwidth + cosy + 4);
            clockbase.setTextColor(15);
            clockbase.drawNumber(i / 5, halfwidth + sinx, halfwidth + cosy + 3);
        }
    }
    clockbase.setTextFont(7);

    needle1.setPivot(4, 100);
    shadow1.setPivot(4, 100);
    needle2.setPivot(1, 100);
    shadow2.setPivot(1, 100);

    for (int i = 6; i >= 0; --i) {
        needle1.fillTriangle(4, -16 - (i << 1), 8, needle1.height() - (i << 1), 0, needle1.height() - (i << 1), 15 - i);
        shadow1.fillTriangle(4, -16 - (i << 1), 8, shadow1.height() - (i << 1), 0, shadow1.height() - (i << 1), 1 + i);
    }
    for (int i = 0; i < 7; ++i) {
        needle1.fillTriangle(4, 16 + (i << 1), 8, needle1.height() + 32 + (i << 1), 0, needle1.height() + 32 + (i << 1), 15 - i);
        shadow1.fillTriangle(4, 16 + (i << 1), 8, shadow1.height() + 32 + (i << 1), 0, shadow1.height() + 32 + (i << 1), 1 + i);
    }
    needle1.fillTriangle(4, 32, 8, needle1.height() + 64, 0, needle1.height() + 64, 0);
    shadow1.fillTriangle(4, 32, 8, shadow1.height() + 64, 0, shadow1.height() + 64, 0);
    needle1.fillRect(0, 117, 9, 2, 15);
    shadow1.fillRect(0, 117, 9, 2, 1);
    needle1.drawFastHLine(1, 117, 7, 12);
    shadow1.drawFastHLine(1, 117, 7, 4);

    needle1.fillCircle(4, 100, 4, 15);
    shadow1.fillCircle(4, 100, 4, 1);
    needle1.drawCircle(4, 100, 4, 14);

    needle2.fillScreen(9);
    shadow2.fillScreen(3);
    needle2.drawFastVLine(1, 0, 119, 8);
    shadow2.drawFastVLine(1, 0, 119, 1);
    needle2.fillRect(0, 99, 3, 3, 8);

    lcd.startWrite();

    // Connect WiFi and sync NTP
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    // Use POSIX TZ string for US Eastern (auto DST)
    configTzTime("EST5EDT,M3.2.0,M11.1.0", NTP_SERVER);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        // Wait for NTP sync (time will jump from 1970 to current)
        struct tm ti;
        int wait = 0;
        while (!getLocalTime(&ti, 100) && wait < 30) { wait++; }
        if (ti.tm_year > 100) {  // year > 2000
            ntp_synced = true;
            Serial.printf("NTP synced: %02d:%02d:%02d\n", ti.tm_hour, ti.tm_min, ti.tm_sec);
        }
        WiFi.disconnect(true);  // save power, we have the time
        WiFi.mode(WIFI_OFF);
    }
}

void update7Seg(int32_t hour, int32_t min)
{
    int x = clockbase.getPivotX() - 69;
    int y = clockbase.getPivotY();
    clockbase.setCursor(x, y);
    clockbase.setTextColor(5);
    clockbase.print("88:88");
    clockbase.setCursor(x, y);
    clockbase.setTextColor(12);
    clockbase.printf("%02d:%02d", hour, min);
}

void drawDot(int pos, int palette)
{
    bool flg = 0 == (pos % 5);
    float rad = pos * 6 * -0.0174532925;
    float cosy = -cos(rad) * (halfwidth * 10 / 11);
    float sinx = -sin(rad) * (halfwidth * 10 / 11);
    canvas.fillCircle(halfwidth + sinx, halfwidth + cosy, flg * 3 + 1, palette);
}

void drawClock(uint64_t time)
{
    static int32_t p_min = -1;
    int32_t sec = time / 1000;
    int32_t min = sec / 60;
    if (p_min != min) {
        p_min = min;
        update7Seg(min / 60, min % 60);
    }
    clockbase.pushSprite(0, 0);

    drawDot(sec % 60, 14);
    drawDot(min % 60, 15);
    drawDot(((min / 60) * 5) % 60, 15);

    float fhour = (float)time / 120000;
    float fmin = (float)time / 10000;
    float fsec = (float)time * 6 / 1000;
    int px = canvas.getPivotX();
    int py = canvas.getPivotY();
    shadow1.pushRotateZoom(px + 2, py + 2, fhour, 1.0, 0.7, transpalette);
    shadow1.pushRotateZoom(px + 3, py + 3, fmin, 1.0, 1.0, transpalette);
    shadow2.pushRotateZoom(px + 4, py + 4, fsec, 1.0, 1.0, transpalette);
    needle1.pushRotateZoom(fhour, 1.0, 0.7, transpalette);
    needle1.pushRotateZoom(fmin, 1.0, 1.0, transpalette);
    needle2.pushRotateZoom(fsec, 1.0, 1.0, transpalette);

    canvas.pushRotateZoom(0, zoom, zoom, transpalette);
    lcd.display();
}

void loop(void)
{
    if (ntp_synced) {
        struct timeval tv;
        gettimeofday(&tv, nullptr);
        struct tm ti;
        localtime_r(&tv.tv_sec, &ti);
        count = (uint64_t)ti.tm_hour * 3600000
              + (uint64_t)ti.tm_min  * 60000
              + (uint64_t)ti.tm_sec  * 1000
              + (tv.tv_usec / 1000);
    } else {
        static uint32_t p_milli = 0;
        uint32_t milli = lgfx::millis() % 1000;
        if (p_milli > milli) count += 1000 + (milli - p_milli);
        else                 count += (milli - p_milli);
        p_milli = milli;
    }

    int32_t tmp = (count % 1000) >> 3;
    canvas.setPaletteColor(8, 255 - (tmp >> 1), 255 - (tmp >> 1), 200 - tmp);

    if (count > oneday) { count -= oneday; }
    drawClock(count);
}
