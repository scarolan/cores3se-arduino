// Saturday Morning Cartoons — M5Stack CoreS3 SE
// Plays RGB332 .bin files from SD card sequentially, looping forever.
// Double-buffered with diffDraw. NeoPixel ambient effects.
// Note: Display and SD share SPI bus — must manage bus access.

#include <SD.h>
#include <M5Unified.h>
#include <FastLED.h>
#include <vector>
#include <algorithm>

// --- Display ---
static M5GFX& lcd = M5.Display;
static LGFX_Sprite _sprites[2];
static uint8_t _flip = 0;

// --- NeoPixels ---
#define NEO_PIN  5
#define NUM_LEDS 10
static CRGB leds[NUM_LEDS];

// --- Playlist ---
static std::vector<String> playlist;
static int currentFileIdx = 0;

// --- Current clip state ---
static File clipFile;
static uint16_t frameW, frameH, numFrames, frameDurationMs;
static uint32_t frameSize;
static uint16_t currentFrame = 0;
static uint32_t lastFrameTime = 0;
static bool playing = false;

// --- Frame buffer for SD reads (avoids bus contention per-row) ---
static uint8_t* frameBuf = nullptr;

// --- Button timing ---
#define LONG_PRESS_MS 600
static uint32_t btnADownTime = 0;

// --- Audio feedback ---
static void beepSkip()    { M5.Speaker.tone(440, 80); delay(90); M5.Speaker.tone(550, 80); }
static void beepRestart() { M5.Speaker.tone(330, 100); }
static void beepPrev()    { M5.Speaker.tone(550, 80); delay(90); M5.Speaker.tone(440, 80); }
static void beepRandom()  { M5.Speaker.tone(350, 60); delay(70); M5.Speaker.tone(440, 60); delay(70); M5.Speaker.tone(550, 60); }

// --- diffDraw ---
static void diffDraw(LGFX_Sprite* sp0, LGFX_Sprite* sp1)
{
  union { std::uint32_t* s32; std::uint8_t* s; };
  union { std::uint32_t* p32; std::uint8_t* p; };
  s32 = (std::uint32_t*)sp0->getBuffer();
  p32 = (std::uint32_t*)sp1->getBuffer();

  auto width  = sp0->width();
  auto height = sp0->height();
  auto w32 = (width + 3) >> 2;
  std::int32_t y = 0;
  do {
    std::int32_t x32 = 0;
    do {
      while (s32[x32] == p32[x32] && ++x32 < w32);
      if (x32 == w32) break;
      std::int32_t xs = x32 << 2;
      while (s[xs] == p[xs]) ++xs;
      while (++x32 < w32 && s32[x32] != p32[x32]);
      std::int32_t xe = (x32 << 2) - 1;
      if (xe >= width) xe = width - 1;
      while (s[xe] == p[xe]) --xe;
      lcd.pushImage(xs, y, xe - xs + 1, 1, &s[xs]);
    } while (x32 < w32);
    s32 += w32;
    p32 += w32;
  } while (++y < height);
  lcd.display();
}

// --- Show message on screen ---
static void showMessage(const char* line1, const char* line2 = nullptr, uint8_t color = 0xFF) {
  LGFX_Sprite& sp = _sprites[_flip];
  sp.clear(0x00);
  sp.setTextSize(2);
  sp.setTextColor(color);
  sp.setCursor((sp.width() - sp.textWidth(line1)) / 2, sp.height() / 2 - 16);
  sp.print(line1);
  if (line2) {
    sp.setTextSize(1);
    sp.setTextColor(0xFF);
    sp.setCursor((sp.width() - sp.textWidth(line2)) / 2, sp.height() / 2 + 16);
    sp.print(line2);
  }
  lcd.startWrite();
  diffDraw(&_sprites[_flip], &_sprites[_flip ^ 1]);
  lcd.endWrite();
  _flip ^= 1;
}

// --- Scan SD card for .bin files ---
static void scanPlaylist() {
  playlist.clear();
  File root = SD.open("/");
  if (!root) return;

  while (File entry = root.openNextFile()) {
    if (entry.isDirectory()) { entry.close(); continue; }
    String name = String("/") + entry.name();
    if (name.endsWith(".bin") || name.endsWith(".BIN")) {
      playlist.push_back(name);
    }
    entry.close();
  }
  root.close();

  // Sort alphabetically (files are zero-padded by converter)
  std::sort(playlist.begin(), playlist.end());
}

