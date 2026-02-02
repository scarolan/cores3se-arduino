// Classic Screensavers — M5Stack CoreS3 SE
// 6 modes: Flying Toasters, Pipes, Starfield, Matrix Rain, Mystify, Bouncing Logo
// Touch to cycle modes. NeoPixels ambient glow. Runs forever as desk piece.

#include <M5Unified.h>
#include <FastLED.h>
// #include <SD.h>  // Uncomment for screenshot feature
#include "toaster_sprites.h"
#include "dvd_logo.h"

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

// --- RGB332 helpers ---
static inline uint8_t rgb332(uint8_t r, uint8_t g, uint8_t b) {
  return (r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6);
}

static inline void rgb332_unpack(uint8_t c, uint8_t& r, uint8_t& g, uint8_t& b) {
  r = c & 0xE0;
  g = (c & 0x1C) << 3;
  b = (c & 0x03) << 6;
}

static inline uint8_t rgb332_dim(uint8_t c, uint8_t factor) {
  uint8_t r = c & 0xE0;
  uint8_t g = (c & 0x1C) << 3;
  uint8_t b = (c & 0x03) << 6;
  r = ((uint16_t)r * factor) >> 8;
  g = ((uint16_t)g * factor) >> 8;
  b = ((uint16_t)b * factor) >> 8;
  return (r & 0xE0) | ((g >> 3) & 0x1C) | (b >> 6);
}

// --- LUT ---
static uint8_t fadeLUT[256];

// --- Mode management ---
enum Mode {
  MODE_TOASTERS = 0,
  MODE_PIPES,
  MODE_STARFIELD,
  MODE_MATRIX,
  MODE_MYSTIFY,
  MODE_BOUNCE,
  MODE_COUNT
};
static Mode currentMode = MODE_TOASTERS;
static uint32_t modeStartTime = 0;
static uint32_t modeDuration = 0;
static bool transitioning = false;
static int transPhase = 0;
static uint32_t transStart = 0;
#define TRANS_DURATION 1000

// --- Timing ---
static uint32_t frameCount = 0;

// --- NeoPixel smoothing ---
static uint8_t neoR[NUM_LEDS], neoG[NUM_LEDS], neoB[NUM_LEDS];

// ============================================================
// Bresenham line drawing (into RGB332 buffer)
// ============================================================
static void drawLine(uint8_t* buf, int x0, int y0, int x1, int y1, uint8_t color) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    if (x0 >= 0 && x0 < SCR_W && y0 >= 0 && y0 < SCR_H)
      buf[y0 * SCR_W + x0] = color;
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// ============================================================
// Filled circle (midpoint algorithm)
// ============================================================
static void fillCircle(uint8_t* buf, int cx, int cy, int r, uint8_t color) {
  int x = 0, y = r, d = 1 - r;
  auto hline = [&](int lx, int rx, int row) {
    if (row < 0 || row >= SCR_H) return;
    if (lx < 0) lx = 0;
    if (rx >= SCR_W) rx = SCR_W - 1;
    for (int i = lx; i <= rx; i++) buf[row * SCR_W + i] = color;
  };
  while (x <= y) {
    hline(cx - y, cx + y, cy + x);
    hline(cx - y, cx + y, cy - x);
    hline(cx - x, cx + x, cy + y);
    hline(cx - x, cx + x, cy - y);
    if (d < 0) { d += 2 * x + 3; }
    else { d += 2 * (x - y) + 5; y--; }
    x++;
  }
}

// ============================================================
// Filled rectangle
// ============================================================
static void fillRect(uint8_t* buf, int x, int y, int w, int h, uint8_t color) {
  for (int row = y; row < y + h; row++) {
    if (row < 0 || row >= SCR_H) continue;
    int lx = x < 0 ? 0 : x;
    int rx = (x + w) > SCR_W ? SCR_W : (x + w);
    for (int col = lx; col < rx; col++)
      buf[row * SCR_W + col] = color;
  }
}

