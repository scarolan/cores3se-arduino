// Bitcoin Price Tracker — M5Stack CoreS3 SE
// Fetches BTC price from Coinbase API and displays it
// Uses HTTP GET to https://api.coinbase.com/v2/exchange-rates?currency=BTC

#include <M5Unified.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// --- WiFi Configuration ---
#include "wifi_config.h"
// Credentials loaded from .env file via build_flags

// --- Hardware ---
static M5GFX& lcd = M5.Display;

// --- Display constants ---
#define SCR_W 320
#define SCR_H 240

// --- Global state ---
static float btcPriceUSD = 0.0f;
static uint32_t lastUpdate = 0;
static int wifiRetryCount = 0;
static bool connectedToWiFi = false;
static bool fetchedData = false;

// ============================================================
// WiFi connection
// ============================================================
static void connectWiFi() {
  if (connectedToWiFi) return;
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiRetryCount++;
  
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 10000) {
    delay(100);
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    connectedToWiFi = true;
    wifiRetryCount = 0;
  }
}

// ============================================================
// Fetch BTC price from Coinbase API
// Returns: USD price as float, or 0.0 on error
// ============================================================
static float fetchBTCPrice() {
  if (!connectedToWiFi) return 0.0f;
  
  HTTPClient http;
  String url = "https://api.coinbase.com/v2/exchange-rates?currency=BTC";
  
  http.begin(url);
  int httpResponseCode = http.GET();
  
  if (httpResponseCode == 200) {
    String payload = http.getString();
    
    // Parse JSON response
    // Example: {"data":{"currency":"BTC","rates":{"USD":"64238.45"}}}
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      const char* usdRate = doc["data"]["rates"]["USD"];
      if (usdRate) {
        return atof(usdRate);
      }
    }
  }
  
  http.end();
  return 0.0f;
}

// ============================================================
// Draw formatted BTC price
// ============================================================
static void drawPrice(float price, bool valid) {
  lcd.clear();
  
  // Title
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.setTextDatum(MC_DATUM);
  lcd.drawString("BITCOIN PRICE", SCR_W / 2, 30);
  
  if (!connectedToWiFi) {
    // WiFi not connected
    lcd.setTextSize(1);
    lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
    lcd.drawString("Connecting to WiFi...", SCR_W / 2, 80);
    
    int dots = (millis() / 500) % 4;
    String loading = "Connecting";
    for (int i = 0; i < dots; i++) loading += ".";
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.drawString(loading, SCR_W / 2, 110);
    
    if (wifiRetryCount > 5) {
      lcd.setTextSize(1);
      lcd.setTextColor(TFT_RED, TFT_BLACK);
      lcd.drawString("WiFi fail - check config", SCR_W / 2, 140);
      lcd.setTextSize(1);
      lcd.setTextColor(TFT_GRAY, TFT_BLACK);
      lcd.drawString("Check .env WiFi settings", SCR_W / 2, 170);
    }
    return;
  }
  
  if (!valid) {
    // Failed to fetch data
    lcd.setTextColor(TFT_ORANGE, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.drawString("Fetching price...", SCR_W / 2, 80);
    
    int dots = (millis() / 500) % 4;
    String loading = "";
    for (int i = 0; i < dots; i++) loading += ".";
    lcd.setTextColor(TFT_WHITE, TFT_BLACK);
    lcd.setTextSize(1);
    lcd.drawString("API: coinbase.com" + loading, SCR_W / 2, 110);
    
    if (millis() - lastUpdate > 30000 && lastUpdate > 0) {
      lcd.setTextColor(TFT_RED, TFT_BLACK);
      lcd.setTextSize(1);
      lcd.drawString("Fetch failed - retrying", SCR_W / 2, 140);
    }
    return;
  }
  
  // Display price
  String priceStr = "$" + String(price, 2);
  float priceFloat = (int)(price * 100) / 100.0f;
  
  lcd.setTextSize(4);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK);
  lcd.drawString(priceStr, SCR_W / 2, 120);
  
  // USD label
  lcd.setTextSize(2);
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.drawString("USD", SCR_W / 2, 165);
  
  // Last update info
  lcd.setTextSize(1);
  lcd.setTextColor(TFT_GREEN, TFT_BLACK);
  uint32_t elapsed = (millis() - lastUpdate) / 1000;
  lcd.drawString("Last: " + String(elapsed) + "s ago", SCR_W / 2, 195);
  
  // Refresh info
  lcd.setTextColor(TFT_GRAY, TFT_BLACK);
  lcd.setTextSize(1);
  lcd.drawString("Auto-refresh every 60s", SCR_W / 2, 215);
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  
  lcd.setColorDepth(8);
  if (lcd.width() < lcd.height()) {
    lcd.setRotation(lcd.getRotation() ^ 1);
  }
  
  lcd.fillScreen(TFT_BLACK);
  connectWiFi();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  M5.update();
  uint32_t now = millis();
  
  // Handle touch to force refresh
  auto touch = M5.Touch.getDetail();
  if (touch.wasPressed()) {
    lastUpdate = 0;  // Force immediate fetch
  }
  
  // Connect WiFi if needed
  if (!connectedToWiFi && wifiRetryCount < 10) {
    connectWiFi();
  }
  
  // Fetch price every 60 seconds (or immediately on first run)
  if (now - lastUpdate >= 60000 || lastUpdate == 0) {
    float newPrice = fetchBTCPrice();
    if (newPrice > 0.0f) {
      btcPriceUSD = newPrice;
      fetchedData = true;
      lastUpdate = now;
    }
    wifiRetryCount = 0;  // Reset on successful API call
  }
  
  // Draw UI
  drawPrice(btcPriceUSD, fetchedData);
  
  lcd.display();
  
  delay(100);
}