// --- Open next clip ---
static bool openClip(int idx) {
  if (clipFile) clipFile.close();
  playing = false;

  if (idx >= (int)playlist.size()) return false;

  clipFile = SD.open(playlist[idx].c_str(), FILE_READ);
  if (!clipFile) return false;

  // Read header
  uint8_t hdr[8];
  if (clipFile.read(hdr, 8) != 8) { clipFile.close(); return false; }

  frameW         = hdr[0] | (hdr[1] << 8);
  frameH         = hdr[2] | (hdr[3] << 8);
  numFrames      = hdr[4] | (hdr[5] << 8);
  frameDurationMs = hdr[6] | (hdr[7] << 8);
  frameSize = (uint32_t)frameW * frameH;

  currentFrame = 0;
  lastFrameTime = millis();
  playing = true;

  // Show brief title card
  String fname = playlist[idx];
  // Strip path and extension
  int lastSlash = fname.lastIndexOf('/');
  if (lastSlash >= 0) fname = fname.substring(lastSlash + 1);
  if (fname.endsWith(".bin")) fname = fname.substring(0, fname.length() - 4);
  // Strip leading number prefix (e.g., "003_")
  if (fname.length() > 4 && fname[3] == '_') fname = fname.substring(4);

  char info[64];
  snprintf(info, sizeof(info), "%d of %d", idx + 1, (int)playlist.size());
  showMessage(fname.c_str(), info, 0xFC);
  delay(1500);

  return true;
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  M5.Speaker.setVolume(64);

  // Display — landscape 320x240
  lcd.setColorDepth(8);
  if (lcd.width() < lcd.height()) {
    lcd.setRotation(lcd.getRotation() ^ 1);
  }
  auto scrW = lcd.width();
  auto scrH = lcd.height();

  for (int i = 0; i < 2; i++) {
    _sprites[i].setColorDepth(8);
    _sprites[i].setTextSize(1);
    if (!_sprites[i].createSprite(scrW, scrH)) {
      _sprites[i].setPsram(true);
      _sprites[i].createSprite(scrW, scrH);
    }
  }

  // NeoPixels
  FastLED.addLeds<WS2812B, NEO_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(40);

  // Clear both sprites
  _sprites[0].clear(0x00);
  _sprites[1].clear(0x00);

  showMessage("LOADING...", "Mounting SD card");

  // Mount SD — shares SPI bus with display, so don't hold lcd.startWrite()
  uint8_t cs   = M5.getPin(m5::pin_name_t::sd_spi_cs);
  uint8_t sclk = M5.getPin(m5::pin_name_t::sd_spi_sclk);
  uint8_t mosi = M5.getPin(m5::pin_name_t::sd_spi_mosi);
  uint8_t miso = M5.getPin(m5::pin_name_t::sd_spi_miso);

  SPI.begin(sclk, miso, mosi, -1);
  delay(50);
  if (!SD.begin(cs, SPI, 15000000)) {
    showMessage("SD FAILED", "Check card and reboot", 0xE0);
    while (true) { delay(1000); }
  }

  // Allocate frame read buffer in PSRAM
  frameBuf = (uint8_t*)ps_malloc(320 * 240);

  // Scan for .bin files
  scanPlaylist();

  if (playlist.empty()) {
    showMessage("NO FILES", "Add .bin files to SD root", 0xE0);
    while (true) { delay(1000); }
  }

  char buf[32];
  snprintf(buf, sizeof(buf), "Found %d clips", (int)playlist.size());
  showMessage("READY!", buf, 0x1C);
  delay(1000);

  // Start first clip
  currentFileIdx = 0;
  openClip(currentFileIdx);
}

void loop() {
  M5.update();

  // Right button (BtnC): skip to next video
  if (M5.BtnC.wasPressed()) {
    beepSkip();
    currentFileIdx++;
    if (currentFileIdx >= (int)playlist.size()) currentFileIdx = 0;
    openClip(currentFileIdx);
    return;
  }

  // Left button (BtnA): short press = restart current, long press = previous
  if (M5.BtnA.wasPressed()) {
    btnADownTime = millis();
  }
  if (M5.BtnA.wasReleased()) {
    if (millis() - btnADownTime >= LONG_PRESS_MS) {
      beepPrev();
      currentFileIdx--;
      if (currentFileIdx < 0) currentFileIdx = (int)playlist.size() - 1;
    } else {
      beepRestart();
    }
    openClip(currentFileIdx);
    return;
  }

  // Center button (BtnB): play random video
  if (M5.BtnB.wasPressed()) {
    beepRandom();
    int r = random(0, (int)playlist.size());
    if (playlist.size() > 1) while (r == currentFileIdx) r = random(0, (int)playlist.size());
    currentFileIdx = r;
    openClip(currentFileIdx);
    return;
  }

  if (!playing) return;

  uint32_t now = millis();
  if (now - lastFrameTime < frameDurationMs) return;
  lastFrameTime = now;

  LGFX_Sprite& sp = _sprites[_flip];
  uint8_t* spBuf = (uint8_t*)sp.getBuffer();
  int spW = sp.width();
  int spH = sp.height();

  // Center the frame on screen
  int xOff = (spW - frameW) / 2;
  int yOff = (spH - frameH) / 2;

  // Clear sprite (for letterboxing if needed)
  if (xOff > 0 || yOff > 0) sp.clear(0x00);

  // Read entire frame from SD into temp buffer (SD has bus)
  uint32_t fileOffset = 8 + (uint32_t)currentFrame * frameSize;
  clipFile.seek(fileOffset);
  clipFile.read(frameBuf, frameSize);

  // Copy into sprite buffer
  if (xOff == 0 && yOff == 0 && frameW == spW && frameH == spH) {
    memcpy(spBuf, frameBuf, frameSize);
  } else {
    for (int y = 0; y < frameH && (yOff + y) < spH; y++) {
      memcpy(&spBuf[(yOff + y) * spW + xOff], &frameBuf[y * frameW], frameW);
    }
  }

  // Now push to display (display has bus)
  lcd.startWrite();
  diffDraw(&_sprites[_flip], &_sprites[_flip ^ 1]);
  lcd.endWrite();
  _flip ^= 1;

  currentFrame++;

  // End of clip — advance to next
  if (currentFrame >= numFrames) {
    currentFileIdx++;
    if (currentFileIdx >= (int)playlist.size()) {
      currentFileIdx = 0; // Loop back to start
    }
    openClip(currentFileIdx);
  }

  // NeoPixels: ambient glow that shifts with playback
  static uint8_t hue = 0;
  hue += 2;
  for (int i = 0; i < NUM_LEDS; i++)
    leds[i] = CHSV(hue + i * 25, 200, 80);
  FastLED.show();
}