// ============================================================
// Simple 5x7 font for Matrix and Bouncing Logo
// ============================================================
static const uint8_t font5x7[][5] PROGMEM = {
  // ASCII 33-90 (! to Z), index = char - 33
  {0x00,0x00,0x5F,0x00,0x00}, // !
  {0x00,0x07,0x00,0x07,0x00}, // "
  {0x14,0x7F,0x14,0x7F,0x14}, // #
  {0x24,0x2A,0x7F,0x2A,0x12}, // $
  {0x23,0x13,0x08,0x64,0x62}, // %
  {0x36,0x49,0x55,0x22,0x50}, // &
  {0x00,0x05,0x03,0x00,0x00}, // '
  {0x00,0x1C,0x22,0x41,0x00}, // (
  {0x00,0x41,0x22,0x1C,0x00}, // )
  {0x14,0x08,0x3E,0x08,0x14}, // *
  {0x08,0x08,0x3E,0x08,0x08}, // +
  {0x00,0x50,0x30,0x00,0x00}, // ,
  {0x08,0x08,0x08,0x08,0x08}, // -
  {0x00,0x60,0x60,0x00,0x00}, // .
  {0x20,0x10,0x08,0x04,0x02}, // /
  {0x3E,0x51,0x49,0x45,0x3E}, // 0
  {0x00,0x42,0x7F,0x40,0x00}, // 1
  {0x42,0x61,0x51,0x49,0x46}, // 2
  {0x21,0x41,0x45,0x4B,0x31}, // 3
  {0x18,0x14,0x12,0x7F,0x10}, // 4
  {0x27,0x45,0x45,0x45,0x39}, // 5
  {0x3C,0x4A,0x49,0x49,0x30}, // 6
  {0x01,0x71,0x09,0x05,0x03}, // 7
  {0x36,0x49,0x49,0x49,0x36}, // 8
  {0x06,0x49,0x49,0x29,0x1E}, // 9
  {0x00,0x36,0x36,0x00,0x00}, // :
  {0x00,0x56,0x36,0x00,0x00}, // ;
  {0x08,0x14,0x22,0x41,0x00}, // <
  {0x14,0x14,0x14,0x14,0x14}, // =
  {0x00,0x41,0x22,0x14,0x08}, // >
  {0x02,0x01,0x51,0x09,0x06}, // ?
  {0x32,0x49,0x79,0x41,0x3E}, // @
  {0x7E,0x11,0x11,0x11,0x7E}, // A
  {0x7F,0x49,0x49,0x49,0x36}, // B
  {0x3E,0x41,0x41,0x41,0x22}, // C
  {0x7F,0x41,0x41,0x22,0x1C}, // D
  {0x7F,0x49,0x49,0x49,0x41}, // E
  {0x7F,0x09,0x09,0x09,0x01}, // F
  {0x3E,0x41,0x49,0x49,0x7A}, // G
  {0x7F,0x08,0x08,0x08,0x7F}, // H
  {0x00,0x41,0x7F,0x41,0x00}, // I
  {0x20,0x40,0x41,0x3F,0x01}, // J
  {0x7F,0x08,0x14,0x22,0x41}, // K
  {0x7F,0x40,0x40,0x40,0x40}, // L
  {0x7F,0x02,0x0C,0x02,0x7F}, // M
  {0x7F,0x04,0x08,0x10,0x7F}, // N
  {0x3E,0x41,0x41,0x41,0x3E}, // O
  {0x7F,0x09,0x09,0x09,0x06}, // P
  {0x3E,0x41,0x51,0x21,0x5E}, // Q
  {0x7F,0x09,0x19,0x29,0x46}, // R
  {0x46,0x49,0x49,0x49,0x31}, // S
  {0x01,0x01,0x7F,0x01,0x01}, // T
  {0x3F,0x40,0x40,0x40,0x3F}, // U
  {0x1F,0x20,0x40,0x20,0x1F}, // V
  {0x3F,0x40,0x38,0x40,0x3F}, // W
  {0x63,0x14,0x08,0x14,0x63}, // X
  {0x07,0x08,0x70,0x08,0x07}, // Y
  {0x61,0x51,0x49,0x45,0x43}, // Z
};

static void drawChar5x7(uint8_t* buf, int cx, int cy, char ch, uint8_t color) {
  int idx = -1;
  if (ch >= '!' && ch <= 'Z') idx = ch - '!';
  else if (ch >= 'a' && ch <= 'z') idx = ch - 'a' + ('A' - '!');  // uppercase mapping
  if (idx < 0 || idx >= (int)(sizeof(font5x7) / sizeof(font5x7[0]))) return;

  for (int col = 0; col < 5; col++) {
    uint8_t bits = pgm_read_byte(&font5x7[idx][col]);
    for (int row = 0; row < 7; row++) {
      if (bits & (1 << row)) {
        int px = cx + col;
        int py = cy + row;
        if (px >= 0 && px < SCR_W && py >= 0 && py < SCR_H)
          buf[py * SCR_W + px] = color;
      }
    }
  }
}

static void drawString5x7(uint8_t* buf, int x, int y, const char* str, uint8_t color) {
  while (*str) {
    drawChar5x7(buf, x, y, *str, color);
    x += 6;
    str++;
  }
}

// ============================================================
// MODE 1: Flying Toasters
// ============================================================
#define MAX_FLYERS 15
struct FlyingObject {
  float x, y;
  float vx, vy;
  uint8_t frame;
  uint8_t frameDelay;
  uint8_t frameCounter;
  bool isToast;
};
static FlyingObject flyers[MAX_FLYERS];

