// ledcube - scene player for three 64x64 HUB75 panels arranged as a cube corner
// (top + left + right faces meeting at one corner).
//
// Scenes are rendered into three viewer-oriented 64x64 face buffers. Each face
// pixel also has a 3D position on the unit cube, so volumetric scenes flow
// seamlessly across the edges. A lookup table then maps faces onto the
// physical chain (which panel, which rotation) - see ledcube.conf.
//
// Control / data arrive as UDP datagrams (default port 7777):
//   "C <command>"   scene=<name> | auto | next | brightness=<n> | off | on |
//                   reload | status
//   "S k=v ..."     PC stats  (cpu ram gpu gput vram down up cores=a,b,c)
//   "A" + 32 bytes  audio spectrum bands, 0..255, bass first
//
// This file is public domain; the rgbmatrix library it links to is GPL v2.

#include "led-matrix.h"
#include "graphics.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <math.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using rgb_matrix::Canvas;
using rgb_matrix::Color;
using rgb_matrix::Font;
using rgb_matrix::FrameCanvas;
using rgb_matrix::RGBMatrix;

static const int N = 64;
enum { TOP = 0, LEFT = 1, RIGHT = 2 };
static const int NBANDS = 32;

// ---------------------------------------------------------------- utilities

struct RGB { uint8_t r, g, b; };

static inline uint8_t u8(float v) {
  return v <= 0 ? 0 : v >= 1 ? 255 : (uint8_t)(v * 255.0f + 0.5f);
}
static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline RGB scale(RGB c, float k) {
  k = clamp01(k);
  return {(uint8_t)(c.r * k), (uint8_t)(c.g * k), (uint8_t)(c.b * k)};
}

static RGB hsv(float h, float s, float v) {
  h -= floorf(h);
  v = clamp01(v);
  int i = (int)(h * 6.0f);
  float f = h * 6.0f - i;
  float p = v * (1 - s), q = v * (1 - f * s), t = v * (1 - (1 - f) * s);
  float r, g, b;
  switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
  }
  return {u8(r), u8(g), u8(b)};
}

static uint32_t rng_state = 0x9e3779b9u;
static inline uint32_t rnd() {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}
static inline float frand() { return (rnd() & 0xffffff) / 16777216.0f; }

static inline uint32_t hash3(int x, int y, int z) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u +
               (uint32_t)z * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
static inline float hashf(int x, int y, int z) {
  return (hash3(x, y, z) & 0xffff) / 65535.0f;
}

// Smooth 3D value noise, 0..1.
static float vnoise(float x, float y, float z) {
  int xi = (int)floorf(x), yi = (int)floorf(y), zi = (int)floorf(z);
  float fx = x - xi, fy = y - yi, fz = z - zi;
  fx = fx * fx * (3 - 2 * fx);
  fy = fy * fy * (3 - 2 * fy);
  fz = fz * fz * (3 - 2 * fz);
  float c[2][2][2];
  for (int a = 0; a < 2; a++)
    for (int b = 0; b < 2; b++)
      for (int d = 0; d < 2; d++) c[a][b][d] = hashf(xi + a, yi + b, zi + d);
#define LERP(a, b, t) ((a) + ((b) - (a)) * (t))
  float x00 = LERP(c[0][0][0], c[1][0][0], fx), x10 = LERP(c[0][1][0], c[1][1][0], fx);
  float x01 = LERP(c[0][0][1], c[1][0][1], fx), x11 = LERP(c[0][1][1], c[1][1][1], fx);
  float y0 = LERP(x00, x10, fy), y1 = LERP(x01, x11, fy);
  return LERP(y0, y1, fz);
#undef LERP
}

static double now_seconds() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ------------------------------------------------------------ frame & faces

struct Frame {
  RGB px[3][N][N];  // [face][j (down)][i (right)], as seen from outside
  void clear() { memset(px, 0, sizeof(px)); }
  inline void set(int f, int i, int j, RGB c) {
    if ((unsigned)i < (unsigned)N && (unsigned)j < (unsigned)N) px[f][j][i] = c;
  }
  inline void add(int f, int i, int j, RGB c) {
    if ((unsigned)i >= (unsigned)N || (unsigned)j >= (unsigned)N) return;
    RGB &p = px[f][j][i];
    p.r = std::min(255, p.r + c.r);
    p.g = std::min(255, p.g + c.g);
    p.b = std::min(255, p.b + c.b);
  }
  void rect(int f, int x, int y, int w, int h, RGB c) {
    for (int j = y; j < y + h; j++)
      for (int i = x; i < x + w; i++) set(f, i, j, c);
  }
  void disc(int f, float cx, float cy, float r, RGB c) {
    for (int j = (int)(cy - r - 1); j <= (int)(cy + r + 1); j++)
      for (int i = (int)(cx - r - 1); i <= (int)(cx + r + 1); i++)
        if ((i - cx) * (i - cx) + (j - cy) * (j - cy) <= r * r) set(f, i, j, c);
  }
  void line(int f, int x0, int y0, int x1, int y1, RGB c) {
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
    for (;;) {
      set(f, x0, y0, c);
      if (x0 == x1 && y0 == y1) break;
      int e2 = 2 * err;
      if (e2 > -dy) { err -= dy; x0 += sx; }
      if (e2 < dx) { err += dx; y0 += sy; }
    }
  }
};

// 3D position of every face pixel on the unit cube. The three visible faces
// are x=1 (LEFT), y=1 (RIGHT) and z=1 (TOP); they meet at corner (1,1,1).
static float P[3][N][N][3];

static void init_geometry() {
  for (int j = 0; j < N; j++)
    for (int i = 0; i < N; i++) {
      float a = (i + 0.5f) / N, b = (j + 0.5f) / N;
      // TOP: right = +y, down = +x (bottom edge touches LEFT's top edge)
      P[TOP][j][i][0] = b; P[TOP][j][i][1] = a; P[TOP][j][i][2] = 1;
      // LEFT (x=1): right = +y (towards the shared vertical edge), down = -z
      P[LEFT][j][i][0] = 1; P[LEFT][j][i][1] = a; P[LEFT][j][i][2] = 1 - b;
      // RIGHT (y=1): right = -x (away from the shared edge), down = -z
      P[RIGHT][j][i][0] = 1 - a; P[RIGHT][j][i][1] = 1; P[RIGHT][j][i][2] = 1 - b;
    }
}

// Step across face boundaries. Returns false if (i,j) falls off the visible
// part of the cube.
static bool neighbor(int f, int i, int j, int &f2, int &i2, int &j2) {
  bool io = i < 0 || i >= N, jo = j < 0 || j >= N;
  if (!io && !jo) { f2 = f; i2 = i; j2 = j; return true; }
  if (io && jo) return false;
  if (f == TOP) {
    if (j >= N) { f2 = LEFT; i2 = i; j2 = j - N; return true; }
    if (i >= N) { f2 = RIGHT; i2 = N - 1 - j; j2 = i - N; return true; }
    return false;
  }
  if (f == LEFT) {
    if (j < 0) { f2 = TOP; i2 = i; j2 = N + j; return true; }
    if (i >= N) { f2 = RIGHT; i2 = i - N; j2 = j; return true; }
    return false;
  }
  if (j < 0) { f2 = TOP; i2 = N + j; j2 = N - 1 - i; return true; }
  if (i < 0) { f2 = LEFT; i2 = N + i; j2 = j; return true; }
  return false;
}

// Adapter so the library's BDF text drawing can target one face.
class FaceCanvas : public Canvas {
 public:
  FaceCanvas(Frame *fr, int face) : fr_(fr), face_(face) {}
  int width() const override { return N; }
  int height() const override { return N; }
  void SetPixel(int x, int y, uint8_t r, uint8_t g, uint8_t b) override {
    if (fr_) fr_->set(face_, x, y, {r, g, b});
  }
  void Clear() override {}
  void Fill(uint8_t, uint8_t, uint8_t) override {}
 private:
  Frame *fr_;
  int face_;
};

static Font font_small, font_med, font_big;  // 5x7, 6x10, 10x20

// Draw text with its top-left at (x,y). Returns the advance in pixels.
static int text(Frame *fr, int f, const Font &font, int x, int y, RGB c, const char *s) {
  FaceCanvas fc(fr, f);
  return rgb_matrix::DrawText(&fc, font, x, y + font.baseline(), Color(c.r, c.g, c.b), s);
}
static int text_width(const Font &font, const char *s) {
  return text(NULL, 0, font, 0, 0, {0, 0, 0}, s);
}
static void text_centered(Frame *fr, int f, const Font &font, int y, RGB c, const char *s) {
  text(fr, f, font, (N - text_width(font, s)) / 2, y, c, s);
}

