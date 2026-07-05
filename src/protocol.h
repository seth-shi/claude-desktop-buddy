#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include "ble_bridge.h"
// Buddy wire protocol (see ../REFERENCE.md), de-M5-ified from the original
// src/data.h. Parses newline-delimited JSON from the BLE NUS RX ring:
// heartbeat snapshot, time sync, owner name, and cmd/ack handshakes.

struct AppState {
  uint8_t  total = 0, running = 0, waiting = 0;
  bool     completed = false;
  uint32_t tokens = 0, tokensToday = 0;
  // Subscription usage windows pushed by the desktop bridge (claude_usage_ble.py,
  // a protocol extension — not in the official heartbeat). rem* = remaining quota
  // % (0..100), -1 until a value arrives; sec* = seconds until that window resets.
  // These drive the two side bars; a rem* < 0 renders that bar gray.
  int      rem5 = -1, rem7 = -1;
  uint32_t sec5 = 0,  sec7 = 0;
  char     msg[24] = "";
  char     owner[24] = "";
  bool     connected = false;
  char     promptId[40] = "";   // pending permission id; empty = none
  char     promptTool[20] = "";
  char     promptHint[44] = "";
  // time sync
  uint32_t epoch = 0;      // seconds since 1970 at last sync, tz-adjusted
  int32_t  tzoff = 0;
  uint32_t timeSyncMs = 0; // millis() at sync
  bool     timeValid = false;
  uint32_t lastLiveMs = 0; // millis() of last heartbeat (BLE, desktop app)
  uint32_t quotaMs = 0;    // millis() of last quota update (USB serial bridge)
};

static AppState g_app;

inline bool appConnected() {
  return g_app.lastLiveMs != 0 && (millis() - g_app.lastLiveMs) <= 30000;
}

// Quota stays valid for a while after each push (the bridge sends ~every 5 min);
// outside that window the side bars revert to "unknown" (hidden).
inline bool quotaFresh() {
  return g_app.quotaMs != 0 && (millis() - g_app.quotaMs) <= 20UL * 60UL * 1000UL;
}

// Send one JSON line to the desktop (NUS TX), newline-terminated.
inline void protoSend(const char* json) {
  Serial.println(json);
  bleWrite((const uint8_t*)json, strlen(json));
  bleWrite((const uint8_t*)"\n", 1);
}

// Reply to {"cmd":"status"} so the desktop's stats panel populates.
inline void sendStatusAck() {
  JsonDocument d;
  d["ack"] = "status";
  d["ok"] = true;
  JsonObject data = d["data"].to<JsonObject>();
  if (g_app.owner[0]) data["name"] = g_app.owner;
  data["sec"] = bleSecure();
  data["rem5"] = g_app.rem5;             // quota state echoed back for host-side checks
  data["rem7"] = g_app.rem7;
  data["qf"]   = (int)quotaFresh();
  JsonObject sys = data["sys"].to<JsonObject>();
  sys["up"] = (uint32_t)(millis() / 1000);
  sys["heap"] = ESP.getFreeHeap();
  char buf[192];
  serializeJson(d, buf, sizeof(buf));
  protoSend(buf);
}

inline void sendAck(const char* cmd, bool ok) {
  JsonDocument d;
  d["ack"] = cmd;
  d["ok"] = ok;
  char buf[64];
  serializeJson(d, buf, sizeof(buf));
  protoSend(buf);
}

// Reply to a pending permission prompt.
inline void sendPermission(const char* decision) {
  if (!g_app.promptId[0]) return;
  JsonDocument d;
  d["cmd"] = "permission";
  d["id"] = g_app.promptId;
  d["decision"] = decision;  // "once" | "deny"
  char buf[128];
  serializeJson(d, buf, sizeof(buf));
  protoSend(buf);
}