static void initToasters() {
  for (int i = 0; i < MAX_FLYERS; i++) {
    FlyingObject& f = flyers[i];
    f.isToast = (i >= 10);  // last 5 are toast
    f.x = random(0, SCR_W + 100);
    f.y = random(-100, SCR_H);
    float speed = 1.0f + random(0, 20) * 0.1f;  // 1.0-3.0
    f.vx = -speed;
    f.vy = speed * 0.6f;
    f.frame = random(0, NUM_TOASTER_FRAMES);
    f.frameDelay = random(3, 8);
    f.frameCounter = 0;
  }
}

static void blitSprite(uint8_t* buf, const SpriteFrame* frame, int dx, int dy) {
  uint8_t w = pgm_read_byte(&frame->w);
  uint8_t h = pgm_read_byte(&frame->h);
  const uint8_t* rgb = (const uint8_t*)pgm_read_dword(&frame->rgb332);
  const uint8_t* alpha = (const uint8_t*)pgm_read_dword(&frame->alpha);

  for (int sy = 0; sy < h; sy++) {
    int py = dy + sy;
    if (py < 0 || py >= SCR_H) continue;
    for (int sx = 0; sx < w; sx++) {
      int px = dx + sx;
      if (px < 0 || px >= SCR_W) continue;
      int idx = sy * w + sx;
      // Check alpha bit
      if (pgm_read_byte(&alpha[idx >> 3]) & (0x80 >> (idx & 7))) {
        uint8_t c = pgm_read_byte(&rgb[idx]);
        if (c != 0) buf[py * SCR_W + px] = c;
      }
    }
  }
}

static void renderToasters(uint8_t* buf) {
  // Dark blue background
  memset(buf, rgb332(0, 0, 40), SCR_W * SCR_H);

  for (int i = 0; i < MAX_FLYERS; i++) {
    FlyingObject& f = flyers[i];
    f.x += f.vx;
    f.y += f.vy;

    // Wrap around
    uint8_t fw, fh;
    if (f.isToast) {
      fw = pgm_read_byte(&toastFrame.w);
      fh = pgm_read_byte(&toastFrame.h);
    } else {
      fw = pgm_read_byte(&toasterFrames[0].w);
      fh = pgm_read_byte(&toasterFrames[0].h);
    }
    if (f.x < -(int)fw - 10) f.x = SCR_W + random(10, 60);
    if (f.y > SCR_H + 10) f.y = -fh - random(10, 60);

    // Animate toasters
    if (!f.isToast) {
      f.frameCounter++;
      if (f.frameCounter >= f.frameDelay) {
        f.frameCounter = 0;
        f.frame = (f.frame + 1) % NUM_TOASTER_FRAMES;
      }
      blitSprite(buf, &toasterFrames[f.frame], (int)f.x, (int)f.y);
    } else {
      blitSprite(buf, &toastFrame, (int)f.x, (int)f.y);
    }
  }
}

// ============================================================
// MODE 2: Pipes — continuous thick lines with round joints
// ============================================================
#define PIPE_R 4  // pipe half-thickness

struct PipeState {
  int px, py;      // current pixel position
  int dx, dy;      // direction (unit: -1, 0, or 1)
  uint8_t color;
  uint8_t highlight;
  uint8_t shadow;
  int segLen;      // total pixels for this straight segment
  int segProgress; // pixels drawn so far in segment
  bool active;
};
#define MAX_PIPES 3
static PipeState pipes[MAX_PIPES];
static int pipeTotalPixels = 0;
static bool pipeFading = false;

static uint8_t pipeColors[] = {
  rgb332(255, 80, 80),   // red
  rgb332(80, 255, 80),   // green
  rgb332(80, 120, 255),  // blue
  rgb332(255, 255, 80),  // yellow
  rgb332(255, 80, 255),  // magenta
  rgb332(80, 255, 255),  // cyan
};

static void initPipes() {
  pipeTotalPixels = 0;
  pipeFading = false;
  for (int i = 0; i < MAX_PIPES; i++) {
    pipes[i].active = false;
  }
}

static void startNewPipe(PipeState& p) {
  int edge = random(0, 4);
  switch (edge) {
    case 0: p.px = random(20, SCR_W - 20); p.py = 0; p.dx = 0; p.dy = 1; break;
    case 1: p.px = random(20, SCR_W - 20); p.py = SCR_H - 1; p.dx = 0; p.dy = -1; break;
    case 2: p.px = 0; p.py = random(20, SCR_H - 20); p.dx = 1; p.dy = 0; break;
    default: p.px = SCR_W - 1; p.py = random(20, SCR_H - 20); p.dx = -1; p.dy = 0; break;
  }
  p.color = pipeColors[random(0, sizeof(pipeColors))];
  p.highlight = rgb332_dim(p.color, 255);
  p.shadow = rgb332_dim(p.color, 100);
  p.segLen = random(40, 140);
  p.segProgress = 0;
  p.active = true;
}

