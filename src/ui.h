#pragma once
#include "display.h"
#include "theme.h"
#include "sprites.h"
#include "protocol.h"
// Home + pairing UI (LovyanGFX). Layout (240x320):
//   top bar / two thin quota+time bars per side / centered dragon /
//   2x2 icon cards / bottom status line.
// Per side: gold = fraction of the window left before reset, green = remaining
// quota %, with a window icon below (clock = 5h left, calendar = 7d right).
// Fed by AppState.rem5/rem7/sec5/sec7 (pushed by claude_usage_ble.py). A side
// with no value yet — or a dropped link — is hidden (blends into the background).

extern LGFX lcd;

namespace ui {

static const int DRAGON_X = 58, DRAGON_Y = 26;
// Centre of the blank space the dragon coils around — where the idle pearl sits
// and the seven dragon balls orbit. Tune COIL_CY to nudge them up/down.
static const int COIL_CX = DRAGON_X + SPR_W / 2;   // 120
static const int COIL_CY = DRAGON_Y + 120;         // 146
// Quota columns: the bar fills from the top down, the quota % sits just below
// it, and the reset countdown (2 lines) goes at the very bottom of the column.
static const int BAR_Y = 24, BAR_H = 234, BAR_W = 6;
static const int PCT_Y = 262;    // quota % text, just below the bar
static const int RESET_Y = 274;  // reset countdown below the %; 2nd line at RESET_Y + 9

// Animated accents float directly over the dragon (see drawOverlay): each tick
// repaints the opaque sprite first, then paints the accent on top, so there is no
// per-tick bg wipe that could carve a hole in the dragon or the cards.

// ---- tiny primitive icons (drawn around center x,y) ----
inline void iconLightning(int x, int y, uint16_t c) {
  lcd.fillTriangle(x + 1, y - 8, x - 5, y + 2, x + 1, y + 1, c);
  lcd.fillTriangle(x - 1, y + 8, x + 5, y - 2, x - 1, y - 1, c);
}
inline void iconTarget(int x, int y, uint16_t c) {
  lcd.drawCircle(x, y, 7, c);
  lcd.drawCircle(x, y, 4, c);
  lcd.fillCircle(x, y, 1, c);
}
inline void iconCoin(int x, int y, uint16_t c) {
  lcd.fillCircle(x, y, 7, c);
  lcd.drawCircle(x, y, 7, COIN_EDGE);
  lcd.drawCircle(x, y, 3, COIN_EDGE);
}
inline void iconCoinStack(int x, int y, uint16_t c) {
  for (int i = 0; i < 3; i++) {  // bottom coin first, top drawn last
    int cy = y + 4 - i * 4;
    lcd.fillEllipse(x, cy, 8, 3, c);
    lcd.drawEllipse(x, cy, 8, 3, COIN_EDGE);
  }
}
inline void iconPlay(int x, int y, uint16_t c) { lcd.fillTriangle(x - 4, y - 6, x - 4, y + 6, x + 6, y, c); }
inline void iconPause(int x, int y, uint16_t c) {
  lcd.fillRect(x - 5, y - 6, 4, 12, c);
  lcd.fillRect(x + 1, y - 6, 4, 12, c);
}
inline void iconClock(int x, int y, uint16_t c) {
  lcd.drawCircle(x, y, 6, c);
  lcd.drawLine(x, y, x, y - 4, c);
  lcd.drawLine(x, y, x + 3, y + 1, c);
}
inline void iconCalendar(int x, int y, uint16_t c) {
  lcd.drawRoundRect(x - 6, y - 4, 12, 11, 2, c);
  lcd.drawLine(x - 6, y, x + 6, y, c);
  lcd.drawLine(x - 3, y - 4, x - 3, y - 7, c);
  lcd.drawLine(x + 3, y - 4, x + 3, y - 7, c);
}

// ---- tiny animated-accent primitives ----
inline void ovStar(int x, int y, int r, uint16_t c) {  // 4-point sparkle
  lcd.drawLine(x - r, y, x + r, y, c);
  lcd.drawLine(x, y - r, x, y + r, c);
}
inline void ovHeart(int x, int y, int s, uint16_t c) {
  lcd.fillCircle(x - s / 2, y, s / 2, c);
  lcd.fillCircle(x + s / 2, y, s / 2, c);
  lcd.fillTriangle(x - s, y + s / 3, x + s, y + s / 3, x, y + s + s / 2, c);
}
inline void ovPearl(int x, int y, int r, uint16_t c) {  // dragon pearl: orb + rim + shine
  lcd.fillCircle(x, y, r, c);
  lcd.drawCircle(x, y, r, COIN_EDGE);
  int hr = r / 3; if (hr < 1) hr = 1;
  lcd.fillCircle(x - r / 3, y - r / 3, hr, CARD);      // top-left highlight
}

// A filled 5-point star, drawn as a 10-vertex fan from its centre (the star is
// star-shaped about its centre, so the fan fills it exactly). Below ~2px there's
// no room for points, so it degrades to a dot.
inline void star5(int cx, int cy, int r, uint16_t c) {
  if (r < 2) { lcd.fillCircle(cx, cy, 1, c); return; }
  static const float sx[10] = {0, 0.2246f, 0.951f, 0.363f, 0.588f,
                               0, -0.588f, -0.363f, -0.951f, -0.2246f};
  static const float sy[10] = {-1, -0.309f, -0.309f, 0.118f, 0.809f,
                               0.382f, 0.809f, 0.118f, -0.309f, -0.309f};
  int px[10], py[10];
  for (int i = 0; i < 10; i++) { px[i] = cx + (int)(sx[i] * r); py[i] = cy + (int)(sy[i] * r); }
  for (int i = 0; i < 10; i++) {
    int j = (i + 1) % 10;
    lcd.fillTriangle(cx, cy, px[i], py[i], px[j], py[j], c);
  }
}

// The N red stars on a dragon ball, in the canonical 1..7 layouts. Offsets are
// percentages of the ball radius; stars shrink a little past 4 to stay clear.
inline void ballStars(int x, int y, int r, int n) {
  int sr = r * (n <= 4 ? 30 : 23) / 100; if (sr < 2) sr = 2;
  static const int8_t L[7][14] = {
    {  0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0},  // 1
    {-45,-45,  45, 45,   0,  0,   0,  0,   0,  0,   0,  0,   0,  0},  // 2
    {  0,-55, -52, 45,  52, 45,   0,  0,   0,  0,   0,  0,   0,  0},  // 3
    {-48,-48,  48,-48, -48, 48,  48, 48,   0,  0,   0,  0,   0,  0},  // 4
    {-52,-52,  52,-52,   0,  0, -52, 52,  52, 52,   0,  0,   0,  0},  // 5
    {-48,-58,  48,-58, -48,  0,  48,  0, -48, 58,  48, 58,   0,  0},  // 6
    {  0,  0,   0,-68,   0, 68, -60,-34,  60,-34, -60, 34,  60, 34},  // 7
  };
  const int8_t* row = L[n - 1];
  for (int i = 0; i < n; i++)
    star5(x + row[i * 2] * r / 100, y + row[i * 2 + 1] * r / 100, sr, RED);
}

// A dragon ball: glossy orange orb + rim + highlight, with 1..7 red stars.
inline void drawDragonBall(int x, int y, int r, int stars) {
  lcd.fillCircle(x, y, r, DRAGON_GOLD);
  lcd.drawCircle(x, y, r, COIN_EDGE);
  int hr = r / 3; if (hr < 1) hr = 1;
  lcd.fillCircle(x - r / 2, y - r / 2, hr, CARD);      // top-left shine
  ballStars(x, y, r, stars);
}

// Compact token count, max 5 chars. uint32_t tops out at ~4.2B so B covers it.
//   999 -> "999"   5.4k -> "5.4k"   423k -> "423k"   1.2M -> "1.2M"
//   123M -> "123M"  1.2B -> "1.2B"
inline void fmtK(char* b, size_t n, uint32_t v) {
  if (v >= 1000000000) snprintf(b, n, "%lu.%luB", (unsigned long)(v / 1000000000), (unsigned long)((v % 1000000000) / 100000000));
  else if (v >= 100000000) snprintf(b, n, "%luM", (unsigned long)(v / 1000000));                             // 100M..999M
  else if (v >= 1000000)   snprintf(b, n, "%lu.%luM", (unsigned long)(v / 1000000), (unsigned long)((v % 1000000) / 100000)); // 1.2M..99.9M
  else if (v >= 10000)     snprintf(b, n, "%luk", (unsigned long)(v / 1000));                                // 20k..999k
  else if (v >= 1000)      snprintf(b, n, "%lu.%luk", (unsigned long)(v / 1000), (unsigned long)((v % 1000) / 100)); // 5.4k
  else                     snprintf(b, n, "%lu", (unsigned long)v);
}

inline void timeStr(char* b, size_t n) {
  if (!g_app.timeValid) { strncpy(b, "--:--", n); return; }
  uint32_t sec = g_app.epoch + (millis() - g_app.timeSyncMs) / 1000;
  snprintf(b, n, "%02u:%02u", (unsigned)((sec / 3600) % 24), (unsigned)((sec / 60) % 60));
}

// Card footprint (smaller than before). Two columns of these fit at x=CARD_L/CARD_R.
static const int CARD_W = 82, CARD_H = 38;
static const int CARD_L = 26, CARD_R = 132;

// icon: 0 coin(today) 1 coin-stack(total) 2 play(active) 3 pause(wait)
// tsize: fractional text scale (LovyanGFX allows floats, so we can nudge by
// small steps instead of the coarse 1->2 doubling).
inline void card(int x, int y, int icon, const char* value, uint16_t accent, float tsize) {
  lcd.fillRoundRect(x, y, CARD_W, CARD_H, 7, CARD);
  lcd.drawRoundRect(x, y, CARD_W, CARD_H, 7, CARD_STROKE);
  int ix = x + 14, iy = y + 19;
  switch (icon) {
    case 0: iconCoin(ix, iy, accent); break;
    case 1: iconCoinStack(ix, iy, accent); break;
    case 2: iconPlay(ix, iy, accent); break;
    default: iconPause(ix, iy, accent); break;
  }
  lcd.setTextSize(tsize);
  int tx = x + 28, ty = y + (int)((CARD_H - 8 * tsize) / 2);
  // Faux-bold: opaque first pass, then a 1px horizontal overstrike with a
  // transparent background (single-arg setTextColor -> fore==back -> no bg
  // fill) so the glyphs thicken without erasing the first pass.
  lcd.setTextColor(INK, CARD);
  lcd.setCursor(tx, ty);
  lcd.print(value);
  lcd.setTextColor(INK);
  lcd.setCursor(tx + 1, ty);
  lcd.print(value);
}

// One merged bar per side: two touching segments (time=blue, quota=green)
// inside a single rounded track, so it reads as one bar not two. Colors are
// fixed by metric (not by level), same on both sides.
inline void mergedBar(int x, int timePct, int quotaPct) {
  lcd.fillRoundRect(x, BAR_Y, 2 * BAR_W, BAR_H, 3, TRACK);
  int t = timePct > 100 ? 100 : timePct;
  int q = quotaPct > 100 ? 100 : quotaPct;
  if (t >= 0) { int fh = BAR_H * t / 100; if (fh > 3) lcd.fillRect(x, BAR_Y + BAR_H - fh, BAR_W, fh, DRAGON_GOLD); }
  if (q >= 0) { int fh = BAR_H * q / 100; if (fh > 3) lcd.fillRect(x + BAR_W, BAR_Y + BAR_H - fh, BAR_W, fh, DRAGON_GREEN); }
}

// One side's column. rem >= 0: a green quota-remaining bar filling from the top,
// the quota "NN%" just below it, and the reset countdown as two lines
// ("{h}h" / "{m}m") at the bottom. rem < 0 (no value received / link down) ->
// the whole column is wiped to the background.
// Text is centred on the column centre (cx); sec = seconds until the window resets.
inline void quotaBar(bool left, int rem, uint32_t sec) {
  int x = left ? 6 : 222;
  int cx = x + BAR_W;
  if (rem < 0) {                                            // no value -> wipe column
    lcd.fillRect(cx - 13, BAR_Y - 2, 26, (RESET_Y + 18) - (BAR_Y - 2), BG);
    return;
  }

  // Quota-remaining bar, filled bottom-up. Painted as two complementary slices —
  // empty track on top, colour fill on the bottom — so every pixel is written
  // exactly once. No clear-to-background pass, so the column never flashes when a
  // usage push refreshes it. Green while healthy; the dragon's gold below 50%.
  int q = rem > 100 ? 100 : rem;
  uint16_t barColor = (q < 50) ? DRAGON_GOLD : DRAGON_GREEN;
  int fh = BAR_H * q / 100;                                 // filled height
  int eh = BAR_H - fh;                                      // empty (track) height
  if (eh > 0) lcd.fillRect(x, BAR_Y,      2 * BAR_W, eh, TRACK);
  if (fh > 0) lcd.fillRect(x, BAR_Y + eh, 2 * BAR_W, fh, barColor);

  // quota percentage, just below the bar. Only its own (variable-width) strip is
  // cleared, so a shrinking number leaves no residue without blanking the column.
  char pct[6];
  snprintf(pct, sizeof(pct), "%d%%", q);
  lcd.fillRect(cx - 13, PCT_Y - 1, 26, 10, BG);
  lcd.setTextSize(1);
  lcd.setTextColor(INK, BG);
  lcd.setCursor(cx - lcd.textWidth(pct) / 2, PCT_Y);       lcd.print(pct);

  // reset countdown, two centred lines below the %. >= 1 day -> "{d}d" / "{h}h"
  // (don't pile up into a huge hour count); under a day -> "{h}h" / "{m}m".
  char l1[6], l2[6];
  int days = (int)(sec / 86400);
  if (days > 0) {
    snprintf(l1, sizeof(l1), "%dd", days);
    snprintf(l2, sizeof(l2), "%dh", (int)((sec % 86400) / 3600));
  } else {
    snprintf(l1, sizeof(l1), "%dh", (int)(sec / 3600));
    snprintf(l2, sizeof(l2), "%dm", (int)((sec % 3600) / 60));
  }
  lcd.fillRect(cx - 13, RESET_Y - 1, 26, 19, BG);          // clear the two-line strip
  lcd.setTextSize(1);
  lcd.setTextColor(DIM, BG);
  lcd.setCursor(cx - lcd.textWidth(l1) / 2, RESET_Y);      lcd.print(l1);
  lcd.setCursor(cx - lcd.textWidth(l2) / 2, RESET_Y + 9);  lcd.print(l2);
}

// Both side columns from live state. rem5/rem7 stay -1 (hidden) until the usage
// bridge pushes over USB serial; protoPoll clears them again once stale, so we
// render them directly here. Left = 5h, right = 7d.
inline void drawQuotaBars() {
  quotaBar(true,  g_app.rem5, g_app.sec5);
  quotaBar(false, g_app.rem7, g_app.sec7);
}

inline void status(const char* s, uint16_t dot) {
  lcd.fillRect(0, 300, 240, 20, BG);
  lcd.fillSmoothCircle(9, 307, 3, dot);
  lcd.setTextColor(INK, BG);
  lcd.setTextSize(1);
  lcd.setCursor(16, 304);
  lcd.print(s);
}

// Repaint just the base dragon sprite in place. The sprite is opaque and covers
// its whole rectangle, so pushing a new one needs no bg clear -> no flash. Used
// by the full redraw, the in-place sprite swap (idle<->busy), and every overlay
// tick (so large accents can float over the dragon without a bg wipe).
inline void drawDragon(int st) {
  lcd.setSwapBytes(true);
  lcd.pushImage(DRAGON_X, DRAGON_Y, SPR_W, SPR_H, spriteFor(st));
}

// Text with a 1px background-coloured halo so it stays legible over the dragon
// body (light theme: the halo carves a thin near-white moat around each glyph;
// over the plain background the halo is invisible). Single-arg setTextColor ->
// transparent bg, so the four outline passes don't erase one another.
inline void outlinedText(const char* s, int x, int y, int size, uint16_t fg) {
  lcd.setTextSize(size);
  const int o = size >= 3 ? 2 : 1;
  lcd.setTextColor(BG);
  lcd.setCursor(x - o, y); lcd.print(s);
  lcd.setCursor(x + o, y); lcd.print(s);
  lcd.setCursor(x, y - o); lcd.print(s);
  lcd.setCursor(x, y + o); lcd.print(s);
  lcd.setTextColor(fg);
  lcd.setCursor(x, y);     lcd.print(s);
}

// Per-state animated accent. The base dragon is repainted first each tick (opaque
// -> no flash), so accents can be large and float directly over the dragon. Every
// accent stays within the dragon's bounds ([DRAGON_X..+SPR_W] x [DRAGON_Y..+SPR_H])
// so the sprite redraw fully clears the previous frame — no per-zone bg wipe.
//   offline: big rising "z z z"  idle: one big pearl  busy: three orbiting pearls
//   attention: pulsing big "?"..."???"   `frame` is a free-running counter.
inline void drawOverlay(int st, uint16_t frame) {
  drawDragon(st);
  const int cx = DRAGON_X + SPR_W / 2;   // 120, dragon centre x
  switch (st) {
    case 0: {  // offline: three big z's rising up-right over the upper body
      static const int zsz[3] = {2, 3, 4};
      int ph = frame & 3;
      int bx = cx - 8, by = DRAGON_Y + 78;
      for (int i = 0; i < 3; i++)
        outlinedText("z", bx + i * 15, by - i * 24 - ph * 3, zsz[i], DIM);
    } break;
    case 1: {  // idle: one big dragon pearl bobbing in the coil's blank centre
      static const int bob[4] = {0, -3, -5, -3};
      ovPearl(COIL_CX, COIL_CY + bob[frame & 3], 15, DRAGON_GOLD);
    } break;
    case 2: {  // busy: the seven dragon balls (1..7 stars) orbiting the blank space
      const float R = 33.0f;
      const float base = frame * 0.15f;               // slow orbit
      for (int i = 0; i < 7; i++) {
        float a = base + i * (6.2831853f / 7.0f);
        int px = COIL_CX + (int)(R * cosf(a));
        int py = COIL_CY + (int)(R * sinf(a));
        drawDragonBall(px, py, 10, i + 1);            // i+1 stars
      }
    } break;
    default: {  // attention: pulsing "?" -> "??" -> "???", large + outlined
      static const char* q[3] = {"?", "??", "???"};
      const char* s = q[frame % 3];
      lcd.setTextSize(3);
      outlinedText(s, cx - lcd.textWidth(s) / 2, DRAGON_Y + 44, 3, GOLD);
    } break;
  }
}

// Pairing rows drawn in place of the 2x2 cards (same y region ~200..296):
// row 1 = device name, row 2 = the 6-digit passkey. The rest of the home
// screen (top bar, quota bars, dragon) stays put so pairing lives in place.
inline void pairCards(const char* name, uint32_t pk) {
  // row 1: device name
  lcd.setTextColor(INK, BG);
  lcd.setTextSize(1);
  lcd.setCursor(120 - lcd.textWidth(name) / 2, 216);
  lcd.print(name);

  // row 2: pairing code (gold once the bonding handshake supplies one, dim
  // placeholder while merely advertising)
  char b[8];
  lcd.setTextSize(2);
  if (pk) { snprintf(b, sizeof(b), "%06lu", (unsigned long)pk); lcd.setTextColor(GOLD, BG); }
  else    { strncpy(b, "------", sizeof(b));                     lcd.setTextColor(DIM, BG); }
  lcd.setCursor(120 - lcd.textWidth(b) / 2, 242);
  lcd.print(b);
}

// Disconnected -> pairing view: name + passkey replace the info cards, in place.
// (pk is the live bonding passkey, 0 while just advertising.)
// full=true does the whole screen (bg + bars + dragon); full=false repaints only
// the volatile bits (clock + cards + status) so token/minute updates don't flash.
inline void drawHome(const char* name, uint32_t pk, bool full) {
  bool conn = g_app.connected;
  int st = deriveSprite();

  if (full) {
    lcd.fillScreen(BG);
    // top bar dot; quota bars (real values / gray when no data); dragon
    lcd.fillSmoothCircle(14, 14, 4, conn ? GREEN : DIM);
    drawQuotaBars();
    drawDragon(st);
  }

  // clock, top-right (device name lives in the pairing view, not here)
  char tb[8];
  timeStr(tb, sizeof(tb));
  lcd.fillRect(188, 8, 50, 12, BG);
  lcd.setTextColor(DIM, BG);
  lcd.setTextSize(1);
  lcd.setCursor(234 - lcd.textWidth(tb), 10);
  lcd.print(tb);

  if (!conn) {
    pairCards(name, pk);
    status("Open Hardware Buddy to pair", GOLD);
    return;
  }

  // account owner name, top-left beside the status dot (arrives via {"cmd":"owner"};
  // repainted on the volatile path so it shows once it lands just after connect).
  lcd.fillRect(22, 8, 158, 12, BG);
  if (g_app.owner[0]) {
    lcd.setTextColor(INK, BG);
    lcd.setTextSize(1);
    lcd.setCursor(24, 10);
    lcd.print(g_app.owner);
  }

  // 2x2 icon cards. left = today (single coin), right = cumulative (coin stack).
  char v[12];
  fmtK(v, sizeof(v), g_app.tokensToday);
  card(CARD_L, 210, 0, v, GOLD, 1.6f);
  fmtK(v, sizeof(v), g_app.tokens);
  card(CARD_R, 210, 1, v, GOLD, 1.6f);
  snprintf(v, sizeof(v), "%u", g_app.running);
  card(CARD_L, 254, 2, v, GREEN, 1.6f);
  snprintf(v, sizeof(v), "%u", g_app.waiting);
  card(CARD_R, 254, 3, v, DIM, 1.6f);

  status(g_app.msg[0] ? g_app.msg : "connected", GREEN);
}

}  // namespace ui