// Chunky seven-segment digit.
static void seg7(Frame &fr, int f, int x, int y, int w, int h, int t, int d, RGB c) {
  static const uint8_t m[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
  if (d < 0 || d > 9) return;
  int hh = (h - t) / 2;
  uint8_t s = m[d];
  if (s & 0x01) fr.rect(f, x, y, w, t, c);
  if (s & 0x02) fr.rect(f, x + w - t, y, t, hh + t, c);
  if (s & 0x04) fr.rect(f, x + w - t, y + hh, t, h - hh, c);
  if (s & 0x08) fr.rect(f, x, y + h - t, w, t, c);
  if (s & 0x10) fr.rect(f, x, y + hh, t, h - hh, c);
  if (s & 0x20) fr.rect(f, x, y, t, hh + t, c);
  if (s & 0x40) fr.rect(f, x, y + hh, w, t, c);
}

// ------------------------------------------------------------- shared state

struct Stats {
  float cpu = 0, ram = 0, gpu = 0, gput = 0, vram = 0, down = 0, up = 0;
  std::vector<float> cores;
  float cpu_hist[N] = {0}, gpu_hist[N] = {0};
  double stamp = -1e9;
};

struct Weather {
  bool valid = false;
  float temp = 0;
  int code = 0;
  bool is_day = true;
};

struct Shared {
  std::mutex mu;
  Stats stats;
  float bands[NBANDS] = {0};
  double audio_stamp = -1e9;
  Weather weather;
  std::deque<std::pair<std::string, sockaddr_in>> commands;
} G;

static std::map<std::string, std::string> conf;
static std::string conf_path = "ledcube.conf";

static void load_config() {
  std::map<std::string, std::string> c;
  FILE *fp = fopen(conf_path.c_str(), "r");
  if (!fp) {
    fprintf(stderr, "ledcube: cannot read %s, using defaults\n", conf_path.c_str());
  } else {
    char line[512];
    while (fgets(line, sizeof(line), fp)) {
      char *hash = strchr(line, '#');
      if (hash) *hash = 0;
      char *eq = strchr(line, '=');
      if (!eq) continue;
      std::string k(line, eq - line), v(eq + 1);
      auto trim = [](std::string &s) {
        size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
        s = a == std::string::npos ? "" : s.substr(a, b - a + 1);
      };
      trim(k); trim(v);
      if (!k.empty()) c[k] = v;
    }
    fclose(fp);
  }
  conf = c;
}
static std::string cfg(const char *k, const char *def) {
  auto it = conf.find(k);
  return it == conf.end() ? def : it->second;
}
static double cfgd(const char *k, double def) {
  auto it = conf.find(k);
  return it == conf.end() || it->second.empty() ? def : atof(it->second.c_str());
}

// --------------------------------------------------------------- the scenes

struct Scene {
  virtual ~Scene() {}
  virtual const char *name() const = 0;
  virtual void enter() {}
  virtual void render(Frame &fr, double t, float dt) = 0;
  virtual bool raw() const { return false; }  // bypass the face mapping
};

#define FOR_PIXELS \
  for (int f = 0; f < 3; f++) for (int j = 0; j < N; j++) for (int i = 0; i < N; i++)

struct Plasma : Scene {
  const char *name() const override { return "plasma"; }
  void render(Frame &fr, double t, float) override {
    float tt = (float)fmod(t, 3600.0);
    float cx = 0.5f + 0.5f * sinf(tt * 0.31f), cy = 0.5f + 0.5f * sinf(tt * 0.23f + 1),
          cz = 0.5f + 0.5f * sinf(tt * 0.17f + 2);
    FOR_PIXELS {
      const float *p = P[f][j][i];
      float d = sqrtf((p[0] - cx) * (p[0] - cx) + (p[1] - cy) * (p[1] - cy) +
                      (p[2] - cz) * (p[2] - cz));
      float v = sinf(p[0] * 5.1f + tt * 0.9f) + sinf(p[1] * 4.3f - tt * 0.7f) +
                sinf(p[2] * 4.7f + tt * 0.5f) + sinf(d * 9.0f - tt * 1.3f);
      float h = v * 0.125f + tt * 0.02f;
      const float tau = 6.2831853f;
      fr.px[f][j][i] = {u8(0.5f + 0.5f * cosf(tau * h)),
                        u8(0.5f + 0.5f * cosf(tau * (h + 0.33f))),
                        u8(0.5f + 0.5f * cosf(tau * (h + 0.67f)))};
    }
  }
};

struct Ripples : Scene {
  const char *name() const override { return "ripples"; }
  void render(Frame &fr, double t, float) override {
    float tt = (float)fmod(t, 3600.0);
    float s[3][3];
    for (int k = 0; k < 3; k++) {
      s[k][0] = 0.5f + 0.6f * sinf(tt * (0.21f + 0.05f * k) + k * 2.1f);
      s[k][1] = 0.5f + 0.6f * sinf(tt * (0.17f + 0.04f * k) + k * 1.3f + 1);
      s[k][2] = 0.5f + 0.6f * sinf(tt * (0.13f + 0.06f * k) + k * 0.7f + 2);
    }
    FOR_PIXELS {
      const float *p = P[f][j][i];
      float v = 0;
      for (int k = 0; k < 3; k++) {
        float dx = p[0] - s[k][0], dy = p[1] - s[k][1], dz = p[2] - s[k][2];
        float d = sqrtf(dx * dx + dy * dy + dz * dz);
        v += sinf(d * 22.0f - tt * 3.0f) / (1.0f + 2.5f * d);
      }
      float b = clamp01(0.5f + 0.65f * v);
      fr.px[f][j][i] = hsv(0.55f + tt * 0.008f + 0.3f * v, 0.8f, 0.04f + b * b);
    }
  }
};

struct Lava : Scene {
  RGB pal[256];
  int variant = -1;
  const char *name() const override { return "lava"; }
  void enter() override {
    variant = (variant + 1) % 3;
    static const float stops[3][5][3] = {
        {{0, 0, 0}, {0.5f, 0, 0}, {1, 0.35f, 0}, {1, 0.85f, 0.1f}, {1, 1, 0.8f}},
        {{0, 0, 0.05f}, {0, 0.1f, 0.45f}, {0, 0.55f, 0.65f}, {0.2f, 0.95f, 0.9f}, {0.9f, 1, 1}},
        {{0, 0, 0}, {0.3f, 0, 0.45f}, {0, 0.6f, 0.3f}, {0.2f, 1, 0.6f}, {0.85f, 1, 0.95f}}};
    for (int k = 0; k < 256; k++) {
      float x = k / 255.0f * 4;
      int a = std::min(3, (int)x);
      float fx = x - a;
      const float *c0 = stops[variant][a], *c1 = stops[variant][a + 1];
      pal[k] = {u8(c0[0] + (c1[0] - c0[0]) * fx), u8(c0[1] + (c1[1] - c0[1]) * fx),
                u8(c0[2] + (c1[2] - c0[2]) * fx)};
    }
  }
  void render(Frame &fr, double t, float) override {
    float tt = (float)fmod(t, 10000.0);
    FOR_PIXELS {
      const float *p = P[f][j][i];
      float n = vnoise(p[0] * 2.6f + tt * 0.07f, p[1] * 2.6f - tt * 0.05f, p[2] * 2.6f - tt * 0.22f) +
                0.5f * vnoise(p[0] * 6 + 9, p[1] * 6 + tt * 0.11f, p[2] * 6 - tt * 0.35f);
      n = clamp01((n / 1.5f - 0.22f) * 1.8f);
      fr.px[f][j][i] = pal[(int)(n * n * (3 - 2 * n) * 255)];
    }
  }
};

// Digital rain: streams are born on the top face's diagonal, run to the nearest
// edge, and fall down the side below it.
struct Rain : Scene {
  struct Stream { float head, speed, delay; int len; };
  Stream st[2][N];
  const char *name() const override { return "rain"; }
  void reset(int side, int c, bool stagger) {
    Stream &s = st[side][c];
    s.head = (float)(side == 0 ? c : N - 1 - c);
    s.speed = 22 + frand() * 40;
    s.len = 10 + rnd() % 28;
    s.delay = frand() * (stagger ? 5.0f : 2.5f);
  }
  void enter() override {
    for (int side = 0; side < 2; side++)
      for (int c = 0; c < N; c++) reset(side, c, true);
  }
  static bool path(int side, int c, int p, int &f, int &i, int &j) {
    if (p < 0 || p >= 2 * N) return false;
    if (side == 0) {
      if (p < N) { if (p < c) return false; f = TOP; i = c; j = p; }
      else { f = LEFT; i = c; j = p - N; }
    } else {
      if (p < N) { if (p < N - 1 - c) return false; f = TOP; i = p; j = N - 1 - c; }
      else { f = RIGHT; i = c; j = p - N; }
    }
    return true;
  }
  void render(Frame &fr, double t, float dt) override {
    fr.clear();
    int tick = (int)(t * 9);
    for (int side = 0; side < 2; side++)
      for (int c = 0; c < N; c++) {
        Stream &s = st[side][c];
        if (s.delay > 0) { s.delay -= dt; continue; }
        s.head += s.speed * dt;
        if (s.head - s.len > 2 * N) { reset(side, c, false); continue; }
        int h = (int)s.head;
        for (int k = 0; k < s.len; k++) {
          int f, i, j;
          if (!path(side, c, h - k, f, i, j)) continue;
          float b = 1.0f - (float)k / s.len;
          b *= 0.55f + 0.45f * hashf(c + side * 100, h - k, tick);
          RGB col = k == 0 ? RGB{200, 255, 220}
                           : RGB{0, u8(b * b), (uint8_t)(u8(b * b) / 4)};
          fr.add(f, i, j, col);
        }
      }
  }
};

// Conway's Life wrapped around the three faces.
struct Life : Scene {
  uint16_t age[3][N][N];
  float glow[3][N][N];
  float acc = 0;
  int gen = 0, stale = 0, last_pop = 0;
  float hue0 = 0;
  const char *name() const override { return "life"; }
  void seed() {
    FOR_PIXELS age[f][j][i] = frand() < 0.28f ? 1 : 0;
    gen = stale = 0;
    hue0 = frand();
  }
  void enter() override { memset(glow, 0, sizeof(glow)); seed(); }
  void step() {
    static uint16_t nxt[3][N][N];
    int pop = 0;
    FOR_PIXELS {
      int n = 0;
      for (int dj = -1; dj <= 1; dj++)
        for (int di = -1; di <= 1; di++) {
          if (!di && !dj) continue;
          int f2, i2, j2;
          if (neighbor(f, i + di, j + dj, f2, i2, j2) && age[f2][j2][i2]) n++;
        }
      uint16_t a = age[f][j][i];
      nxt[f][j][i] = a ? ((n == 2 || n == 3) ? (uint16_t)std::min(a + 1, 60000) : 0)
                       : (n == 3 ? 1 : 0);
      if (nxt[f][j][i]) pop++;
    }
    memcpy(age, nxt, sizeof(age));
    stale = abs(pop - last_pop) <= 2 ? stale + 1 : 0;
    last_pop = pop;
    if (++gen > 1500 || stale > 60 || pop < 40) seed();
  }
  void render(Frame &fr, double, float dt) override {
    acc += dt;
    while (acc > 0.09f) { step(); acc -= 0.09f; }
    FOR_PIXELS {
      float &g = glow[f][j][i];
      uint16_t a = age[f][j][i];
      g = a ? 1.0f : g * (1.0f - 4.0f * dt);
      float h = hue0 + (a ? std::min((int)a, 80) * 0.006f : 0.55f);
      fr.px[f][j][i] = hsv(h, a ? 0.75f : 1.0f, a ? 1.0f : g * 0.35f);
    }
  }
};

// Stars streaming out of the shared corner across all three faces.
struct Warp : Scene {
  struct Star { int f; float a, r, v, hue; };
  std::vector<Star> stars;
  const char *name() const override { return "warp"; }
  void spawn(Star &s, bool anywhere) {
    s.f = rnd() % 3;
    s.a = frand() * 1.5708f;
    s.r = anywhere ? frand() * 90 : frand() * 3;
    s.v = 6 + frand() * 14;
    s.hue = 0.55f + frand() * 0.2f;
  }
  void enter() override {
    stars.resize(260);
    for (auto &s : stars) spawn(s, true);
  }
  void render(Frame &fr, double, float dt) override {
    fr.clear();
    for (auto &s : stars) {
      float r0 = s.r;
      s.r += s.v * dt * (1.0f + s.r * 0.09f);
      if (r0 > 92) { spawn(s, false); continue; }
      float ca = cosf(s.a), sa = sinf(s.a);
      float b = clamp01(0.15f + s.r / 55.0f);
      RGB c = hsv(s.hue, 0.5f * (1 - b * 0.6f), b);
      int steps = (int)(s.r - r0) + 1;
      for (int k = 0; k <= steps; k++) {
        float r = r0 + (s.r - r0) * k / steps;
        int i, j;
        if (s.f == TOP) { i = N - 1 - (int)(r * ca); j = N - 1 - (int)(r * sa); }
        else if (s.f == LEFT) { i = N - 1 - (int)(r * ca); j = (int)(r * sa); }
        else { i = (int)(r * ca); j = (int)(r * sa); }
        fr.add(s.f, i, j, c);
      }
    }
  }
};

// A point travelling over the cube's surface (comets, fireworks, lightning).
struct Walker { int f; float x, y, ang; };

// Bilinear splat at a sub-pixel position, spilling onto the adjoining face.
template <class Fn> static void splat(int f, float x, float y, Fn fn) {
  float fx = x - 0.5f, fy = y - 0.5f;
  int i0 = (int)floorf(fx), j0 = (int)floorf(fy);
  float wx = fx - i0, wy = fy - j0;
  for (int b = 0; b < 2; b++)
    for (int a = 0; a < 2; a++) {
      int f2, i2, j2;
      if (neighbor(f, i0 + a, j0 + b, f2, i2, j2))
        fn(f2, i2, j2, (a ? wx : 1 - wx) * (b ? wy : 1 - wy));
    }
}

// Advance d pixels (d <= 1). Same edge rules as neighbor(), plus the turn the
// heading needs where the two faces' axes differ. Returns 0 if it stayed on its
// face, 1 if it crossed onto another, 2 if it bounced off an outer edge.
static int walk(Walker &c, float d) {
  const float pi = 3.14159265f;
  float nx = c.x + cosf(c.ang) * d, ny = c.y + sinf(c.ang) * d;
  bool lo_i = nx < 0, hi_i = nx >= N, lo_j = ny < 0, hi_j = ny >= N;
  bool io = lo_i || hi_i, jo = lo_j || hi_j;
  int r = 1;
  if (!io && !jo) { c.x = nx; c.y = ny; return 0; }
  if (io && jo) { c.ang += pi; return 2; }
  if (c.f == TOP && hi_j) { c.f = LEFT; c.x = nx; c.y = ny - N; }
  else if (c.f == TOP && hi_i) { c.f = RIGHT; c.x = N - ny; c.y = nx - N; c.ang += pi / 2; }
  else if (c.f == LEFT && lo_j) { c.f = TOP; c.x = nx; c.y = ny + N; }
  else if (c.f == LEFT && hi_i) { c.f = RIGHT; c.x = nx - N; c.y = ny; }
  else if (c.f == RIGHT && lo_j) { c.f = TOP; c.x = N + ny; c.y = N - nx; c.ang -= pi / 2; }
  else if (c.f == RIGHT && lo_i) { c.f = LEFT; c.x = nx + N; c.y = ny; }
  else if (io) { c.ang = pi - c.ang; r = 2; }  // outer edge: bounce
  else { c.ang = -c.ang; r = 2; }
  c.x = std::min(std::max(c.x, 0.0f), N - 0.001f);
  c.y = std::min(std::max(c.y, 0.0f), N - 0.001f);
  return r;
}

// Comets gliding over the cube's surface: they cross the shared edges, bounce
// off the outer ones, and leave fading trails that add up where they overlap.
struct Comets : Scene {
  struct Comet : Walker { float speed, hue, seed; };
  static const int COUNT = 9;
  Comet cm[COUNT];
  float buf[3][N][N][3];  // linear trail light, decays every frame
  const char *name() const override { return "comets"; }
  void enter() override {
    memset(buf, 0, sizeof(buf));
    float hue0 = frand();
    for (int k = 0; k < COUNT; k++) {
      Comet &c = cm[k];
      c.f = k % 3;
      c.x = 4 + frand() * (N - 8);
      c.y = 4 + frand() * (N - 8);
      c.ang = frand() * 6.2831853f;
      c.speed = 26 + frand() * 22;
      c.hue = hue0 + (float)k / COUNT;
      c.seed = frand() * 100;
    }
  }
  void render(Frame &fr, double t, float dt) override {
    float tt = (float)fmod(t, 3600.0);
    float keep = expf(-1.7f * dt);
    FOR_PIXELS {
      float *p = buf[f][j][i];
      p[0] *= keep; p[1] *= keep; p[2] *= keep;
    }
    for (Comet &c : cm) {
      c.ang = fmodf(c.ang + (vnoise(tt * 0.4f, c.seed, 0.5f) - 0.5f) * 5.0f * dt, 6.2831853f);
      RGB col = hsv(c.hue + tt * 0.01f, 1.0f, 1.0f);
      float dist = c.speed * dt;
      int steps = (int)(dist * 2) + 1;
      float d = dist / steps, k = d * 1.4f / 255.0f;
      for (int s = 0; s < steps; s++) {
        walk(c, d);
        splat(c.f, c.x, c.y, [&](int f, int i, int j, float w) {
          float *p = buf[f][j][i];
          p[0] += col.r * k * w; p[1] += col.g * k * w; p[2] += col.b * k * w;
        });
      }
    }
    FOR_PIXELS {
      const float *p = buf[f][j][i];
      fr.px[f][j][i] = {u8(p[0]), u8(p[1]), u8(p[2])};
    }
    for (Comet &c : cm)  // white-hot heads
      splat(c.f, c.x, c.y, [&](int f, int i, int j, float w) {
        fr.add(f, i, j, scale({255, 255, 255}, w * 1.5f));
      });
  }
};

// Flames climb both side faces and lick over the top edges onto the top face.
// Classic "heat rises and cools" automaton run along the same paths as Rain,
// with the two side faces joined into one ring of 2N columns.
struct Fire : Scene {
  float h[2 * N][2 * N];  // [column][distance up the path]
  float acc = 0;
  const char *name() const override { return "fire"; }
  void enter() override { memset(h, 0, sizeof(h)); acc = 0; }
  static RGB heat(float v) {
    v = clamp01(v);
    return {u8(v * 2.6f), u8(v * 2.6f - 0.9f), u8(v * 3.0f - 2.3f)};
  }
  void step(float tt) {
    // Each cell takes the blurred heat from below, minus a cooling amount read
    // from a noise map that rises with the flames - cool patches cut the fire
    // short, the gaps between them become tongues.
    for (int p = 2 * N - 1; p >= 1; p--)
      for (int g = 0; g < 2 * N; g++) {
        int gl = std::max(0, g - 1), gr = std::min(2 * N - 1, g + 1);
        float below = (h[gl][p - 1] + 2 * h[g][p - 1] + h[gr][p - 1]) * 0.25f;
        float n = vnoise(g * 0.13f, p * 0.085f - tt * 2.0f, tt * 0.5f);
        h[g][p] = std::max(0.0f, below - 0.003f - 0.036f * n * n - frand() * 0.006f);
      }
    float roar = 0.8f + 0.45f * vnoise(tt * 0.35f, 7.0f, 1.0f);
    for (int g = 0; g < 2 * N; g++)
      h[g][0] = clamp01(roar * (0.5f + 0.75f * vnoise(g * 0.11f, tt * 1.1f, 3.0f)) + frand() * 0.15f);
  }
  void render(Frame &fr, double t, float dt) override {
    acc += dt;
    while (acc > 0.03f) { step((float)fmod(t, 3600.0)); acc -= 0.03f; }
    for (int j = 0; j < N; j++)
      for (int i = 0; i < N; i++) {
        fr.px[LEFT][j][i] = heat(h[i][N - 1 - j]);
        fr.px[RIGHT][j][i] = heat(h[N + i][N - 1 - j]);
        fr.px[TOP][j][i] = heat(h[i][2 * N - 1 - j] + h[2 * N - 1 - j][2 * N - 1 - i]);
      }
  }
};

// Shells climb the side faces (and on over the top), then burst into sparks
// that spread across the surface and fall down the sides.
struct Fireworks : Scene {
  struct Shell : Walker { float speed, fuse, hue; };
  struct Spark : Walker { float vx, vy, life, life0; RGB col; };
  std::vector<Shell> shells;
  std::vector<Spark> sparks;
  float buf[3][N][N][3];
  float next = 0;
  const char *name() const override { return "fireworks"; }
  void enter() override {
    memset(buf, 0, sizeof(buf));
    shells.clear();
    sparks.clear();
    next = 0.2f;
  }
  void deposit(const Walker &w, RGB c, float k) {
    k /= 255.0f;
    splat(w.f, w.x, w.y, [&](int f, int i, int j, float wt) {
      float *p = buf[f][j][i];
      p[0] += c.r * k * wt; p[1] += c.g * k * wt; p[2] += c.b * k * wt;
    });
  }
  void burst(const Shell &s) {
    int n = 40 + rnd() % 36;
    float hue2 = s.hue + (rnd() % 2 ? 0.0f : 0.3f + frand() * 0.3f);
    bool ring = rnd() % 3 == 0;
    for (int k = 0; k < n && sparks.size() < 600; k++) {
      Spark p;
      p.f = s.f; p.x = s.x; p.y = s.y;
      p.ang = frand() * 6.2831853f;
      float sp = ring ? 36 + frand() * 5 : 8 + frand() * 46;
      p.vx = cosf(p.ang) * sp;
      p.vy = sinf(p.ang) * sp;
      p.life = p.life0 = 0.9f + frand() * 1.0f;
      p.col = hsv(k & 1 ? s.hue : hue2, 0.85f, 1.0f);
      sparks.push_back(p);
    }
    for (int dj = -3; dj <= 3; dj++)  // flash
      for (int di = -3; di <= 3; di++) {
        int f2, i2, j2, d2 = di * di + dj * dj;
        if (d2 <= 10 && neighbor(s.f, (int)s.x + di, (int)s.y + dj, f2, i2, j2))
          for (int c = 0; c < 3; c++) buf[f2][j2][i2][c] += 1.6f * (1 - d2 / 12.0f);
      }
  }
  void render(Frame &fr, double, float dt) override {
    float keep = expf(-3.2f * dt);
    FOR_PIXELS {
      float *p = buf[f][j][i];
      p[0] *= keep; p[1] *= keep; p[2] *= keep;
    }
    next -= dt;
    if (next <= 0 && shells.size() < 5) {
      Shell s;
      s.f = rnd() % 2 ? LEFT : RIGHT;
      s.x = 5 + frand() * (N - 10);
      s.y = N - 1;
      s.ang = -1.5708f + (frand() - 0.5f) * 0.5f;
      s.speed = 60 + frand() * 25;
      s.fuse = 38 + frand() * 72;
      s.hue = frand();
      shells.push_back(s);
      next = 0.3f + frand() * 1.0f;
    }
    for (size_t k = 0; k < shells.size();) {
      Shell &s = shells[k];
      float dist = s.speed * dt;
      int steps = (int)(dist * 2) + 1;
      float d = dist / steps;
      bool boom = false;
      for (int q = 0; q < steps && !boom; q++) {
        boom = walk(s, d) == 2 || (s.fuse -= d) <= 0;
        deposit(s, {255, 190, 90}, d * 0.5f);
      }
      if (boom) { burst(s); shells[k] = shells.back(); shells.pop_back(); }
      else k++;
    }
    float drag = expf(-1.2f * dt);
    for (size_t k = 0; k < sparks.size();) {
      Spark &p = sparks[k];
      p.life -= dt;
      p.vx *= drag;
      p.vy *= drag;
      if (p.f != TOP) p.vy += 42 * dt;  // gravity only acts on the side faces
      float sp = sqrtf(p.vx * p.vx + p.vy * p.vy);
      p.ang = atan2f(p.vy, p.vx);
      float dist = sp * dt;
      int steps = (int)(dist * 2) + 1;
      float d = dist / steps;
      bool dead = p.life <= 0;
      for (int q = 0; q < steps && !dead; q++) {
        int r = walk(p, d);
        if (r == 2) dead = true;
        deposit(p, p.col, d * 1.3f * p.life / p.life0);
      }
      p.vx = cosf(p.ang) * sp;  // the heading turns when a spark changes face
      p.vy = sinf(p.ang) * sp;
      if (dead) { sparks[k] = sparks.back(); sparks.pop_back(); }
      else k++;
    }
    FOR_PIXELS {
      const float *p = buf[f][j][i];
      fr.px[f][j][i] = {u8(p[0]), u8(p[1]), u8(p[2])};
    }
    for (const Spark &p : sparks)  // twinkling heads
      if (rnd() & 1)
        fr.add(p.f, (int)p.x, (int)p.y, scale({255, 255, 255}, 0.9f * p.life / p.life0));
    for (const Shell &s : shells) fr.add(s.f, (int)s.x, (int)s.y, {255, 240, 200});
  }
};

// Spiral arms winding into the shared corner, as if the cube were draining
// into it. The angle is measured around the cube's long diagonal, so each face
// covers a third of the turn and the arms join up across the edges.
struct Vortex : Scene {
  float A[3][N][N], L[3][N][N];
  bool ready = false;
  const char *name() const override { return "vortex"; }
  void enter() override {
    if (ready) return;
    ready = true;
    FOR_PIXELS {
      const float *p = P[f][j][i];
      float x = 1 - p[0], y = 1 - p[1], z = 1 - p[2];
      A[f][j][i] = atan2f((x + y - 2 * z) * 0.40825f, (x - y) * 0.70711f);
      L[f][j][i] = logf(sqrtf(x * x + y * y + z * z) + 0.04f);
    }
  }
  void render(Frame &fr, double t, float) override {
    float tt = (float)fmod(t, 3600.0);
    float twist = 7.0f + 3.0f * sinf(tt * 0.23f);
    FOR_PIXELS {
      float a = A[f][j][i], l = L[f][j][i];
      float s1 = sinf(3 * a + twist * l - tt * 4.0f);
      float s2 = sinf(-5 * a + 5 * l + tt * 2.2f);
      float b = clamp01(0.5f + 0.5f * s1);
      b = b * b * (0.55f + 0.45f * s2);
      fr.px[f][j][i] = hsv(a * 0.159155f + l * 0.2f - tt * 0.06f, 0.9f, b);
    }
  }
};

// Stained-glass kaleidoscope with the cube corner's own symmetry: the pattern
// depends only on a pixel's two distances from the corner's edges, sorted, so
// all three faces mirror each other (a six-fold figure seen corner-on).
struct Kaleido : Scene {
  const char *name() const override { return "kaleido"; }
  void render(Frame &fr, double t, float) override {
    float tt = (float)fmod(t, 3600.0);
    float cx = 0.5f + 0.35f * sinf(tt * 0.37f), cy = 0.5f + 0.35f * sinf(tt * 0.29f + 2);
    FOR_PIXELS {
      const float *p = P[f][j][i];
      float q0 = 1 - p[0], q1 = 1 - p[1], q2 = 1 - p[2];
      float b = std::max(q0, std::max(q1, q2));
      float a = q0 + q1 + q2 - b - std::min(q0, std::min(q1, q2));
      float w = 0.16f * (vnoise(a * 4 + tt * 0.2f, b * 4 - tt * 0.15f, tt * 0.1f) - 0.5f);
      float x = a + w, y = b + w;
      float d = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
      float v = sinf(x * 17 + tt * 1.1f) + sinf(y * 13 - tt * 0.9f) +
                sinf((x + y) * 9 + tt * 0.6f) + sinf(d * 21 - tt * 2.3f);
      float band = fabsf(sinf(v * 2.2f));
      fr.px[f][j][i] = hsv(v * 0.11f + tt * 0.04f + b * 0.3f, 0.95f, 0.06f + 0.94f * band * band);
    }
  }
};

// The cube as a glass box: shiny balls bounce around inside a neon-gridded
// room, ray-traced for a viewer looking straight at the shared corner.
struct Orbs : Scene {
  struct Ball { float p[3], v[3], r, bounce; RGB col; };
  static const int COUNT = 5;
  Ball ball[COUNT];
  const char *name() const override { return "orbs"; }
  void enter() override {
    float hue0 = frand();
    for (int k = 0; k < COUNT; k++) {
      Ball &b = ball[k];
      b.r = 0.10f + frand() * 0.07f;
      for (int c = 0; c < 3; c++) b.p[c] = b.r + frand() * (1 - 2 * b.r);
      b.v[0] = (0.15f + frand() * 0.3f) * (rnd() & 1 ? 1 : -1);
      b.v[1] = (0.15f + frand() * 0.3f) * (rnd() & 1 ? 1 : -1);
      b.v[2] = 0;
      b.bounce = sqrtf(2 * 1.8f * (0.35f + frand() * 0.4f));  // speed leaving the floor
      b.col = hsv(hue0 + (float)k / COUNT, 0.85f, 1.0f);
    }
  }
  void render(Frame &fr, double t, float dt) override {
    float tt = (float)fmod(t, 3600.0);
    for (Ball &b : ball) {
      b.v[2] -= 1.8f * dt;
      for (int c = 0; c < 3; c++) {
        b.p[c] += b.v[c] * dt;
        if (b.p[c] > 1 - b.r) { b.p[c] = 1 - b.r; b.v[c] = -fabsf(b.v[c]); }
        if (b.p[c] < b.r) { b.p[c] = b.r; b.v[c] = c == 2 ? b.bounce : fabsf(b.v[c]); }
      }
    }
    const float K = 0.57735f;                               // ray direction is -(K,K,K)
    static const float Lt[3] = {0.36f, 0.48f, 0.8f};        // light
    static const float Hf[3] = {0.475f, 0.536f, 0.698f};    // half-vector for the highlight
    static const float wall[3][3] = {{0.5f, 0.0f, 0.6f}, {0.0f, 0.5f, 0.4f}, {0.0f, 0.3f, 0.7f}};
    FOR_PIXELS {
      const float *o = P[f][j][i];
      int hit = -1;
      float best = 1e9f;
      for (int k = 0; k < COUNT; k++) {
        const Ball &b = ball[k];
        float ox = o[0] - b.p[0], oy = o[1] - b.p[1], oz = o[2] - b.p[2];
        float bq = -(ox + oy + oz) * K;
        float disc = bq * bq - (ox * ox + oy * oy + oz * oz - b.r * b.r);
        if (disc <= 0) continue;
        float s = -bq - sqrtf(disc);
        if (s < best) { best = s; hit = k; }
      }
      if (hit >= 0) {
        const Ball &b = ball[hit];
        float n[3];
        for (int c = 0; c < 3; c++) n[c] = (o[c] - K * best - b.p[c]) / b.r;
        float diff = std::max(0.0f, n[0] * Lt[0] + n[1] * Lt[1] + n[2] * Lt[2]);
        float sp = std::max(0.0f, n[0] * Hf[0] + n[1] * Hf[1] + n[2] * Hf[2]);
        sp *= sp; sp *= sp; sp *= sp; sp *= sp; sp *= sp;
        float k = 0.12f + 0.88f * diff;
        fr.px[f][j][i] = {u8(b.col.r / 255.0f * k + sp), u8(b.col.g / 255.0f * k + sp),
                          u8(b.col.b / 255.0f * k + sp)};
        continue;
      }
      // Back walls: the ray stops where its smallest coordinate reaches zero.
      int ax = o[0] < o[1] ? (o[0] < o[2] ? 0 : 2) : (o[1] < o[2] ? 1 : 2);
      float m = o[ax];
      float u = o[(ax + 1) % 3] - m, v = o[(ax + 2) % 3] - m;
      float gu = u * 8 - floorf(u * 8), gv = v * 8 - floorf(v * 8);
      bool line = fabsf(gu - 0.5f) > 0.43f || fabsf(gv - 0.5f) > 0.43f;
      float k = (1 - 0.6f * m) * (line ? 0.65f + 0.35f * sinf((u + v) * 9 - tt * 2.5f) : 0.10f);
      if (ax == 2)  // floor: shadows straight below each ball
        for (const Ball &b : ball) {
          float dx = o[0] - m - b.p[0], dy = o[1] - m - b.p[1];
          if (dx * dx + dy * dy < b.r * b.r) k *= 0.25f;
        }
      fr.px[f][j][i] = {u8(wall[ax][0] * k), u8(wall[ax][1] * k), u8(wall[ax][2] * k)};
    }
  }
};

// Thunderstorm: forked lightning starts on the top face, runs over an edge and
// down a side to the ground, lighting up drifting clouds as it goes.
struct Storm : Scene {
  struct Drop { int f; float i, j, speed; };
  Drop rain[70];
  float bolt[3][N][N];
  float flash = 0, next = 0, again = -1;
  uint32_t seed = 1;
  const char *name() const override { return "storm"; }
  void enter() override {
    memset(bolt, 0, sizeof(bolt));
    flash = 0;
    next = 0.4f;
    again = -1;
    for (Drop &d : rain) d = {rnd() & 1 ? LEFT : RIGHT, frand() * N, frand() * N, 80 + frand() * 70};
  }
  void fork(Walker w, float len, float power, int depth) {
    while (len > 0) {
      float dev = (frand() - 0.5f) * 1.9f, seg = 2 + frand() * 4;
      w.ang += dev;
      for (float s = 0; s < seg; s += 0.5f) {
        if (walk(w, 0.5f) == 2) return;
        splat(w.f, w.x, w.y, [&](int f, int i, int j, float wt) {
          bolt[f][j][i] += power * wt * 0.9f;
        });
      }
      w.ang -= dev;
      len -= seg;
      if (depth < 3 && frand() < 0.16f) {
        Walker c = w;
        c.ang += (rnd() & 1 ? 1 : -1) * (0.4f + frand() * 0.5f);
        fork(c, len * (0.25f + frand() * 0.4f), power * 0.55f, depth + 1);
      }
    }
  }
  void strike() {
    Walker w;
    w.f = TOP;
    if (rnd() & 1) { w.x = 6 + frand() * (N - 12); w.y = frand() * 30; w.ang = 1.5708f; }
    else { w.x = frand() * 30; w.y = 6 + frand() * (N - 12); w.ang = 0; }
    fork(w, 190, 1.0f, 0);
  }
  void render(Frame &fr, double t, float dt) override {
    float tt = (float)fmod(t, 3600.0);
    float keep = expf(-3.5f * dt);
    flash *= expf(-3.0f * dt);
    FOR_PIXELS bolt[f][j][i] *= keep;
    next -= dt;
    if (next <= 0) {
      seed = rng_state;
      strike();
      flash = 1;
      again = frand() < 0.7f ? 0.08f + frand() * 0.08f : -1;
      next = 0.35f + frand() * 1.5f;
    } else if (again > 0 && (again -= dt) <= 0) {  // the same bolt flickers once more
      uint32_t keep_rng = rng_state;
      rng_state = seed;
      strike();
      rng_state = keep_rng;
      flash = 0.8f;
      again = -1;
    }
    FOR_PIXELS {
      const float *p = P[f][j][i];
      float n = vnoise(p[0] * 3 + tt * 0.12f, p[1] * 3 - tt * 0.09f, p[2] * 3 + tt * 0.05f);
      float cloud = n * n * (0.28f + 0.9f * flash), v = bolt[f][j][i];
      fr.px[f][j][i] = {u8(cloud * 0.45f + v * 0.85f), u8(cloud * 0.4f + v * 0.9f),
                        u8(cloud * 0.9f + v * 1.3f)};
    }
    RGB wet = scale({70, 100, 170}, 0.45f + 0.55f * flash);
    for (Drop &d : rain) {  // rain streaks down the two side faces
      d.j += d.speed * dt;
      if (d.j > N + 4) d = {rnd() & 1 ? LEFT : RIGHT, frand() * N, -frand() * 20, 80 + frand() * 70};
      for (int k = 0; k < 4; k++) fr.add(d.f, (int)d.i, (int)d.j - k, scale(wet, 1 - k * 0.25f));
    }
  }
};

// The cube as a tank half full of water that keeps sloshing: the side faces
// show the tilting waterline, the top looks down onto the surface.
struct Tank : Scene {
  struct Bubble { int f; float i, z, speed, phase; };
  Bubble bub[28];
  const char *name() const override { return "tank"; }
  static float level(float x, float y, float tt) {
    return 0.56f + 0.20f * sinf(tt * 0.8f) * (x - 0.5f) + 0.20f * sinf(tt * 0.63f + 1.3f) * (y - 0.5f) +
           0.018f * sinf(x * 13 + tt * 2.6f) + 0.018f * sinf(y * 11 - tt * 2.1f);
  }
  // Wobbling net of bright lines, like sunlight on the bottom of a pool.
  static float caustic(float x, float y, float ph) {
    float w = 1 - fabsf(sinf(x * 1.2f + 1.7f * sinf(y * 0.8f + ph)) *
                        sinf(y * 1.1f + 1.7f * sinf(x * 0.7f - ph * 0.8f)));
    w *= w; w *= w;
    return w * w;
  }
  void respawn(Bubble &b, bool anywhere) {
    b.f = rnd() & 1 ? LEFT : RIGHT;
    b.i = frand() * N;
    b.z = anywhere ? frand() * 0.5f : 0;
    b.speed = 0.10f + frand() * 0.2f;
    b.phase = frand() * 6.28f;
  }
  void enter() override { for (Bubble &b : bub) respawn(b, true); }
  void render(Frame &fr, double t, float dt) override {
    float tt = (float)fmod(t, 3600.0);
    FOR_PIXELS {
      const float *p = P[f][j][i];
      float h = level(p[0], p[1], tt);
      if (f == TOP) {
        float w = caustic(p[0] * 10, p[1] * 10, tt * 0.6f);
        float k = 0.3f + 0.7f * clamp01((h - 0.36f) / 0.4f);
        fr.px[f][j][i] = {u8(w * 0.5f * k), u8((0.15f + 0.65f * w) * k), u8((0.4f + 0.55f * w) * k)};
        continue;
      }
      float dep = h - p[2];
      if (dep < 0) fr.px[f][j][i] = {0, 0, 0};
      else if (dep < 0.03f) fr.px[f][j][i] = scale({190, 255, 255}, 1 - dep * 12);
      else {
        float w = caustic((p[0] - p[1]) * 9, p[2] * 9, tt * 0.6f);
        float k = expf(-dep * 2.0f);
        fr.px[f][j][i] = {u8(0.3f * w * k), u8((0.22f + 0.5f * w) * k), u8((0.5f + 0.45f * w) * k + 0.08f)};
      }
    }
    for (Bubble &b : bub) {
      b.z += b.speed * dt;
      float i = b.i + 1.5f * sinf(tt * 3 + b.phase), a = (i + 0.5f) / N;
      if (b.z > level(b.f == LEFT ? 1 : 1 - a, b.f == LEFT ? a : 1, tt) - 0.03f) { respawn(b, false); continue; }
      int j = (int)((1 - b.z) * N);
      fr.add(b.f, (int)i, j, {150, 220, 255});
      fr.add(b.f, (int)i, j - 1, {50, 80, 100});
    }
  }
};

static const char *weather_text(int code) {
  if (code == 0) return "CLEAR";
  if (code <= 2) return "P.CLOUDY";
  if (code == 3) return "OVERCAST";
  if (code == 45 || code == 48) return "FOG";
  if (code >= 51 && code <= 57) return "DRIZZLE";
  if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82)) return "RAIN";
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return "SNOW";
  if (code >= 95) return "STORM";
  return "";
}