// Draw one pixel-row of pipe cross-section at (cx,cy) with shading
static void drawPipeSlice(uint8_t* buf, int cx, int cy, int dx, int dy,
                           uint8_t base, uint8_t hi, uint8_t sh) {
  if (dx != 0) {
    // Moving horizontally: draw vertical slice with top highlight, bottom shadow
    for (int i = -PIPE_R; i <= PIPE_R; i++) {
      int py = cy + i;
      if (py < 0 || py >= SCR_H || cx < 0 || cx >= SCR_W) continue;
      uint8_t c;
      if (i <= -PIPE_R + 1) c = hi;
      else if (i >= PIPE_R - 1) c = sh;
      else c = base;
      buf[py * SCR_W + cx] = c;
    }
  } else {
    // Moving vertically: draw horizontal slice with left highlight, right shadow
    for (int i = -PIPE_R; i <= PIPE_R; i++) {
      int px = cx + i;
      if (px < 0 || px >= SCR_W || cy < 0 || cy >= SCR_H) continue;
      uint8_t c;
      if (i <= -PIPE_R + 1) c = hi;
      else if (i >= PIPE_R - 1) c = sh;
      else c = base;
      buf[cy * SCR_W + px] = c;
    }
  }
}

static void advancePipe(uint8_t* buf, PipeState& p) {
  if (!p.active) { startNewPipe(p); return; }

  // Draw one slice at current position
  drawPipeSlice(buf, p.px, p.py, p.dx, p.dy, p.color, p.highlight, p.shadow);

  // Advance 1 pixel
  p.px += p.dx;
  p.py += p.dy;
  p.segProgress++;
  pipeTotalPixels++;

  // Off screen? Restart
  if (p.px < -PIPE_R - 5 || p.px > SCR_W + PIPE_R + 5 ||
      p.py < -PIPE_R - 5 || p.py > SCR_H + PIPE_R + 5) {
    p.active = false;
    return;
  }

  // End of segment? Turn with a round joint
  if (p.segProgress >= p.segLen) {
    fillCircle(buf, p.px, p.py, PIPE_R + 1, p.color);

    // Pick perpendicular direction
    if (p.dx != 0) {
      p.dx = 0;
      p.dy = random(0, 2) ? 1 : -1;
    } else {
      p.dy = 0;
      p.dx = random(0, 2) ? 1 : -1;
    }
    p.segLen = random(40, 140);
    p.segProgress = 0;
  }
}

static void renderPipes(uint8_t* buf) {
  // Pipes accumulates, so we must keep both sprite buffers in sync.
  // Copy the OTHER buffer into this one first so we have the full image.
  uint8_t* otherBuf = (uint8_t*)_sprites[_flip ^ 1].getBuffer();
  memcpy(buf, otherBuf, SCR_W * SCR_H);

  if (pipeFading) {
    int total = SCR_W * SCR_H;
    bool allBlack = true;
    for (int i = 0; i < total; i++) {
      buf[i] = fadeLUT[buf[i]];
      if (buf[i] != 0) allBlack = false;
    }
    if (allBlack) {
      initPipes();
      memset(buf, 0, SCR_W * SCR_H);
    }
    return;
  }

  // Advance each pipe by 2 pixels per frame (slow, smooth growth)
  for (int step = 0; step < 2; step++) {
    for (int i = 0; i < MAX_PIPES; i++) {
      advancePipe(buf, pipes[i]);
    }
  }

  if (pipeTotalPixels > 3000) {
    pipeFading = true;
  }
}

// ============================================================
// MODE 3: Starfield
// ============================================================
#define MAX_STARS 500
struct Star {
  float x, y;   // screen-relative: (0,0)=center, units = pixels at z=1
  float z;      // depth: starts at max, decreases toward 0 (viewer)
  float pz;     // previous z for streak
};
static Star* stars = nullptr;
#define STAR_MAX_Z 32.0f
#define STAR_SPEED 0.075f

static void spawnStar(Star& s, bool randomDepth) {
  // Spawn in screen coordinates: at z=STAR_MAX_Z these map to pixels
  s.x = (float)(random(0, SCR_W) - SCR_W / 2);
  s.y = (float)(random(0, SCR_H) - SCR_H / 2);
  if (randomDepth) {
    s.z = random(1, (int)(STAR_MAX_Z * 10)) * 0.1f;
  } else {
    s.z = STAR_MAX_Z;
  }
  s.pz = s.z;
}