static void _applyJson(const char* line) {
  JsonDocument doc;
  if (deserializeJson(doc, line)) return;

  // Commands that need an ack.
  const char* cmd = doc["cmd"];
  if (cmd) {
    if (!strcmp(cmd, "status")) {
      sendStatusAck();
    } else if (!strcmp(cmd, "owner") || !strcmp(cmd, "name")) {
      const char* nm = doc["name"];
      if (nm) { strncpy(g_app.owner, nm, sizeof(g_app.owner) - 1); g_app.owner[sizeof(g_app.owner) - 1] = 0; }
      sendAck(cmd, true);
    } else if (!strcmp(cmd, "unpair")) {
      bleClearBonds();
      sendAck("unpair", true);
    }
    g_app.lastLiveMs = millis();
    return;
  }

  // Time sync: {"time":[epoch_sec, tz_offset_sec]}
  JsonArray t = doc["time"];
  if (!t.isNull() && t.size() == 2) {
    g_app.epoch = (uint32_t)t[0].as<uint32_t>() + (int32_t)t[1];
    g_app.tzoff = (int32_t)t[1];
    g_app.timeSyncMs = millis();
    g_app.timeValid = true;
    g_app.lastLiveMs = millis();
    return;
  }

  // Two independent, separately-timestamped payloads share this line format:
  //   - heartbeat  (total/running/tokens/msg/prompt...) — from the desktop app over BLE
  //   - quota      (rem5/rem7/sec5/sec7)                — from the local bridge over USB serial
  // A quota-only line must NOT look like a heartbeat (else the dragon would wake
  // and the cards would show zeros), so each block updates only its own state.
  bool hasQuota = !doc["rem5"].isNull() || !doc["rem7"].isNull()
               || !doc["sec5"].isNull() || !doc["sec7"].isNull();
  bool hasBeat  = !doc["total"].isNull() || !doc["running"].isNull()
               || !doc["tokens"].isNull() || !doc["tokens_today"].isNull()
               || !doc["msg"].isNull() || !doc["prompt"].isNull();

  if (hasQuota) {
    g_app.rem5 = doc["rem5"] | g_app.rem5;
    g_app.rem7 = doc["rem7"] | g_app.rem7;
    g_app.sec5 = doc["sec5"] | g_app.sec5;
    g_app.sec7 = doc["sec7"] | g_app.sec7;
    g_app.quotaMs = millis();
  }

  if (hasBeat) {
    g_app.total   = doc["total"]   | g_app.total;
    g_app.running = doc["running"] | g_app.running;
    g_app.waiting = doc["waiting"] | g_app.waiting;
    g_app.completed = doc["completed"] | false;
    g_app.tokens      = doc["tokens"]       | g_app.tokens;
    g_app.tokensToday = doc["tokens_today"] | g_app.tokensToday;
    const char* m = doc["msg"];
    if (m) { strncpy(g_app.msg, m, sizeof(g_app.msg) - 1); g_app.msg[sizeof(g_app.msg) - 1] = 0; }

    JsonObject pr = doc["prompt"];
    if (!pr.isNull()) {
      const char* pid = pr["id"]; const char* pt = pr["tool"]; const char* ph = pr["hint"];
      strncpy(g_app.promptId,   pid ? pid : "", sizeof(g_app.promptId) - 1);   g_app.promptId[sizeof(g_app.promptId) - 1] = 0;
      strncpy(g_app.promptTool, pt  ? pt  : "", sizeof(g_app.promptTool) - 1); g_app.promptTool[sizeof(g_app.promptTool) - 1] = 0;
      strncpy(g_app.promptHint, ph  ? ph  : "", sizeof(g_app.promptHint) - 1); g_app.promptHint[sizeof(g_app.promptHint) - 1] = 0;
    } else {
      g_app.promptId[0] = g_app.promptTool[0] = g_app.promptHint[0] = 0;
    }
    g_app.lastLiveMs = millis();
  }
}

// Drain both input channels and dispatch complete '\n'-terminated JSON lines:
//   - BLE NUS RX  -> heartbeat from the desktop app
//   - USB serial  -> quota from the local usage bridge (independent of BLE)
inline void protoPoll() {
  static char buf[1024];
  static uint16_t len = 0;
  while (bleAvailable()) {
    int c = bleRead();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (len > 0) { buf[len] = 0; if (buf[0] == '{') _applyJson(buf); len = 0; }
    } else if (len < sizeof(buf) - 1) {
      buf[len++] = (char)c;
    }
  }
  // USB serial line buffer (kept separate so BLE and serial lines can't interleave).
  static char sbuf[256];
  static uint16_t slen = 0;
  while (Serial.available()) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\n' || c == '\r') {
      if (slen > 0) { sbuf[slen] = 0; if (sbuf[0] == '{') _applyJson(sbuf); slen = 0; }
    } else if (slen < sizeof(sbuf) - 1) {
      sbuf[slen++] = (char)c;
    }
  }

  g_app.connected = appConnected();
  if (!g_app.connected) {
    g_app.total = g_app.running = g_app.waiting = 0;
    g_app.completed = false;
  }
  // Quota bars follow their own freshness window (the bridge pushes ~every 5 min),
  // independent of the BLE link; stale -> revert to "unknown" (hidden).
  if (!quotaFresh()) {
    g_app.rem5 = g_app.rem7 = -1;
    g_app.sec5 = g_app.sec7 = 0;
  }
}

// Live state -> dragon state (0..3). States 0 and 1 share the sleep sprite and
// differ only in the animated accent (see ui::drawOverlay):
//   0 offline   : no data for 30s             -> sleep     + zzz
//   1 idle      : connected, nothing running  -> sleep     + one floating pearl
//   2 busy      : running > 0                 -> busy      + three orbiting pearls
//   3 attention : permission prompt / waiting -> attention + "???"
inline int deriveSprite() {
  if (!g_app.connected)                       return 0; // offline
  if (g_app.promptId[0] || g_app.waiting > 0) return 3; // attention
  if (g_app.running > 0)                      return 2; // busy
  return 1;                                             // idle
}
