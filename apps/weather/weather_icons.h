#pragma once
#include <M5GFX.h>

// Color palette
#define IC_SUN      0xFEA0  // warm yellow
#define IC_MOON     0xC618  // light grey
#define IC_CLOUD    0xCE79  // grey-white
#define IC_CLOUD_DK 0x9492  // darker cloud
#define IC_RAIN     0x5DDF  // blue
#define IC_SNOW     0xFFFF  // white
#define IC_BOLT     0xFFE0  // yellow
#define IC_FOG      0xA514  // mid grey

// --- Helper shapes ---

static void drawSun(M5GFX& gfx, int cx, int cy, int r) {
    gfx.fillCircle(cx, cy, r, IC_SUN);
    // 8 rays
    for (int i = 0; i < 8; i++) {
        float a = i * PI / 4.0f;
        int x1 = cx + cos(a) * (r + 2);
        int y1 = cy + sin(a) * (r + 2);
        int x2 = cx + cos(a) * (r + r / 2 + 2);
        int y2 = cy + sin(a) * (r + r / 2 + 2);
        gfx.drawLine(x1, y1, x2, y2, IC_SUN);
    }
}

static void drawMoon(M5GFX& gfx, int cx, int cy, int r) {
    gfx.fillCircle(cx, cy, r, IC_MOON);
    // Crescent cutout
    gfx.fillCircle(cx + r / 2, cy - r / 3, r * 2 / 3, TFT_BLACK);
}

static void drawCloud(M5GFX& gfx, int cx, int cy, int w, uint16_t color = IC_CLOUD) {
    int h = w / 3;
    // Base ellipse
    gfx.fillEllipse(cx, cy, w / 2, h / 2, color);
    // Top bumps
    gfx.fillCircle(cx - w / 5, cy - h / 2, h * 2 / 3, color);
    gfx.fillCircle(cx + w / 6, cy - h / 2 - 2, h / 2, color);
}

static void drawRainDrops(M5GFX& gfx, int cx, int cy, int w, int count) {
    int spacing = w / (count + 1);
    int startX = cx - w / 2 + spacing;
    for (int i = 0; i < count; i++) {
        int x = startX + i * spacing;
        gfx.drawLine(x, cy, x - 2, cy + 6, IC_RAIN);
        gfx.drawLine(x + 1, cy, x - 1, cy + 6, IC_RAIN);
    }
}

static void drawSnowflakes(M5GFX& gfx, int cx, int cy, int w, int count) {
    int spacing = w / (count + 1);
    int startX = cx - w / 2 + spacing;
    for (int i = 0; i < count; i++) {
        int x = startX + i * spacing;
        int y = cy + (i % 2) * 4;
        int s = 3;
        gfx.drawLine(x - s, y, x + s, y, IC_SNOW);
        gfx.drawLine(x, y - s, x, y + s, IC_SNOW);
        gfx.drawLine(x - 2, y - 2, x + 2, y + 2, IC_SNOW);
        gfx.drawLine(x + 2, y - 2, x - 2, y + 2, IC_SNOW);
    }
}

static void drawLightning(M5GFX& gfx, int cx, int cy) {
    // Simple bolt shape
    gfx.fillTriangle(cx - 2, cy, cx + 6, cy, cx, cy + 8, IC_BOLT);
    gfx.fillTriangle(cx - 4, cy + 6, cx + 4, cy + 6, cx - 1, cy + 14, IC_BOLT);
}

// --- Main icon dispatcher ---

static void drawWeatherIcon(M5GFX& gfx, int x, int y, int size, const char* iconCode) {
    if (!iconCode || strlen(iconCode) < 2) return;

    int cx = x + size / 2;
    int cy = y + size / 2;
    int r = size / 5;       // sun/moon radius
    int cw = size * 3 / 5;  // cloud width
    bool night = (iconCode[2] == 'n');

    char code[4] = {iconCode[0], iconCode[1], 0, 0};

    if (strcmp(code, "01") == 0) {
        // Clear
        if (night) drawMoon(gfx, cx, cy, r + 4);
        else       drawSun(gfx, cx, cy, r);
    }
    else if (strcmp(code, "02") == 0) {
        // Few clouds
        if (night) drawMoon(gfx, cx - size / 6, cy - size / 6, r);
        else       drawSun(gfx, cx - size / 6, cy - size / 6, r - 2);
        drawCloud(gfx, cx + size / 8, cy + size / 8, cw);
    }
    else if (strcmp(code, "03") == 0) {
        // Scattered clouds
        drawCloud(gfx, cx, cy, cw);
    }
    else if (strcmp(code, "04") == 0) {
        // Overcast / broken clouds
        drawCloud(gfx, cx - size / 8, cy - size / 10, cw * 3 / 4, IC_CLOUD_DK);
        drawCloud(gfx, cx + size / 8, cy + size / 10, cw);
    }
    else if (strcmp(code, "09") == 0) {
        // Shower rain
        drawCloud(gfx, cx, cy - size / 6, cw);
        drawRainDrops(gfx, cx, cy + size / 6, cw, 4);
    }
    else if (strcmp(code, "10") == 0) {
        // Rain (sun + cloud + rain)
        if (night) drawMoon(gfx, cx - size / 5, cy - size / 4, r - 2);
        else       drawSun(gfx, cx - size / 5, cy - size / 4, r - 3);
        drawCloud(gfx, cx + size / 10, cy - size / 10, cw);
        drawRainDrops(gfx, cx + size / 10, cy + size / 5, cw, 3);
    }
    else if (strcmp(code, "11") == 0) {
        // Thunderstorm
        drawCloud(gfx, cx, cy - size / 5, cw, IC_CLOUD_DK);
        drawLightning(gfx, cx, cy + size / 8);
    }
    else if (strcmp(code, "13") == 0) {
        // Snow
        drawCloud(gfx, cx, cy - size / 6, cw);
        drawSnowflakes(gfx, cx, cy + size / 5, cw, 3);
    }
    else if (strcmp(code, "50") == 0) {
        // Mist / fog — horizontal wavy lines
        for (int i = 0; i < 5; i++) {
            int ly = cy - size / 4 + i * (size / 6);
            int lx = cx - size / 3;
            int lw = size * 2 / 3;
            for (int px = 0; px < lw; px++) {
                int py = ly + (int)(sin(px * 0.3f) * 2);
                gfx.drawPixel(lx + px, py, IC_FOG);
                gfx.drawPixel(lx + px, py + 1, IC_FOG);
            }
        }
    }
    else {
        // Unknown — draw question mark
        gfx.setTextColor(TFT_WHITE);
        gfx.setTextSize(3);
        gfx.drawString("?", cx - 8, cy - 12);
    }
}
