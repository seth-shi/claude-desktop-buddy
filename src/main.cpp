// zthd-metrics C++ port (ES3N28P). Display (LovyanGFX) + BLE (Bluedroid NUS,
// SC bonding) + buddy heartbeat protocol. UI ported from the Rust firmware.
#include <Arduino.h>
#include <esp_mac.h>
#include "display.h"
#include "theme.h"
#include "sprites.h"
#include "ble_bridge.h"
#include "protocol.h"
#include "ui.h"

LGFX lcd;

static char btName[24] = "Claude-Buddy";

static void startBt() {
  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(btName, sizeof(btName), "Claude-Buddy-%02X%02X", mac[4], mac[5]);
  bleInit(btName);
}

#define DIAG 0  // set 1 to show the color/orientation bring-up pattern

#if DIAG
static uint16_t g_testGreen[40 * 40];
static void drawDiag() {
  lcd.fillScreen(lcd.color565(255, 255, 255));
  lcd.fillRect(0, 0, 80, 36, lcd.color565(255, 0, 0));
  lcd.fillRect(80, 0, 80, 36, lcd.color565(0, 255, 0));
  lcd.fillRect(160, 0, 80, 36, lcd.color565(0, 0, 255));
  for (int i = 0; i < 40 * 40; i++) g_testGreen[i] = 0x07E0;
  lcd.setSwapBytes(false);
  lcd.pushImage(20, 52, 40, 40, g_testGreen);
  lcd.setSwapBytes(true);
  lcd.pushImage(90, 52, 40, 40, g_testGreen);
  lcd.setSwapBytes(true);
  lcd.pushImage(58, 116, SPR_W, SPR_H, spriteFor(1));
}
#endif

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("zthd-metrics C++ boot");
  lcd.init();
  lcd.setColorDepth(16);
  lcd.setRotation(2);
  lcd.setBrightness(255);
  lcd.fillScreen(BG);
  startBt();
  Serial.printf("advertising as %s\n", btName);
#if DIAG
  drawDiag();
#endif
}

void loop() {
#if DIAG
  delay(1000);
  return;
#endif
  protoPoll();

  // Passkey (Bluedroid emits a fresh random one during bonding). Non-zero ->
  // pairing mode, shown in place on the home screen (name + code swap the cards).
  uint32_t pk = blePasskey();

  // Two-tier redraw to avoid flashing: a full-screen repaint only when the
  // layout changes (connect/disconnect, sprite state, pairing code), and a
  // cheap in-place repaint of just the clock+cards+status for value updates.
  bool conn = g_app.connected;
  int st = deriveSprite();
  uint32_t minute = g_app.timeValid
      ? ((g_app.epoch + (millis() - g_app.timeSyncMs) / 1000) / 60) : 0;
  uint32_t msgh = 0;
  for (const char* p = g_app.msg; *p; p++) msgh = msgh * 31u + (uint8_t)*p;
  uint32_t ownh = 0;  // owner name arrives just after connect -> fold into dsig
  for (const char* p = g_app.owner; *p; p++) ownh = ownh * 31u + (uint8_t)*p;

  uint32_t lsig = (conn ? 1u : 0u) + ((uint32_t)st << 1) + pk * 2246822519u;
  uint32_t dsig = g_app.tokens * 2654435761u + g_app.tokensToday
                + ((uint32_t)g_app.running << 4) + ((uint32_t)g_app.waiting << 8)
                + minute * 40503u + msgh + ownh * 668265263u;
  // Quota bars get their own signature (rem/sec) so a usage push over serial
  // repaints just the two bars in place — no full-screen flash.
  uint32_t qsig = (uint32_t)(g_app.rem5 + 1) * 2654435761u
                + (uint32_t)(g_app.rem7 + 1) * 2246822519u
                + (g_app.sec5 / 60) * 40503u + (g_app.sec7 / 60) * 668265263u;

  static uint32_t lastL = 0xFFFFFFFF, lastD = 0xFFFFFFFF, lastQ = 0xFFFFFFFF;
  static uint32_t lastAnim = 0;
  static uint16_t frame = 0;
  if (lsig != lastL) {
    ui::drawHome(btName, pk, true);   // full redraw already paints the bars
    ui::drawOverlay(st, frame);
    lastL = lsig; lastD = dsig; lastQ = qsig;
  } else if (dsig != lastD) {
    ui::drawHome(btName, pk, false);
    lastD = dsig;
  }
  if (qsig != lastQ) { ui::drawQuotaBars(); lastQ = qsig; }
  // Gentle accent animation: advance ~3 fps, redraw only the overlay zone.
  if (millis() - lastAnim >= 330) { lastAnim = millis(); frame++; ui::drawOverlay(st, frame); }
  delay(60);
}