static void initStarfield() {
  if (!stars) stars = (Star*)ps_malloc(MAX_STARS * sizeof(Star));
  for (int i = 0; i < MAX_STARS; i++) {
    spawnStar(stars[i], true);  // random depth so field is full at start
  }
}

static void renderStarfield(uint8_t* buf) {
  memset(buf, 0, SCR_W * SCR_H);

  float cx = SCR_W * 0.5f;
  float cy = SCR_H * 0.5f;

  for (int i = 0; i < MAX_STARS; i++) {
    Star& s = stars[i];
    s.pz = s.z;
    s.z -= STAR_SPEED;

    if (s.z <= 0.1f) {
      spawnStar(s, false);
      continue;
    }

    // Project: divide by z for perspective
    float sx = cx + s.x / s.z;
    float sy = cy + s.y / s.z;

    if (sx < 0 || sx >= SCR_W || sy < 0 || sy >= SCR_H) {
      spawnStar(s, false);
      continue;
    }

    // Previous position for streak
    float px = cx + s.x / s.pz;
    float py = cy + s.y / s.pz;

    // Brightness: closer = brighter, linear with floor
    float t = 1.0f - (s.z / STAR_MAX_Z);  // 0=far, 1=close
    uint8_t bv = 40 + (uint8_t)(t * 215);  // range 40-255, always visible
    uint8_t color = rgb332(bv, bv, bv);

    drawLine(buf, (int)px, (int)py, (int)sx, (int)sy, color);

    // Close stars: bright white fat dot
    if (t > 0.85f) {
      int ix = (int)sx, iy = (int)sy;
      uint8_t white = rgb332(255, 255, 255);
      if (ix >= 0 && ix < SCR_W && iy >= 0 && iy < SCR_H)
        buf[iy * SCR_W + ix] = white;
      if (ix+1 < SCR_W) buf[iy * SCR_W + ix + 1] = white;
      if (iy+1 < SCR_H) buf[(iy+1) * SCR_W + ix] = white;
    }
  }
}

// ============================================================
// MODE 4: Matrix Rain
// ============================================================
#define MATRIX_COLS 40
#define MATRIX_CHAR_W 8
struct MatrixColumn {
  float headY;
  float speed;
  int trailLen;
  char chars[30];
  uint8_t charTimer;
};
static MatrixColumn matCols[MATRIX_COLS];

static char randomMatrixChar() {
  int r = random(0, 62);
  if (r < 26) return 'A' + r;
  if (r < 52) return 'a' + (r - 26);
  return '0' + (r - 52);
}

static void initMatrix() {
  for (int i = 0; i < MATRIX_COLS; i++) {
    MatrixColumn& c = matCols[i];
    c.headY = random(-SCR_H, 0);
    c.speed = 1.0f + random(0, 30) * 0.1f;
    c.trailLen = random(8, 25);
    c.charTimer = 0;
    for (int j = 0; j < 30; j++) c.chars[j] = randomMatrixChar();
  }
}

static void renderMatrix(uint8_t* buf) {
  // Fade existing content
  int total = SCR_W * SCR_H;
  for (int i = 0; i < total; i++) {
    buf[i] = fadeLUT[buf[i]];
  }

  for (int i = 0; i < MATRIX_COLS; i++) {
    MatrixColumn& c = matCols[i];
    c.headY += c.speed;

    // Occasionally mutate a character
    c.charTimer++;
    if (c.charTimer > 5) {
      c.charTimer = 0;
      c.chars[random(0, 30)] = randomMatrixChar();
    }

    int hx = i * MATRIX_CHAR_W;
    int hy = (int)c.headY;

    // Draw head (bright white-green)
    if (hy >= 0 && hy < SCR_H) {
      drawChar5x7(buf, hx, hy, c.chars[0], rgb332(200, 255, 200));
    }

    // Draw body (green, fading)
    for (int j = 1; j < c.trailLen; j++) {
      int ty = hy - j * 8;
      if (ty < 0 || ty >= SCR_H) continue;
      float fade = 1.0f - (float)j / c.trailLen;
      uint8_t g = (uint8_t)(200 * fade);
      uint8_t r_val = (uint8_t)(40 * fade);
      drawChar5x7(buf, hx, ty, c.chars[j % 30], rgb332(r_val, g, 0));
    }

    // Wrap when head goes off bottom
    if (hy > SCR_H + c.trailLen * 8) {
      c.headY = random(-40, -8);
      c.speed = 1.0f + random(0, 30) * 0.1f;
      c.trailLen = random(8, 25);
      for (int j = 0; j < 30; j++) c.chars[j] = randomMatrixChar();
    }
  }
}