static void draw_cloud(Frame &fr, int x, int y, RGB c) {
  fr.disc(TOP, x + 7, y + 9, 5, c);
  fr.disc(TOP, x + 13, y + 6, 6, c);
  fr.disc(TOP, x + 19, y + 9, 5, c);
  fr.rect(TOP, x + 7, y + 9, 13, 6, c);
}

// 26x26 icon on the top face.
static void weather_icon(Frame &fr, int x, int y, const Weather &w, double t) {
  const RGB sun = {255, 190, 0}, cloud = {150, 160, 175}, dark = {90, 95, 110};
  int c = w.code;
  bool rain = (c >= 51 && c <= 67) || (c >= 80 && c <= 82);
  bool snow = (c >= 71 && c <= 77) || c == 85 || c == 86;
  if (c <= 2) {
    if (w.is_day) {
      float cx = x + (c ? 9 : 13), cy = y + (c ? 9 : 13);
      for (int k = 0; k < 8; k++) {
        float a = k * 0.7854f + (float)t * 0.3f;
        fr.line(TOP, (int)(cx + 8 * cosf(a)), (int)(cy + 8 * sinf(a)),
                (int)(cx + 11 * cosf(a)), (int)(cy + 11 * sinf(a)), sun);
      }
      fr.disc(TOP, cx, cy, 6, sun);
    } else {
      float cx = x + (c ? 9 : 13), cy = y + (c ? 9 : 13);
      fr.disc(TOP, cx, cy, 7, {200, 200, 255});
      fr.disc(TOP, cx + 4, cy - 3, 6, {0, 0, 0});
    }
    if (c) draw_cloud(fr, x, y + 8, cloud);
  } else if (c == 45 || c == 48) {
    for (int k = 0; k < 5; k++)
      fr.rect(TOP, x + 2 + (k & 1) * 3, y + 4 + k * 4, 19, 2, cloud);
  } else {
    draw_cloud(fr, x, y + 1, (rain || snow || c >= 95) ? dark : cloud);
    int ph = (int)(t * 8);
    if (c >= 95) {
      fr.line(TOP, x + 14, y + 16, x + 10, y + 20, sun);
      fr.line(TOP, x + 10, y + 20, x + 15, y + 20, sun);
      fr.line(TOP, x + 15, y + 20, x + 11, y + 25, sun);
    } else if (rain) {
      for (int k = 0; k < 4; k++) {
        int o = (ph + k * 3) % 8;
        fr.line(TOP, x + 7 + k * 4, y + 17 + o, x + 6 + k * 4, y + 19 + o, {60, 130, 255});
      }
    } else if (snow) {
      for (int k = 0; k < 4; k++)
        fr.set(TOP, x + 6 + k * 4, y + 17 + (ph / 2 + k * 3) % 9, {255, 255, 255});
    }
  }
}

