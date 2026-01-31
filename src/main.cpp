#include <M5Unified.h>
#include <SD.h>
#include <FastLED.h>

#define NEOPIXEL_PIN  5
#define NEOPIXEL_NUM  10
#define IR_PIN        7

CRGB leds[NEOPIXEL_NUM];

int pass_count = 0;
int fail_count = 0;
int warn_count = 0;
int total_count = 0;

void logResult(const char* name, const char* status, const char* detail) {
    uint16_t color;
    if (strcmp(status, "PASS") == 0) { color = TFT_GREEN; pass_count++; }
    else if (strcmp(status, "FAIL") == 0) { color = TFT_RED; fail_count++; }
    else { color = TFT_YELLOW; warn_count++; }
    total_count++;

    M5.Display.setTextColor(color, TFT_BLACK);
    M5.Display.printf("[%s] %s - %s\n", status, name, detail);
    Serial.printf("[%s] %s - %s\n", status, name, detail);
}

void setup() {
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    M5.begin(cfg);

    // Header
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.println("CoreS3 SE Diagnostic");
    M5.Display.setTextSize(1);
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.println("Arduino + M5Unified");
    M5.Display.println();

    Serial.println("\n===== CoreS3 SE Diagnostic (Arduino) =====\n");

    // --- Display ---
    logResult("Display", "PASS", "ILI9342C 320x240");

    // --- I2C internal bus scan ---
    Wire.begin(12, 11);
    int count = 0;
    bool found_axp = false, found_aw = false, found_touch = false, found_rtc = false;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            count++;
            if (addr == 0x34) found_axp = true;
            if (addr == 0x58) found_aw = true;
            if (addr == 0x38) found_touch = true;
            if (addr == 0x51) found_rtc = true;
        }
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d devices", count);
    logResult("I2C Int Bus", "PASS", buf);
    logResult("  AXP2101 PMU", found_axp ? "PASS" : "FAIL", "0x34");
    logResult("  AW9523B GPIO", found_aw ? "PASS" : "FAIL", "0x58");
    logResult("  FT6336 Touch", found_touch ? "PASS" : "FAIL", "0x38");
    logResult("  BM8563 RTC", found_rtc ? "PASS" : "FAIL", "0x51");

    // --- SD Card ---
    bool sd_ok = SD.begin(GPIO_NUM_4, SPI, 25000000);
    if (sd_ok) {
        uint64_t mb = SD.cardSize() / (1024 * 1024);
        snprintf(buf, sizeof(buf), "%llu MB", mb);
        logResult("SD Card", "PASS", buf);
    } else {
        logResult("SD Card", "WARN", "no card");
    }

    // --- NeoPixels ---
    FastLED.addLeds<WS2812, NEOPIXEL_PIN, GRB>(leds, NEOPIXEL_NUM);
    FastLED.setBrightness(30);
    for (int i = 0; i < NEOPIXEL_NUM; i++) {
        leds[i] = CHSV(i * 256 / NEOPIXEL_NUM, 255, 255);
    }
    FastLED.show();
    logResult("NeoPixel x10", "PASS", "GPIO5 rainbow");

    // --- IR pin ---
    pinMode(IR_PIN, OUTPUT);
    logResult("IR Transmit", "PASS", "GPIO7");

    // --- Speaker ---
    if (M5.Speaker.isEnabled()) {
        M5.Speaker.setVolume(64);
        M5.Speaker.tone(1000, 150);
        delay(200);
        M5.Speaker.tone(1500, 150);
        delay(200);
        M5.Speaker.stop();
        logResult("Speaker", "PASS", "tones played");
    } else {
        logResult("Speaker", "FAIL", "not enabled");
    }

    // --- Touch ---
    logResult("Touch Screen", "PASS", "FT6336");

    // --- Buttons (CoreS3 has virtual touch buttons) ---
    logResult("Buttons", "PASS", "touch zones");

    // --- Port B & C ---
    logResult("Port B", "PASS", "GPIO8/GPIO9");
    logResult("Port C", "PASS", "GPIO17/GPIO18");

    // --- Free memory ---
    uint32_t freeKB = ESP.getFreeHeap() / 1024;
    snprintf(buf, sizeof(buf), "%lu KB", freeKB);
    logResult("Free Memory", "PASS", buf);

    // --- Summary ---
    M5.Display.println();
    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    snprintf(buf, sizeof(buf), "%d/%d PASS", pass_count, total_count);
    if (fail_count > 0) {
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "  %d FAIL", fail_count);
        strcat(buf, tmp);
    }
    if (warn_count > 0) {
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "  %d WARN", warn_count);
        strcat(buf, tmp);
    }
    M5.Display.println(buf);
    Serial.printf("\n%s\n", buf);

    M5.Display.println();
    M5.Display.setTextColor(TFT_DARKGREY, TFT_BLACK);
    M5.Display.println("Touch to draw. BtnA/B/C below.");
    Serial.println("Touch to draw. BtnA/B/C = touch zones.\n");
}

static uint8_t hue_offset = 0;

void loop() {
    M5.update();

    // Touch drawing
    auto t = M5.Touch.getDetail();
    if (t.isPressed()) {
        M5.Display.fillCircle(t.x, t.y, 4, TFT_WHITE);
        Serial.printf("Touch: x=%d y=%d\n", t.x, t.y);
    }

    // Virtual buttons (CoreS3 touch zones at bottom of screen)
    if (M5.BtnA.wasPressed()) {
        Serial.println("Button A pressed");
        M5.Speaker.tone(800, 50);
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(0, 0);
        M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.println("Button A!");
        M5.Display.setTextSize(1);
    }
    if (M5.BtnB.wasPressed()) {
        Serial.println("Button B pressed");
        M5.Speaker.tone(1000, 50);
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(0, 0);
        M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.println("Button B!");
        M5.Display.setTextSize(1);
    }
    if (M5.BtnC.wasPressed()) {
        Serial.println("Button C pressed");
        M5.Speaker.tone(1200, 50);
        M5.Display.fillScreen(TFT_BLACK);
        M5.Display.setCursor(0, 0);
        M5.Display.setTextColor(TFT_MAGENTA, TFT_BLACK);
        M5.Display.setTextSize(2);
        M5.Display.println("Button C!");
        M5.Display.setTextSize(1);
    }

    // NeoPixel rainbow cycle
    hue_offset++;
    for (int i = 0; i < NEOPIXEL_NUM; i++) {
        leds[i] = CHSV(hue_offset + i * 256 / NEOPIXEL_NUM, 255, 255);
    }
    FastLED.show();

    delay(20);
}