// ============================================================
// MODE 5: Mystify (bouncing quadrilaterals with trails)
// ============================================================
#define MYSTIFY_SHAPES 2
#define MYSTIFY_VERTS 4
struct MystifyShape {
  float x[MYSTIFY_VERTS], y[MYSTIFY_VERTS];
  float vx[MYSTIFY_VERTS], vy[MYSTIFY_VERTS];
  float hue;
  float hueSpeed;
};
static MystifyShape mystShapes[MYSTIFY_SHAPES];

static uint8_t hsvToRgb332(float h, float s, float v) {
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
  return rgb332((uint8_t)((rf + m) * 255), (uint8_t)((gf + m) * 255), (uint8_t)((bf + m) * 255));
}

static void initMystify() {
  for (int s = 0; s < MYSTIFY_SHAPES; s++) {
    MystifyShape& m = mystShapes[s];
    m.hue = random(0, 360);
    m.hueSpeed = 0.3f + random(0, 10) * 0.1f;
    for (int v = 0; v < MYSTIFY_VERTS; v++) {
      m.x[v] = random(10, SCR_W - 10);
      m.y[v] = random(10, SCR_H - 10);
      m.vx[v] = (random(0, 2) ? 1.0f : -1.0f) * (1.5f + random(0, 20) * 0.1f);
      m.vy[v] = (random(0, 2) ? 1.0f : -1.0f) * (1.5f + random(0, 20) * 0.1f);
    }
  }
}

static void renderMystify(uint8_t* buf) {
  // Fade trails
  int total = SCR_W * SCR_H;
  for (int i = 0; i < total; i++) {
    buf[i] = fadeLUT[buf[i]];
  }

  for (int s = 0; s < MYSTIFY_SHAPES; s++) {
    MystifyShape& m = mystShapes[s];
    m.hue = fmodf(m.hue + m.hueSpeed, 360.0f);
    uint8_t color = hsvToRgb332(m.hue, 1.0f, 1.0f);

    // Update vertices
    for (int v = 0; v < MYSTIFY_VERTS; v++) {
      m.x[v] += m.vx[v];
      m.y[v] += m.vy[v];

      if (m.x[v] <= 0 || m.x[v] >= SCR_W - 1) {
        m.vx[v] = -m.vx[v];
        m.x[v] = m.x[v] <= 0 ? 0 : SCR_W - 1;
      }
      if (m.y[v] <= 0 || m.y[v] >= SCR_H - 1) {
        m.vy[v] = -m.vy[v];
        m.y[v] = m.y[v] <= 0 ? 0 : SCR_H - 1;
      }
    }

    // Draw closed quadrilateral
    for (int v = 0; v < MYSTIFY_VERTS; v++) {
      int nv = (v + 1) % MYSTIFY_VERTS;
      drawLine(buf, (int)m.x[v], (int)m.y[v], (int)m.x[nv], (int)m.y[nv], color);
    }
  }
}

// ============================================================
// MODE 6: Bouncing DVD Logo (real logo sprite, runtime colorized)
// ============================================================
struct BounceLogo {
  float x, y;
  float vx, vy;
  uint8_t colorIdx;
};
static BounceLogo dvdLogo;

static uint8_t rainbowColors[] = {
  0xE0, // red
  0xEC, // orange
  0xFC, // yellow
  0x1C, // green
  0x03, // blue
  0x63, // indigo/purple
  0xE3, // magenta
};
#define NUM_RAINBOW 7

static void initBounce() {
  dvdLogo.x = random(0, SCR_W - DVD_LOGO_W);
  dvdLogo.y = random(0, SCR_H - DVD_LOGO_H);
  dvdLogo.vx = 2.0f;
  dvdLogo.vy = 1.5f;
  dvdLogo.colorIdx = 0;
}

static void blitDvdLogo(uint8_t* buf, int dx, int dy, uint8_t color) {
  for (int sy = 0; sy < DVD_LOGO_H; sy++) {
    int py = dy + sy;
    if (py < 0 || py >= SCR_H) continue;
    for (int sx = 0; sx < DVD_LOGO_W; sx++) {
      int px = dx + sx;
      if (px < 0 || px >= SCR_W) continue;
      int idx = sy * DVD_LOGO_W + sx;
      if (pgm_read_byte(&dvdLogoAlpha[idx >> 3]) & (0x80 >> (idx & 7))) {
        buf[py * SCR_W + px] = color;
      }
    }
  }
}