struct Clock : Scene {
  const char *name() const override { return "clock"; }
  void render(Frame &fr, double t, float) override {
    fr.clear();
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    bool h24 = cfgd("clock_24h", 0) != 0;
    int hour = h24 ? tm.tm_hour : (tm.tm_hour % 12 ? tm.tm_hour % 12 : 12);

    // Hours on the left face, minutes on the right, drawn as a white mask and
    // then coloured by a gradient that runs through 3D space.
    const RGB W = {255, 255, 255};
    if (hour >= 10 || h24) seg7(fr, LEFT, 4, 8, 25, 48, 6, hour / 10, W);
    seg7(fr, LEFT, 35, 8, 25, 48, 6, hour % 10, W);
    seg7(fr, RIGHT, 4, 8, 25, 48, 6, tm.tm_min / 10, W);
    seg7(fr, RIGHT, 35, 8, 25, 48, 6, tm.tm_min % 10, W);
    float base = (float)fmod(t * 0.01, 1.0);
    for (int f = LEFT; f <= RIGHT; f++)
      for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++)
          if (fr.px[f][j][i].r) {
            const float *p = P[f][j][i];
            fr.px[f][j][i] = hsv(base + 0.22f * p[2] + 0.1f * (p[0] - p[1]), 0.85f, 1.0f);
          }
    RGB accent = hsv(base + 0.25f, 0.6f, 1.0f);

