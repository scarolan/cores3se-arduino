// Generative Art Frame — M5Stack CoreS3 SE
// 4 visual modes: Plasma, Flow Field Particles, Moiré Rings, Cellular Drift
// Touch to cycle modes. NeoPixels ambient glow. Runs forever as desk art.

#include <M5Unified.h>
#include <FastLED.h>

// --- Hardware ---
static M5GFX& lcd = M5.Display;
static LGFX_Sprite _sprites[2];
static uint8_t _flip = 0;

#define NEO_PIN  5
#define NUM_LEDS 10
static CRGB leds[NUM_LEDS];

// --- Display constants ---
#define SCR_W 320
#define SCR_H 240

// --- LUTs (SRAM) ---
static uint8_t sinLUT[256];          // sin*127+128
static uint8_t fadeLUT[256];         // RGB332 fade for particle trails

// --- Palette system ---
static uint8_t currentPalette[256];  // RGB332
static uint8_t palA[256], palB[256]; // source palettes for cross-fade
static float palBlend = 0.0f;
static int palIdxA = 0, palIdxB = 1;
#define NUM_PALETTES 6

// --- Distance LUT (PSRAM) ---
static uint8_t* distLUT = nullptr;   // SCR_W * SCR_H

// --- Cellular automaton buffers (PSRAM) ---
#define CA_W 160
#define CA_H 120
static uint8_t* caA = nullptr;
static uint8_t* caB = nullptr;

// --- Particles ---
#define MAX_PARTICLES 500
struct Particle {
  float x, y, vx, vy;
};
static Particle particles[MAX_PARTICLES];

// --- Mode management ---
enum Mode { MODE_PLASMA = 0, MODE_PARTICLES, MODE_MOIRE, MODE_CELLULAR, MODE_COUNT };
static Mode currentMode = MODE_PLASMA;
static uint32_t modeStartTime = 0;
static uint32_t modeDuration = 0;   // ms
static bool transitioning = false;
static int transPhase = 0;          // 0=fade out, 1=fade in
static uint32_t transStart = 0;
#define TRANS_DURATION 1000          // ms per fade phase

// --- Timing ---
static uint32_t frameCount = 0;
static float timeS = 0.0f;

// --- NeoPixel smoothing ---
static uint8_t neoR[NUM_LEDS], neoG[NUM_LEDS], neoB[NUM_LEDS];

// ============================================================
// RGB332 helpers
// ============================================================
static inline uint8_t rgb332(uint8_t r, uint8_t g, uint8_t b) {
  return (r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6);
}

static inline void rgb332_unpack(uint8_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
  r = c & 0xE0;
  g = (c & 0x1C) << 3;
  b = (c & 0x03) << 6;
}

// Dim an RGB332 color (multiply each channel, keep format)
static inline uint8_t rgb332_dim(uint8_t c, uint8_t factor) {
  // factor 0-255
  uint8_t r = c & 0xE0;
  uint8_t g = (c & 0x1C) << 3;
  uint8_t b = (c & 0x03) << 6;
  r = ((uint16_t)r * factor) >> 8;
  g = ((uint16_t)g * factor) >> 8;
  b = ((uint16_t)b * factor) >> 8;
  return (r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6);
}