static void renderBounce(uint8_t* buf) {
  memset(buf, 0, SCR_W * SCR_H);

  dvdLogo.x += dvdLogo.vx;
  dvdLogo.y += dvdLogo.vy;

  bool bounced = false;
  if (dvdLogo.x <= 0) { dvdLogo.vx = fabsf(dvdLogo.vx); bounced = true; }
  if (dvdLogo.x >= SCR_W - DVD_LOGO_W) { dvdLogo.vx = -fabsf(dvdLogo.vx); bounced = true; }
  if (dvdLogo.y <= 0) { dvdLogo.vy = fabsf(dvdLogo.vy); bounced = true; }
  if (dvdLogo.y >= SCR_H - DVD_LOGO_H) { dvdLogo.vy = -fabsf(dvdLogo.vy); bounced = true; }

  if (bounced) {
    dvdLogo.colorIdx = (dvdLogo.colorIdx + 1) % NUM_RAINBOW;
  }

  blitDvdLogo(buf, (int)dvdLogo.x, (int)dvdLogo.y, rainbowColors[dvdLogo.colorIdx]);
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
static void applyBrightness(uint8_t* buf, uint8_t brightness) {
  int total = SCR_W * SCR_H;
  for (int i = 0; i < total; i++) {
    buf[i] = rgb332_dim(buf[i], brightness);
  }
}

static void startTransition() {
  transitioning = true;
  transPhase = 0;
  transStart = millis();
}

static uint32_t randomModeDuration() {
  return random(45000, 90001);
}

// ============================================================
// NeoPixel update — sample sprite buffer, EMA smooth
// ============================================================
static void updateNeoPixels(uint8_t* buf) {
  int sy = SCR_H / 2;
  for (int i = 0; i < NUM_LEDS; i++) {
    int sx = (SCR_W * (i + 1)) / (NUM_LEDS + 1);
    uint8_t c = buf[sy * SCR_W + sx];
    uint8_t r, g, b;
    rgb332_unpack(c, r, g, b);

    neoR[i] = ((uint16_t)neoR[i] * 217 + (uint16_t)r * 38) >> 8;
    neoG[i] = ((uint16_t)neoG[i] * 217 + (uint16_t)g * 38) >> 8;
    neoB[i] = ((uint16_t)neoB[i] * 217 + (uint16_t)b * 38) >> 8;

    leds[i] = CRGB(neoR[i], neoG[i], neoB[i]);
  }
  FastLED.show();
}

// ============================================================
// Screenshot — save current frame as 24-bit BMP to SD card
// To enable: uncomment #include <SD.h> above, and set
// ENABLE_SCREENSHOTS to 1. Press BtnB (bottom middle) to capture.
// ============================================================
#define ENABLE_SCREENSHOTS 0

#if ENABLE_SCREENSHOTS
static int screenshotNum = 0;
static bool sdReady = false;

static void findNextScreenshotNum() {
  char path[32];
  while (screenshotNum < 9999) {
    snprintf(path, sizeof(path), "/scr_%04d.bmp", screenshotNum);
    if (!SD.exists(path)) break;
    screenshotNum++;
  }
}

static void saveScreenshot(uint8_t* buf) {
  if (!sdReady) return;

  char path[32];
  snprintf(path, sizeof(path), "/scr_%04d.bmp", screenshotNum);

  File f = SD.open(path, FILE_WRITE);
  if (!f) return;

  uint32_t rowSize = ((SCR_W * 3 + 3) & ~3);
  uint32_t imageSize = rowSize * SCR_H;
  uint32_t fileSize = 54 + imageSize;

  uint8_t bmpHeader[54] = {};
  bmpHeader[0] = 'B'; bmpHeader[1] = 'M';
  bmpHeader[2] = fileSize;        bmpHeader[3] = fileSize >> 8;
  bmpHeader[4] = fileSize >> 16;  bmpHeader[5] = fileSize >> 24;
  bmpHeader[10] = 54;
  bmpHeader[14] = 40;
  bmpHeader[18] = SCR_W;        bmpHeader[19] = SCR_W >> 8;
  bmpHeader[22] = SCR_H;        bmpHeader[23] = SCR_H >> 8;
  bmpHeader[26] = 1;
  bmpHeader[28] = 24;
  bmpHeader[34] = imageSize;        bmpHeader[35] = imageSize >> 8;
  bmpHeader[36] = imageSize >> 16;  bmpHeader[37] = imageSize >> 24;

  f.write(bmpHeader, 54);

  uint8_t row[SCR_W * 3 + 4];
  uint32_t pad = rowSize - SCR_W * 3;

  for (int y = SCR_H - 1; y >= 0; y--) {
    for (int x = 0; x < SCR_W; x++) {
      uint8_t c = buf[y * SCR_W + x];
      uint8_t r, g, b;
      rgb332_unpack(c, r, g, b);
      r = r | (r >> 3) | (r >> 6);
      g = g | (g >> 3) | (g >> 6);
      b = b | (b >> 2) | (b >> 4) | (b >> 6);
      row[x * 3 + 0] = b;
      row[x * 3 + 1] = g;
      row[x * 3 + 2] = r;
    }
    for (uint32_t i = 0; i < pad; i++) row[SCR_W * 3 + i] = 0;
    f.write(row, rowSize);
  }

  f.close();
  screenshotNum++;

  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(255, 255, 255);
  FastLED.show();
  delay(150);
  for (int i = 0; i < NUM_LEDS; i++) leds[i] = CRGB(0, 0, 0);
  FastLED.show();
}
#endif

// ============================================================
// Mode init dispatcher
// ============================================================
static void activateNextMode() {
  currentMode = (Mode)((currentMode + 1) % MODE_COUNT);
  modeStartTime = millis();
  modeDuration = randomModeDuration();

  // Clear both buffers for fresh start
  _sprites[0].clear(0x00);
  _sprites[1].clear(0x00);

  switch (currentMode) {
    case MODE_TOASTERS: initToasters(); break;
    case MODE_PIPES:    initPipes(); break;
    case MODE_STARFIELD: initStarfield(); break;
    case MODE_MATRIX:   initMatrix(); break;
    case MODE_MYSTIFY:  initMystify(); break;
    case MODE_BOUNCE:   initBounce(); break;
    default: break;
  }
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

  for (int i = 0; i < 2; i++) {
    _sprites[i].setColorDepth(8);
    if (!_sprites[i].createSprite(SCR_W, SCR_H)) {
      _sprites[i].setPsram(true);
      _sprites[i].createSprite(SCR_W, SCR_H);
    }
  }
  _sprites[0].clear(0x00);
  _sprites[1].clear(0x00);

  FastLED.addLeds<WS2812B, NEO_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(40);
  memset(neoR, 0, NUM_LEDS);
  memset(neoG, 0, NUM_LEDS);
  memset(neoB, 0, NUM_LEDS);

  // Build fade LUT — dim each RGB332 color to ~85%
  for (int i = 0; i < 256; i++) {
    fadeLUT[i] = rgb332_dim(i, 216);
  }

#if ENABLE_SCREENSHOTS
  sdReady = SD.begin(GPIO_NUM_4, SPI, 25000000);
  if (!sdReady) sdReady = SD.begin();
  if (sdReady) findNextScreenshotNum();
#endif

  // Init first mode
  modeStartTime = millis();
  modeDuration = randomModeDuration();
  currentMode = MODE_TOASTERS;
  initToasters();

  lcd.startWrite();
}

// ============================================================
// LOOP
// ============================================================
void loop() {
  M5.update();
  uint32_t now = millis();
  frameCount++;

  // --- Touch screen: cycle mode ---
  auto touch = M5.Touch.getDetail();
  if (touch.wasPressed()) {
    if (!transitioning) {
      startTransition();
    }
  }

#if ENABLE_SCREENSHOTS
  // --- BtnB (bottom middle): screenshot ---
  if (M5.BtnB.wasPressed()) {
    LGFX_Sprite& curSp = _sprites[_flip];
    uint8_t* curBuf = (uint8_t*)curSp.getBuffer();
    lcd.endWrite();
    saveScreenshot(curBuf);
    lcd.startWrite();
  }
#endif

  // --- Auto-transition timer ---
  if (!transitioning && (now - modeStartTime >= modeDuration)) {
    startTransition();
  }

  // --- Render current mode ---
  LGFX_Sprite& sp = _sprites[_flip];
  uint8_t* buf = (uint8_t*)sp.getBuffer();

  switch (currentMode) {
    case MODE_TOASTERS:  renderToasters(buf);  break;
    case MODE_PIPES:     renderPipes(buf);      break;
    case MODE_STARFIELD: renderStarfield(buf);  break;
    case MODE_MATRIX:    renderMatrix(buf);     break;
    case MODE_MYSTIFY:   renderMystify(buf);    break;
    case MODE_BOUNCE:    renderBounce(buf);     break;
    default: break;
  }

  // --- Handle transition fade ---
  if (transitioning) {
    uint32_t elapsed = now - transStart;
    if (transPhase == 0) {
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
      if (elapsed >= TRANS_DURATION) {
        transitioning = false;
      } else {
        uint8_t bright = (uint8_t)((elapsed * 255) / TRANS_DURATION);
        applyBrightness(buf, bright);
      }
    }
  }

  // --- Push to display ---
  bool useDiffDraw = (currentMode == MODE_MATRIX || currentMode == MODE_MYSTIFY);
  if (useDiffDraw) {
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