    // Top face: date, weather, and a seconds comet running round the border.
    char buf[32];
    strftime(buf, sizeof(buf), "%a %b %-d", &tm);
    for (char *c = buf; *c; c++) *c = toupper(*c);
    Weather w;
    {
      std::lock_guard<std::mutex> lk(G.mu);
      w = G.weather;
    }
    if (w.valid) {
      text_centered(&fr, TOP, font_med, 5, accent, buf);
      weather_icon(fr, 4, 19, w, t);
      snprintf(buf, sizeof(buf), "%d°", (int)lroundf(w.temp));
      text(&fr, TOP, font_big, 33, 22, {255, 255, 255}, buf);
      text_centered(&fr, TOP, font_small, 50, {130, 140, 160}, weather_text(w.code));
    } else {
      text_centered(&fr, TOP, font_med, 12, accent, buf);
      snprintf(buf, sizeof(buf), "%02d", tm.tm_sec);
      text_centered(&fr, TOP, font_big, 30, {255, 255, 255}, buf);
    }
    float pos = (tm.tm_sec + ts.tv_nsec * 1e-9f) / 60.0f * 252.0f;
    for (int k = 0; k < 16; k++) {
      int q = ((int)pos - k + 252) % 252, i, j;
      if (q < 63) { i = q; j = 0; }
      else if (q < 126) { i = 63; j = q - 63; }
      else if (q < 189) { i = 63 - (q - 126); j = 63; }
      else { i = 0; j = 63 - (q - 189); }
      fr.set(TOP, i, j, scale(accent, 1.0f - k / 16.0f));
    }
  }
};