// ============================================================
// Palette generation
// ============================================================
static void generatePalette(uint8_t* pal, int idx) {
  // Each palette: base hue, hue range, saturation range, value range
  struct PalDef { float hBase, hRange, sMin, sMax, vMin, vMax; };
  static const PalDef defs[NUM_PALETTES] = {
    { 0,    40,  0.7f, 1.0f, 0.4f, 1.0f },  // ember
    { 160, 60,  0.5f, 0.9f, 0.3f, 1.0f },   // ocean
    { 80,  120, 0.6f, 1.0f, 0.3f, 1.0f },    // aurora
    { 10,  50,  0.7f, 1.0f, 0.5f, 1.0f },    // sunset
    { 200, 160, 0.8f, 1.0f, 0.5f, 1.0f },    // neon
    { 100, 40,  0.4f, 0.8f, 0.3f, 0.9f },    // moss
  };
  const PalDef& d = defs[idx % NUM_PALETTES];

  for (int i = 0; i < 256; i++) {
    float t = i / 255.0f;
    // Sine-based variation for smooth cycling
    float h = fmodf(d.hBase + d.hRange * sinf(t * M_PI * 2.0f), 360.0f);
    if (h < 0) h += 360.0f;
    float s = d.sMin + (d.sMax - d.sMin) * (0.5f + 0.5f * sinf(t * M_PI * 3.0f));
    float v = d.vMin + (d.vMax - d.vMin) * (0.5f + 0.5f * cosf(t * M_PI * 2.5f));

    // HSV to RGB
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rf, gf, bf;
    if (h < 60)       { rf = c; gf = x; bf = 0; }
    else if (h < 120) { rf = x; gf = c; bf = 0; }
    else if (h < 180) { rf = 0; gf = c; bf = x; }
    else if (h < 240) { rf = 0; gf = x; bf = c; }
    else if (h < 300) { rf = x; gf = 0; bf = c; }
    else               { rf = c; gf = 0; bf = x; }

    uint8_t r8 = (uint8_t)((rf + m) * 255);
    uint8_t g8 = (uint8_t)((gf + m) * 255);
    uint8_t b8 = (uint8_t)((bf + m) * 255);
    pal[i] = rgb332(r8, g8, b8);
  }
}

static void updatePalette() {
  // Cross-fade between palA and palB
  palBlend += 0.00055f; // ~30s for full transition at 60fps
  if (palBlend >= 1.0f) {
    palBlend = 0.0f;
    palIdxA = palIdxB;
    palIdxB = (palIdxB + 1) % NUM_PALETTES;
    memcpy(palA, currentPalette, 256);
    generatePalette(palB, palIdxB);
  }

  uint8_t blendB = (uint8_t)(palBlend * 255);
  uint8_t blendA = 255 - blendB;
  for (int i = 0; i < 256; i++) {
    uint8_t rA, gA, bA, rB, gB, bB;
    rgb332_unpack(palA[i], rA, gA, bA);
    rgb332_unpack(palB[i], rB, gB, bB);
    uint8_t r = ((uint16_t)rA * blendA + (uint16_t)rB * blendB) >> 8;
    uint8_t g = ((uint16_t)gA * blendA + (uint16_t)gB * blendB) >> 8;
    uint8_t b = ((uint16_t)bA * blendA + (uint16_t)bB * blendB) >> 8;
    currentPalette[i] = rgb332(r, g, b);
  }
}

// ============================================================
// LUT initialization
// ============================================================
static void initLUTs() {
  // Sin LUT
  for (int i = 0; i < 256; i++) {
    sinLUT[i] = (uint8_t)(127.0f * sinf(i * M_PI * 2.0f / 256.0f) + 128);
  }

  // Fade LUT — dim each RGB332 color to ~85%
  for (int i = 0; i < 256; i++) {
    fadeLUT[i] = rgb332_dim(i, 216);
  }

  // Distance LUT (from center) — PSRAM
  distLUT = (uint8_t*)ps_malloc(SCR_W * SCR_H);
  if (distLUT) {
    int cx = SCR_W / 2, cy = SCR_H / 2;
    for (int y = 0; y < SCR_H; y++) {
      for (int x = 0; x < SCR_W; x++) {
        int dx = x - cx, dy = y - cy;
        float d = sqrtf(dx * dx + dy * dy);
        distLUT[y * SCR_W + x] = (uint8_t)((int)d & 0xFF);
      }
    }
  }
}