static RGB load_color(float pct) {
  return hsv(0.36f * (1.0f - clamp01(pct / 100.0f)), 1.0f, 1.0f);
}

struct StatsScene : Scene {
  const char *name() const override { return "stats"; }
  static void panel(Frame &fr, int f, const char *label, float v, const float *hist,
                    const char *extra) {
    RGB c = load_color(v);
    text(&fr, f, font_med, 2, 1, {150, 160, 180}, label);
    if (extra) text(&fr, f, font_med, N - 1 - text_width(font_med, extra), 1, {150, 160, 180}, extra);
    int n = (int)lroundf(v), x = 4;
    n = std::max(0, std::min(100, n));
    if (n >= 100) seg7(fr, f, x, 13, 13, 24, 3, 1, c);
    if (n >= 10) seg7(fr, f, x + 16, 13, 13, 24, 3, (n / 10) % 10, c);
    seg7(fr, f, x + 32, 13, 13, 24, 3, n % 10, c);
    text(&fr, f, font_med, x + 48, 27, c, "%");
    for (int i = 0; i < N; i++) {
      int h = (int)(clamp01(hist[i] / 100.0f) * 23) + 1;
      RGB hc = load_color(hist[i]);
      for (int k = 0; k < h; k++) fr.set(f, i, N - 1 - k, scale(hc, k == h - 1 ? 1.0f : 0.3f));
    }
  }
  static void bar(Frame &fr, int y, const char *label, float pct) {
    text(&fr, TOP, font_small, 1, y, {150, 160, 180}, label);
    fr.rect(TOP, 18, y, 45, 7, {20, 22, 28});
    fr.rect(TOP, 18, y, (int)(45 * clamp01(pct / 100.0f)), 7, load_color(pct));
  }
  void render(Frame &fr, double, float) override {
    fr.clear();
    Stats s;
    {
      std::lock_guard<std::mutex> lk(G.mu);
      s = G.stats;
    }
    char buf[32];
    panel(fr, LEFT, "CPU", s.cpu, s.cpu_hist, NULL);
    snprintf(buf, sizeof(buf), "%d°C", (int)lroundf(s.gput));
    panel(fr, RIGHT, "GPU", s.gpu, s.gpu_hist, s.gput > 0 ? buf : NULL);
    bar(fr, 1, "RAM", s.ram);
    bar(fr, 10, "VRM", s.vram);
    snprintf(buf, sizeof(buf), "D%.1f U%.1f", s.down, s.up);
    text(&fr, TOP, font_small, 1, 20, {90, 170, 255}, buf);
    int nc = (int)s.cores.size();
    if (nc > 0) {
      int bw = std::max(1, N / nc), x0 = (N - bw * nc) / 2;
      for (int k = 0; k < nc && k < N; k++) {
        int h = (int)(clamp01(s.cores[k] / 100.0f) * 33) + 1;
        fr.rect(TOP, x0 + k * bw, N - h, bw > 2 ? bw - 1 : bw, h, load_color(s.cores[k]));
      }
    }
  }
};

struct Visualizer : Scene {
  float level[NBANDS] = {0}, peak[NBANDS] = {0};
  float hist[N][NBANDS];
  int head = 0;
  float acc = 0;
  const char *name() const override { return "visualizer"; }
  void enter() override { memset(hist, 0, sizeof(hist)); }
  void render(Frame &fr, double t, float dt) override {
    fr.clear();
    float target[NBANDS];
    {
      std::lock_guard<std::mutex> lk(G.mu);
      bool live = now_seconds() - G.audio_stamp < 0.4;
      for (int b = 0; b < NBANDS; b++) target[b] = live ? G.bands[b] : 0;
    }
    for (int b = 0; b < NBANDS; b++) {
      if (target[b] > level[b]) level[b] += (target[b] - level[b]) * std::min(1.0f, dt * 30);
      else level[b] = std::max(target[b], level[b] - dt * 1.6f);
      peak[b] = level[b] > peak[b] ? level[b] : std::max(0.0f, peak[b] - dt * 0.35f);
    }
    acc += dt;
    if (acc > 0.03f) {
      acc = 0;
      head = (head + N - 1) % N;
      memcpy(hist[head], level, sizeof(level));
    }
    float hue0 = (float)fmod(t * 0.015, 1.0);
    for (int b = 0; b < NBANDS; b++) {
      float hue = hue0 + b * 0.022f;
      int h = (int)(level[b] * 61), pk = (int)(peak[b] * 61) + 1;
      for (int w = 0; w < 2; w++) {
        int il = N - 1 - (2 * b + w), ir = 2 * b + w;  // bass sits on the shared edge
        for (int k = 0; k <= h; k++) {
          RGB c = hsv(hue + k * 0.002f, 1.0f, 0.35f + 0.65f * k / 61.0f);
          fr.set(LEFT, il, N - 1 - k, c);
          fr.set(RIGHT, ir, N - 1 - k, c);
        }
        fr.set(LEFT, il, N - 1 - pk, {255, 255, 255});
        fr.set(RIGHT, ir, N - 1 - pk, {255, 255, 255});
      }
    }
    // Top face: spectrum history flowing away from both edges, meeting on the diagonal.
    for (int j = 0; j < N; j++)
      for (int i = 0; i < N; i++) {
        int b, a;
        if (j >= i) { b = (N - 1 - i) / 2; a = N - 1 - j; }
        else { b = (N - 1 - j) / 2; a = N - 1 - i; }
        float v = hist[(head + a) % N][b];
        fr.px[TOP][j][i] = hsv(hue0 + b * 0.022f, 1.0f, v * v * (1.0f - a / 80.0f));
      }
  }
};