// ============================================================
// Perlin-ish noise (simple hash-based, good enough for art)
// ============================================================
static const uint8_t perm[256] = {
  151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,
  140,36,103,30,69,142,8,99,37,240,21,10,23,190,6,148,
  247,120,234,75,0,26,197,62,94,252,219,203,117,35,11,32,
  57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,
  74,165,71,134,139,48,27,166,77,146,158,231,83,111,229,122,
  60,211,133,230,220,105,92,41,55,46,245,40,244,102,143,54,
  65,25,63,161,1,216,80,73,209,76,132,187,208,89,18,169,
  200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,
  52,217,226,250,124,123,5,202,38,147,118,126,255,82,85,212,
  207,206,59,227,47,16,58,17,182,189,28,42,223,183,170,213,
  119,248,152,2,44,154,163,70,221,153,101,155,167,43,172,9,
  129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,
  218,246,97,228,251,34,242,193,238,210,144,12,191,179,162,241,
  81,51,145,235,249,14,239,107,49,192,214,31,181,199,106,157,
  184,84,204,176,115,121,50,45,127,4,150,254,138,236,205,93,
  222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

static inline float fade(float t) { return t * t * t * (t * (t * 6 - 15) + 10); }
static inline float lerpf(float a, float b, float t) { return a + t * (b - a); }
static inline float grad(uint8_t h, float x, float y) {
  switch (h & 3) {
    case 0: return  x + y;
    case 1: return -x + y;
    case 2: return  x - y;
    default: return -x - y;
  }
}

static float noise2d(float x, float y) {
  int xi = (int)floorf(x) & 255;
  int yi = (int)floorf(y) & 255;
  float xf = x - floorf(x);
  float yf = y - floorf(y);
  float u = fade(xf);
  float v = fade(yf);
  uint8_t aa = perm[(perm[xi] + yi) & 255];
  uint8_t ab = perm[(perm[xi] + yi + 1) & 255];
  uint8_t ba = perm[(perm[(xi + 1) & 255] + yi) & 255];
  uint8_t bb = perm[(perm[(xi + 1) & 255] + yi + 1) & 255];
  return lerpf(lerpf(grad(aa, xf, yf), grad(ba, xf - 1, yf), u),
               lerpf(grad(ab, xf, yf - 1), grad(bb, xf - 1, yf - 1), u), v);
}

// ============================================================
// Particle init
// ============================================================
static void initParticles() {
  for (int i = 0; i < MAX_PARTICLES; i++) {
    particles[i].x = random(0, SCR_W);
    particles[i].y = random(0, SCR_H);
    particles[i].vx = 0;
    particles[i].vy = 0;
  }
}

// ============================================================
// Cellular automaton init
// ============================================================
static void initCA() {
  if (!caA) caA = (uint8_t*)ps_malloc(CA_W * CA_H);
  if (!caB) caB = (uint8_t*)ps_malloc(CA_W * CA_H);
  // Random seed
  for (int i = 0; i < CA_W * CA_H; i++) {
    caA[i] = random(0, 256);
  }
  memcpy(caB, caA, CA_W * CA_H);
}

// ============================================================
// Mode: Plasma Waves
// ============================================================
static void renderPlasma(uint8_t* buf) {
  uint16_t t1 = (uint16_t)(timeS * 47.0f);  // irrational-ish ratios
  uint16_t t2 = (uint16_t)(timeS * 31.0f);
  uint16_t t3 = (uint16_t)(timeS * 23.0f);
  uint16_t t4 = (uint16_t)(timeS * 17.0f);

  for (int y = 0; y < SCR_H; y++) {
    uint8_t sy = sinLUT[(y + t2) & 0xFF];
    for (int x = 0; x < SCR_W; x++) {
      uint8_t v = sinLUT[(x + t1) & 0xFF];
      v += sy;
      v += sinLUT[((x + y + t3) >> 1) & 0xFF];
      if (distLUT) v += distLUT[y * SCR_W + x] + t4;
      v = sinLUT[v & 0xFF]; // final pass through sin for smoothing
      buf[y * SCR_W + x] = currentPalette[v];
    }
  }
}

// ============================================================
// Mode: Flow Field Particles
// ============================================================
static void renderParticles(uint8_t* buf) {
  // Fade existing trails
  int total = SCR_W * SCR_H;
  for (int i = 0; i < total; i++) {
    buf[i] = fadeLUT[buf[i]];
  }

  float noiseScale = 0.008f;
  float noiseZ = timeS * 0.15f;

  for (int i = 0; i < MAX_PARTICLES; i++) {
    Particle& p = particles[i];

    // Sample noise field for velocity
    float angle = noise2d(p.x * noiseScale, p.y * noiseScale + noiseZ) * M_PI * 4.0f;
    p.vx = cosf(angle) * 1.5f;
    p.vy = sinf(angle) * 1.5f;

    p.x += p.vx;
    p.y += p.vy;

    // Wrap around
    if (p.x < 0) p.x += SCR_W;
    if (p.x >= SCR_W) p.x -= SCR_W;
    if (p.y < 0) p.y += SCR_H;
    if (p.y >= SCR_H) p.y -= SCR_H;

    // Color by position in field
    uint8_t cidx = (uint8_t)((int)(p.x + p.y * 0.7f + timeS * 20) & 0xFF);
    int px = (int)p.x;
    int py = (int)p.y;
    if (px >= 0 && px < SCR_W && py >= 0 && py < SCR_H) {
      buf[py * SCR_W + px] = currentPalette[cidx];
    }
  }
}

// ============================================================
// Mode: Moiré Ring Interference
// ============================================================
static void renderMoire(uint8_t* buf) {
  // 4 ring sources on Lissajous paths
  struct Source { float x, y; };
  Source src[4];
  for (int i = 0; i < 4; i++) {
    float phase = i * M_PI * 0.5f;
    float freqX = 0.3f + i * 0.17f;
    float freqY = 0.23f + i * 0.13f;
    src[i].x = SCR_W * 0.5f + SCR_W * 0.35f * sinf(timeS * freqX + phase);
    src[i].y = SCR_H * 0.5f + SCR_H * 0.35f * cosf(timeS * freqY + phase * 1.3f);
  }

  uint8_t palShift = (uint8_t)(timeS * 30.0f); // palette rotation

  for (int y = 0; y < SCR_H; y++) {
    for (int x = 0; x < SCR_W; x++) {
      uint16_t sum = 0;
      for (int i = 0; i < 4; i++) {
        // Octagonal distance approximation (no sqrt)
        int dx = abs(x - (int)src[i].x);
        int dy = abs(y - (int)src[i].y);
        int mn = dx < dy ? dx : dy;
        int mx = dx > dy ? dx : dy;
        uint8_t d = (uint8_t)((mx + (mn >> 1)) & 0xFF);
        sum += d;
      }
      buf[y * SCR_W + x] = currentPalette[(uint8_t)(sum + palShift)];
    }
  }
}

// ============================================================
// Mode: Cellular Drift
// ============================================================
static void renderCellular(uint8_t* buf) {
  if (!caA || !caB) return;

  // Step CA
  uint8_t* src = caA;
  uint8_t* dst = caB;

  for (int y = 1; y < CA_H - 1; y++) {
    for (int x = 1; x < CA_W - 1; x++) {
      // Weighted neighbor average
      uint16_t sum = (uint16_t)src[(y-1)*CA_W + x-1] + (uint16_t)src[(y-1)*CA_W + x]*2 + (uint16_t)src[(y-1)*CA_W + x+1]
                   + (uint16_t)src[y*CA_W + x-1]*2     + (uint16_t)src[y*CA_W + x]*4     + (uint16_t)src[y*CA_W + x+1]*2
                   + (uint16_t)src[(y+1)*CA_W + x-1] + (uint16_t)src[(y+1)*CA_W + x]*2 + (uint16_t)src[(y+1)*CA_W + x+1];
      uint8_t avg = sum >> 4; // divide by 16

      // Nonlinear S-curve reaction: push away from middle
      int v = avg;
      if (v > 128) v = v + ((v - 128) >> 2);
      else         v = v - ((128 - v) >> 2);
      if (v > 255) v = 255;
      if (v < 0) v = 0;
      dst[y * CA_W + x] = (uint8_t)v;
    }
  }

  // Random perturbation
  if (random(0, 10) == 0) {
    int rx = random(4, CA_W - 4);
    int ry = random(4, CA_H - 4);
    for (int dy = -3; dy <= 3; dy++)
      for (int dx = -3; dx <= 3; dx++)
        dst[(ry + dy) * CA_W + rx + dx] = random(0, 256);
  }

  // Swap buffers
  uint8_t* tmp = caA;
  caA = dst;
  caB = tmp;

  // Upscale 2x to sprite buffer
  for (int y = 0; y < CA_H; y++) {
    int sy = y * 2;
    for (int x = 0; x < CA_W; x++) {
      uint8_t c = currentPalette[caA[y * CA_W + x]];
      int sx = x * 2;
      buf[sy * SCR_W + sx] = c;
      buf[sy * SCR_W + sx + 1] = c;
      buf[(sy + 1) * SCR_W + sx] = c;
      buf[(sy + 1) * SCR_W + sx + 1] = c;
    }
  }
}

// ============================================================
// diffDraw — push only changed pixels
// ============================================================
static void diffDraw(LGFX_Sprite* sp0, LGFX_Sprite* sp1) {
  union { uint32_t* s32; uint8_t* s; };
  union { uint32_t* p32; uint8_t* p; };
  s32 = (uint32_t*)sp0->getBuffer();
  p32 = (uint32_t*)sp1->getBuffer();

  auto width  = sp0->width();
  auto height = sp0->height();
  auto w32 = (width + 3) >> 2;
  int32_t y = 0;
  do {
    int32_t x32 = 0;
    do {
      while (s32[x32] == p32[x32] && ++x32 < w32);
      if (x32 == w32) break;
      int32_t xs = x32 << 2;
      while (s[xs] == p[xs]) ++xs;
      while (++x32 < w32 && s32[x32] != p32[x32]);
      int32_t xe = (x32 << 2) - 1;
      if (xe >= width) xe = width - 1;
      while (s[xe] == p[xe]) --xe;
      lcd.pushImage(xs, y, xe - xs + 1, 1, &s[xs]);
    } while (x32 < w32);
    s32 += w32;
    p32 += w32;
  } while (++y < height);
  lcd.display();
}

// ============================================================
// Transition helpers
// ============================================================
static void startTransition() {
  transitioning = true;
  transPhase = 0;
  transStart = millis();
}

static void applyBrightness(uint8_t* buf, uint8_t brightness) {
  int total = SCR_W * SCR_H;
  for (int i = 0; i < total; i++) {
    buf[i] = rgb332_dim(buf[i], brightness);
  }
}

// ============================================================
// NeoPixel update — sample sprite buffer, EMA smooth
// ============================================================
static void updateNeoPixels(uint8_t* buf) {
  // Sample 10 evenly spaced points along horizontal center
  int sy = SCR_H / 2;
  for (int i = 0; i < NUM_LEDS; i++) {
    int sx = (SCR_W * (i + 1)) / (NUM_LEDS + 1);
    uint8_t c = buf[sy * SCR_W + sx];
    uint8_t r, g, b;
    rgb332_unpack(c, r, g, b);

    // EMA smoothing (alpha ~0.15)
    neoR[i] = ((uint16_t)neoR[i] * 217 + (uint16_t)r * 38) >> 8;
    neoG[i] = ((uint16_t)neoG[i] * 217 + (uint16_t)g * 38) >> 8;
    neoB[i] = ((uint16_t)neoB[i] * 217 + (uint16_t)b * 38) >> 8;

    leds[i] = CRGB(neoR[i], neoG[i], neoB[i]);
  }
  FastLED.show();
}

// ============================================================
// Pick random mode duration (45-90 seconds)
// ============================================================
static uint32_t randomModeDuration() {
  return random(45000, 90001);
}

// ============================================================
// Switch to next mode
// ============================================================
static void activateNextMode() {
  currentMode = (Mode)((currentMode + 1) % MODE_COUNT);
  modeStartTime = millis();
  modeDuration = randomModeDuration();

  // Mode-specific init
  if (currentMode == MODE_PARTICLES) {
    initParticles();
    // Clear both sprite buffers so trails start fresh
    _sprites[0].clear(0x00);
    _sprites[1].clear(0x00);
  }
  if (currentMode == MODE_CELLULAR) {
    initCA();
  }
}

// ============================================================
// SETUP
// ============================================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);

  // Display — landscape 320x240, 8-bit color
  lcd.setColorDepth(8);
  if (lcd.width() < lcd.height()) {
    lcd.setRotation(lcd.getRotation() ^ 1);
  }

  for (int i = 0; i < 2; i++) {
    _sprites[i].setColorDepth(8);
    if (!_sprites[i].createSprite(SCR_W, SCR_H)) {
      _sprites[i].setPsram(true);
      _sprites[i].createSprite(SCR_W, SCR_H);
    }
  }
  _sprites[0].clear(0x00);
  _sprites[1].clear(0x00);

  // NeoPixels
  FastLED.addLeds<WS2812B, NEO_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(40);
  memset(neoR, 0, NUM_LEDS);
  memset(neoG, 0, NUM_LEDS);
  memset(neoB, 0, NUM_LEDS);

  // Init LUTs and palettes
  initLUTs();
  generatePalette(palA, 0);
  generatePalette(palB, 1);
  memcpy(currentPalette, palA, 256);

  // Init CA buffers
  initCA();

  // Init particles
  initParticles();

  // Start first mode
  modeStartTime = millis();
  modeDuration = randomModeDuration();
  currentMode = MODE_PLASMA;

  // Hold startWrite for entire runtime (no SD card used)
  lcd.startWrite();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  M5.update();
  uint32_t now = millis();
  timeS = now * 0.001f;
  frameCount++;

  // --- Touch: cycle mode ---
  auto touch = M5.Touch.getDetail();
  if (touch.wasPressed()) {
    if (!transitioning) {
      startTransition();
    }
  }

  // --- Auto-transition timer ---
  if (!transitioning && (now - modeStartTime >= modeDuration)) {
    startTransition();
  }

  // --- Update palette ---
  updatePalette();

  // --- Render current mode into sprite ---
  LGFX_Sprite& sp = _sprites[_flip];
  uint8_t* buf = (uint8_t*)sp.getBuffer();

  switch (currentMode) {
    case MODE_PLASMA:    renderPlasma(buf);    break;
    case MODE_PARTICLES: renderParticles(buf); break;
    case MODE_MOIRE:     renderMoire(buf);     break;
    case MODE_CELLULAR:  renderCellular(buf);  break;
    default: break;
  }

  // --- Handle transition fade ---
  if (transitioning) {
    uint32_t elapsed = now - transStart;
    if (transPhase == 0) {
      // Fade out
      if (elapsed >= TRANS_DURATION) {
        applyBrightness(buf, 0);
        transPhase = 1;
        transStart = now;
        activateNextMode();
      } else {
        uint8_t bright = 255 - (uint8_t)((elapsed * 255) / TRANS_DURATION);
        applyBrightness(buf, bright);
      }
    } else {
      // Fade in
      if (elapsed >= TRANS_DURATION) {
        transitioning = false;
      } else {
        uint8_t bright = (uint8_t)((elapsed * 255) / TRANS_DURATION);
        applyBrightness(buf, bright);
      }
    }
  }

  // --- Push to display ---
  if (currentMode == MODE_PARTICLES || currentMode == MODE_CELLULAR) {
    diffDraw(&_sprites[_flip], &_sprites[_flip ^ 1]);
  } else {
    sp.pushSprite(&lcd, 0, 0);
  }
  _flip ^= 1;

  // --- NeoPixels (every 3rd frame) ---
  if (frameCount % 3 == 0) {
    updateNeoPixels(buf);
  }
}