// Raw view of the chain: panel number, plus an arrow pointing at that panel's
// own "up". Used once to work out the face mapping.
struct Identify : Scene {
  const char *name() const override { return "identify"; }
  bool raw() const override { return true; }
  void render(Frame &fr, double, float) override {
    fr.clear();
    static const RGB cols[3] = {{255, 40, 40}, {40, 255, 40}, {60, 90, 255}};
    for (int p = 0; p < 3; p++) {
      fr.rect(p, 0, 0, N, 2, {255, 255, 255});
      for (int k = 0; k < 9; k++) fr.rect(p, 31 - k, 5 + k, 2 * k + 2, 1, {255, 255, 255});
      fr.rect(p, 29, 14, 6, 8, {255, 255, 255});
      seg7(fr, p, 22, 26, 20, 34, 5, p + 1, cols[p]);
    }
  }
};

// Mapped view: every shared edge gets its own colour, so a wrong mapping shows
// up as an edge whose two sides do not match.
struct Calibrate : Scene {
  const char *name() const override { return "calibrate"; }
  void render(Frame &fr, double, float) override {
    fr.clear();
    const RGB red = {255, 0, 0}, green = {0, 255, 0}, blue = {0, 80, 255};
    fr.rect(TOP, 0, N - 3, N, 3, red);    fr.rect(LEFT, 0, 0, N, 3, red);
    fr.rect(TOP, N - 3, 0, 3, N, green);  fr.rect(RIGHT, 0, 0, N, 3, green);
    fr.rect(LEFT, N - 3, 0, 3, N, blue);  fr.rect(RIGHT, 0, 0, 3, N, blue);
    fr.rect(TOP, N - 6, N - 6, 6, 6, {255, 255, 255});
    fr.rect(LEFT, N - 6, 0, 6, 6, {255, 255, 255});
    fr.rect(RIGHT, 0, 0, 6, 6, {255, 255, 255});
    text_centered(&fr, TOP, font_big, 22, {255, 255, 255}, "TOP");
    text_centered(&fr, LEFT, font_big, 22, {255, 255, 255}, "LEFT");
    text_centered(&fr, RIGHT, font_big, 22, {255, 255, 255}, "RIGHT");
  }
};

// ------------------------------------------------------ network & weather

static int g_sock = -1;
static volatile bool g_quit = false;

static void handle_stats(const char *s) {
  std::lock_guard<std::mutex> lk(G.mu);
  Stats &st = G.stats;
  char key[32], val[400];
  int used;
  while (sscanf(s, " %31[^= ]=%399s%n", key, val, &used) == 2) {
    s += used;
    float v = (float)atof(val);
    if (!strcmp(key, "cpu")) st.cpu = v;
    else if (!strcmp(key, "ram")) st.ram = v;
    else if (!strcmp(key, "gpu")) st.gpu = v;
    else if (!strcmp(key, "gput")) st.gput = v;
    else if (!strcmp(key, "vram")) st.vram = v;
    else if (!strcmp(key, "down")) st.down = v;
    else if (!strcmp(key, "up")) st.up = v;
    else if (!strcmp(key, "cores")) {
      st.cores.clear();
      for (char *tok = strtok(val, ","); tok && st.cores.size() < 64; tok = strtok(NULL, ","))
        st.cores.push_back((float)atof(tok));
    }
  }
  memmove(st.cpu_hist, st.cpu_hist + 1, sizeof(float) * (N - 1));
  memmove(st.gpu_hist, st.gpu_hist + 1, sizeof(float) * (N - 1));
  st.cpu_hist[N - 1] = st.cpu;
  st.gpu_hist[N - 1] = st.gpu;
  st.stamp = now_seconds();
}

static void net_thread() {
  char buf[1500];
  while (!g_quit) {
    sockaddr_in from;
    socklen_t fl = sizeof(from);
    ssize_t n = recvfrom(g_sock, buf, sizeof(buf) - 1, 0, (sockaddr *)&from, &fl);
    if (n <= 0) continue;  // timeout
    buf[n] = 0;
    if (buf[0] == 'A' && n >= 1 + NBANDS) {
      std::lock_guard<std::mutex> lk(G.mu);
      for (int b = 0; b < NBANDS; b++) G.bands[b] = (uint8_t)buf[1 + b] / 255.0f;
      G.audio_stamp = now_seconds();
    } else if (buf[0] == 'S' && buf[1] == ' ') {
      handle_stats(buf + 2);
    } else if (buf[0] == 'C' && buf[1] == ' ') {
      std::string cmd(buf + 2);
      while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == '\r' || cmd.back() == ' '))
        cmd.pop_back();
      std::lock_guard<std::mutex> lk(G.mu);
      if (G.commands.size() < 32) G.commands.push_back({cmd, from});
    }
  }
}

static void fetch_weather() {
  std::string lat = cfg("latitude", ""), lon = cfg("longitude", "");
  if (lat.empty() || lon.empty()) {
    std::lock_guard<std::mutex> lk(G.mu);
    G.weather.valid = false;
    return;
  }
  bool metric = cfg("units", "F") == "C";
  char cmd[512];
  snprintf(cmd, sizeof(cmd),
           "curl -s -m 15 'https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
           "&current=temperature_2m,weather_code,is_day&temperature_unit=%s'",
           atof(lat.c_str()), atof(lon.c_str()), metric ? "celsius" : "fahrenheit");
  FILE *p = popen(cmd, "r");
  if (!p) return;
  std::string out;
  char buf[512];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), p)) > 0) out.append(buf, n);
  pclose(p);
  size_t cur = out.find("\"current\":");
  if (cur == std::string::npos) return;
  auto num = [&](const char *key, double &v) {
    size_t k = out.find(key, cur);
    if (k == std::string::npos) return false;
    v = atof(out.c_str() + k + strlen(key));
    return true;
  };
  double temp, code, day;
  if (num("\"temperature_2m\":", temp) && num("\"weather_code\":", code) &&
      num("\"is_day\":", day)) {
    std::lock_guard<std::mutex> lk(G.mu);
    G.weather = {true, (float)temp, (int)code, day != 0};
  }
}

static volatile bool g_weather_now = true;
static void weather_thread() {
  double last = -1e9;
  while (!g_quit) {
    if (g_weather_now || now_seconds() - last > 600) {
      g_weather_now = false;
      last = now_seconds();
      fetch_weather();
    }
    usleep(500 * 1000);
  }
}

// ---------------------------------------------------------- face mapping

static uint8_t map_x[3][N][N], map_y[3][N][N];

// Config "top=0:90" means: the top face is panel 0 of the chain, and that
// panel's own "up" points 90 degrees clockwise from the face's up, as viewed.
static void build_mapping() {
  static const char *keys[3] = {"top", "left", "right"};
  for (int f = 0; f < 3; f++) {
    char def[8];
    snprintf(def, sizeof(def), "%d:0", f);
    std::string v = cfg(keys[f], def);
    int panel = f, rot = 0;
    sscanf(v.c_str(), "%d:%d", &panel, &rot);
    panel = std::max(0, std::min(2, panel));
    rot = ((rot / 90) % 4 + 4) % 4;
    for (int j = 0; j < N; j++)
      for (int i = 0; i < N; i++) {
        int px, py;
        switch (rot) {
          case 1: px = j; py = N - 1 - i; break;
          case 2: px = N - 1 - i; py = N - 1 - j; break;
          case 3: px = N - 1 - j; py = i; break;
          default: px = i; py = j; break;
        }
        map_x[f][j][i] = panel * N + px;
        map_y[f][j][i] = py;
      }
  }
}

// ------------------------------------------------------------------- main

static void write_ppm(const Frame &fr, const char *path) {
  FILE *fp = fopen(path, "wb");
  if (!fp) { perror(path); return; }
  fprintf(fp, "P6\n%d %d\n255\n", 3 * N, N);
  for (int j = 0; j < N; j++)
    for (int f = 0; f < 3; f++) fwrite(fr.px[f][j], 3, N, fp);
  fclose(fp);
}

static void on_signal(int) { g_quit = true; }

int main(int argc, char **argv) {
  const char *dump = NULL, *start_scene = NULL;
  int dump_frames = 200;
  std::vector<char *> rest = {argv[0]};
  for (int a = 1; a < argc; a++) {
    if (!strcmp(argv[a], "--config") && a + 1 < argc) conf_path = argv[++a];
    else if (!strcmp(argv[a], "--dump") && a + 1 < argc) dump = argv[++a];
    else if (!strcmp(argv[a], "--scene") && a + 1 < argc) start_scene = argv[++a];
    else if (!strcmp(argv[a], "--frames") && a + 1 < argc) dump_frames = atoi(argv[++a]);
    else rest.push_back(argv[a]);
  }
  load_config();
  init_geometry();
  build_mapping();
  rng_state ^= (uint32_t)time(NULL);

  std::string fd = cfg("font_dir", "../rpi-rgb-led-matrix/fonts");
  if (!font_small.LoadFont((fd + "/5x7.bdf").c_str()) ||
      !font_med.LoadFont((fd + "/6x10.bdf").c_str()) ||
      !font_big.LoadFont((fd + "/10x20.bdf").c_str())) {
    fprintf(stderr, "ledcube: cannot load fonts from %s\n", fd.c_str());
    return 1;
  }

  std::vector<Scene *> scenes = {new Plasma, new Ripples, new Lava, new Rain, new Life,
                                 new Warp, new Comets, new Fire, new Fireworks, new Vortex,
                                 new Kaleido, new Orbs, new Storm, new Tank,
                                 new Clock, new StatsScene, new Visualizer,
                                 new Identify, new Calibrate};
  auto find = [&](const std::string &n) -> Scene * {
    for (Scene *s : scenes) if (n == s->name()) return s;
    return NULL;
  };

  static Frame cur_frame, prev_frame;

  if (dump) {  // render one scene off-screen with fake data and save it
    Scene *s = find(start_scene ? start_scene : "plasma");
    if (!s) { fprintf(stderr, "unknown scene\n"); return 1; }
    G.weather = {true, 54, 61, true};
    s->enter();
    for (int k = 0; k < dump_frames; k++) {
      double t = k * 0.025;
      char st[256];
      snprintf(st, sizeof(st), "cpu=%.0f ram=61 gpu=%.0f gput=67 vram=38 down=12.4 up=0.8 "
               "cores=30,80,12,55,9,100,42,66,20,35,71,5,90,48,15,60",
               50 + 45 * sin(k * 0.21), 50 + 45 * cos(k * 0.13));
      if (k % 4 == 0) handle_stats(st);
      for (int b = 0; b < NBANDS; b++)
        G.bands[b] = clamp01((0.9f - b * 0.02f) * (0.5f + 0.5f * sinf(k * 0.3f + b * 0.9f)));
      G.audio_stamp = now_seconds();
      s->render(cur_frame, t, 0.025f);
    }
    write_ppm(cur_frame, dump);
    return 0;
  }

  RGBMatrix::Options opt;
  rgb_matrix::RuntimeOptions rt;
  std::string mapping = cfg("gpio_mapping", "adafruit-hat");
  std::string panel_type = cfg("panel_type", "");
  opt.hardware_mapping = mapping.c_str();
  opt.rows = N;
  opt.cols = N;
  opt.chain_length = 3;
  opt.pwm_bits = (int)cfgd("pwm_bits", 9);
  opt.pwm_dither_bits = (int)cfgd("pwm_dither_bits", 0);
  opt.pwm_lsb_nanoseconds = (int)cfgd("pwm_lsb_nanoseconds", 130);
  opt.limit_refresh_rate_hz = (int)cfgd("limit_refresh_hz", 0);
  opt.show_refresh_rate = cfgd("show_refresh", 0) != 0;
  if (!panel_type.empty()) opt.panel_type = panel_type.c_str();
  rt.gpio_slowdown = (int)cfgd("gpio_slowdown", 2);
  int rargc = (int)rest.size();
  char **rargv = rest.data();
  if (!rgb_matrix::ParseOptionsFromFlags(&rargc, &rargv, &opt, &rt)) {
    rgb_matrix::PrintMatrixFlags(stderr);
    return 1;
  }
  RGBMatrix *matrix = RGBMatrix::CreateFromOptions(opt, rt);
  if (!matrix) return 1;
  FrameCanvas *canvas = matrix->CreateFrameCanvas();

  // Measure the panel refresh rate once: back-to-back swaps take one refresh each.
  double refresh_hz = 0;
  {
    for (int k = 0; k < 20; k++) canvas = matrix->SwapOnVSync(canvas);
    double a = now_seconds();
    for (int k = 0; k < 150; k++) canvas = matrix->SwapOnVSync(canvas);
    refresh_hz = 150.0 / (now_seconds() - a);
  }

  signal(SIGINT, on_signal);
  signal(SIGTERM, on_signal);

  g_sock = socket(AF_INET, SOCK_DGRAM, 0);
  sockaddr_in addr = {};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons((int)cfgd("port", 7777));
  if (bind(g_sock, (sockaddr *)&addr, sizeof(addr)) < 0) perror("ledcube: bind");
  struct timeval tv = {1, 0};
  setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
  std::thread net(net_thread), wx(weather_thread);

  // Auto mode rotates through this list; stats is skipped while the PC is
  // silent, and music takes over with the visualizer.
  std::vector<std::string> playlist;
  auto load_playlist = [&]() {
    playlist.clear();
    std::string pl = cfg("playlist",
                         "clock,plasma,stats,fire,clock,ripples,stats,vortex,clock,lava,stats,fireworks,"
                         "clock,rain,stats,orbs,clock,life,stats,kaleido,clock,warp,stats,storm,"
                         "clock,comets,stats,tank");
    size_t a = 0;
    while (a <= pl.size()) {
      size_t b = pl.find(',', a);
      if (b == std::string::npos) b = pl.size();
      std::string n = pl.substr(a, b - a);
      n.erase(0, n.find_first_not_of(' '));
      n.erase(n.find_last_not_of(' ') + 1);
      if (find(n)) playlist.push_back(n);
      a = b + 1;
    }
    if (playlist.empty()) playlist.push_back("plasma");
  };
  load_playlist();

  bool autom = true, on = true;
  int pl_index = -1;
  int brightness = (int)cfgd("brightness", 45);
  Scene *cur = NULL, *prev = NULL;
  double t0 = now_seconds(), scene_start = 0, fade_start = -10, last = 0, fps = 0;
  double music_until = -1;
  const double FADE = 1.0;

  auto switch_to = [&](Scene *s, double t) {
    if (!s || s == cur) return;
    prev = cur;
    cur = s;
    cur->enter();
    scene_start = t;
    fade_start = prev ? t : -10;
  };
  auto advance = [&](double t) {
    bool stats_live = now_seconds() - G.stats.stamp < 6;
    for (size_t k = 0; k < playlist.size(); k++) {
      pl_index = (pl_index + 1) % (int)playlist.size();
      const std::string &n = playlist[pl_index];
      if (n == "stats" && !stats_live) continue;
      if (cur && n == cur->name() && playlist.size() > 1) continue;
      switch_to(find(n), t);
      return;
    }
  };
  if (start_scene && find(start_scene)) { autom = false; switch_to(find(start_scene), 0); }
  else advance(0);

  while (!g_quit) {
    double t = now_seconds() - t0;
    float dt = (float)std::min(0.1, t - last);
    if (dt > 0) fps = fps * 0.95 + 0.05 / dt;
    last = t;

    // commands
    for (;;) {
      std::pair<std::string, sockaddr_in> c;
      {
        std::lock_guard<std::mutex> lk(G.mu);
        if (G.commands.empty()) break;
        c = G.commands.front();
        G.commands.pop_front();
      }
      const std::string &cmd = c.first;
      std::string err;
      if (cmd.rfind("scene=", 0) == 0) {
        Scene *s = find(cmd.substr(6));
        if (s) { autom = false; on = true; switch_to(s, t); }
        else err = "unknown scene; ";
      } else if (cmd == "auto") { autom = true; on = true; advance(t); }
      else if (cmd == "next") { autom = true; on = true; advance(t); }
      else if (cmd.rfind("brightness=", 0) == 0)
        brightness = std::max(1, std::min(100, atoi(cmd.c_str() + 11)));
      else if (cmd == "off") on = false;
      else if (cmd == "on") on = true;
      else if (cmd == "reload") {
        load_config();
        build_mapping();
        load_playlist();
        brightness = (int)cfgd("brightness", 45);
        g_weather_now = true;
      } else if (cmd != "status") err = "unknown command; ";
      char reply[512];
      std::string names;
      for (Scene *s : scenes) names += std::string(s->name()) + " ";
      double ns = now_seconds();
      snprintf(reply, sizeof(reply),
               "%sscene=%s mode=%s power=%s brightness=%d fps=%.0f refresh=%.0fHz stats=%s audio=%s weather=%s\nscenes: %s",
               err.c_str(), cur ? cur->name() : "-", autom ? "auto" : "pinned", on ? "on" : "off",
               brightness, fps, refresh_hz, ns - G.stats.stamp < 6 ? "live" : "none",
               ns - G.audio_stamp < 3 ? "live" : "none", G.weather.valid ? "ok" : "none",
               names.c_str());
      sendto(g_sock, reply, strlen(reply), 0, (sockaddr *)&c.second, sizeof(c.second));
    }

    // director
    if (autom) {
      bool music = cfgd("audio_takeover", 1) != 0 && now_seconds() - G.audio_stamp < 1.0;
      if (music) music_until = t + cfgd("audio_hold_seconds", 8);
      if (t < music_until) {
        switch_to(find("visualizer"), t);
      } else {
        double dur = cfgd("ambient_seconds", 60);
        if (cur && !strcmp(cur->name(), "clock")) dur = cfgd("clock_seconds", 25);
        if (cur && !strcmp(cur->name(), "stats")) dur = cfgd("stats_seconds", 20);
        if (!cur || !strcmp(cur->name(), "visualizer") || t - scene_start > dur) advance(t);
      }
    }

    // night dimming
    int b = brightness;
    int ns = (int)cfgd("night_start", -1), ne = (int)cfgd("night_end", 7);
    if (ns >= 0) {
      time_t now = time(NULL);
      struct tm tm;
      localtime_r(&now, &tm);
      bool night = ns > ne ? (tm.tm_hour >= ns || tm.tm_hour < ne)
                           : (tm.tm_hour >= ns && tm.tm_hour < ne);
      if (night) b = std::min(b, (int)cfgd("night_brightness", 10));
    }

    if (!on) {
      canvas->Clear();
      canvas = matrix->SwapOnVSync(canvas);
      usleep(200 * 1000);
      continue;
    }

    cur->render(cur_frame, t, dt);
    float mix = (float)((t - fade_start) / FADE);
    bool fading = prev && mix < 1.0f;
    if (fading) prev->render(prev_frame, t, dt);
    else prev = NULL;

    canvas->SetBrightness(b);
    bool raw = cur->raw() && !fading;
    int m = fading ? (int)(mix * 256) : 256;
    for (int f = 0; f < 3; f++)
      for (int j = 0; j < N; j++)
        for (int i = 0; i < N; i++) {
          RGB c = cur_frame.px[f][j][i];
          if (fading) {
            RGB p = prev_frame.px[f][j][i];
            c.r = (c.r * m + p.r * (256 - m)) >> 8;
            c.g = (c.g * m + p.g * (256 - m)) >> 8;
            c.b = (c.b * m + p.b * (256 - m)) >> 8;
          }
          if (raw) canvas->SetPixel(f * N + i, j, c.r, c.g, c.b);
          else canvas->SetPixel(map_x[f][j][i], map_y[f][j][i], c.r, c.g, c.b);
        }
    canvas = matrix->SwapOnVSync(canvas);

    double spent = now_seconds() - t0 - t;
    double frame = 1.0 / cfgd("fps", 40);
    if (spent < frame) usleep((useconds_t)((frame - spent) * 1e6));
  }

  net.join();
  wx.join();
  matrix->Clear();
  delete matrix;
  return 0;
}
