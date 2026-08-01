// clawd-on-esp — Phase 3: live Clawd mascot on the TFT
//
// Connects to the desktop LAN bridge (Mobile Protocol v1), tracks session
// states, computes the dominant state and plays the matching GIF animation from
// LittleFS. A slim status bar below shows link + state + diagnostics.
//
// Credentials/host/port/token live in include/secrets.h (gitignored).

#include <Arduino.h>
#include <WiFi.h>
#include <TFT_eSPI.h>

#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #include "secrets.example.h"
#endif

#include "ClawdState.h"
#include "SessionStore.h"
#include "ConfigStore.h"
#include "NetLink.h"
#include "AnimationManager.h"

// ── Globals ─────────────────────────────────────────────────────────────────
TFT_eSPI         tft = TFT_eSPI();
NetLink          net;
SessionStore     store;
AnimationManager anim;

enum class Link : uint8_t { Connecting, Connected, AuthFailed };
Link     linkState  = Link::Connecting;
bool     usingSaved = false;
bool     statusDirty = true;
uint32_t lastStatus = 0;

static const int STATUS_Y = 238;   // animation occupies 0..237, status bar below

// ── Status bar ──────────────────────────────────────────────────────────────
static void drawStatus() {
  tft.fillRect(0, STATUS_Y, tft.width(), tft.height() - STATUS_Y, TFT_BLACK);

  uint16_t dot; const char *txt;
  switch (linkState) {
    case Link::Connected:  dot = TFT_GREEN;  txt = "CONNECTED";  break;
    case Link::AuthFailed: dot = TFT_RED;    txt = "AUTH FAIL";  break;
    default:               dot = TFT_ORANGE; txt = "CONNECTING"; break;
  }
  tft.fillCircle(9, STATUS_Y + 11, 4, dot);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(dot, TFT_BLACK);
  tft.drawString(txt, 20, STATUS_Y + 4, 2);

  ClawdState d = store.dominant();
  tft.setTextDatum(TR_DATUM);
  tft.setTextColor(clawdColor(d), TFT_BLACK);
  tft.drawString(clawdName(d), tft.width() - 6, STATUS_Y + 4, 2);

  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  String s2 = "s:" + String(store.size()) + "  " + String(ESP.getFreeHeap() / 1024) +
              "K  " + WiFi.localIP().toString();
  if (!anim.assetsOk()) s2 = "NO ASSETS (uploadfs)  " + s2;
  tft.drawString(s2, 6, STATUS_Y + 26, 1);
}

// ── Wi-Fi ───────────────────────────────────────────────────────────────────
static bool wifiConnect(uint32_t timeoutMs) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);           // disable modem-sleep — fixes flaky/slow TCP
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

// ── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n\n=== clawd-on-esp — Phase 3: mascot on screen ===");

#ifdef TFT_BL
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
#endif
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("WiFi: " WIFI_SSID, 6, 6, 2);

  if (!wifiConnect(20000)) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("WiFi FAILED (secrets.h)", 6, 30, 2);
    Serial.println("[wifi] failed");
    return;
  }
  Serial.printf("[wifi] %s  RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());

  anim.begin(&tft);   // mount LittleFS + init GIF decoder

  // Prefer a persisted (rotated) token over the compiled one.
  String saved = ConfigStore::loadToken();
  usingSaved = saved.length() >= 32;
  String token = usingSaved ? saved : String(CLAWD_TOKEN);
  Serial.printf("[cfg] token source: %s\n", usingSaved ? "NVS (rotated)" : "secrets.h");

  net.onSnapshotBegin = []() { store.clear(); statusDirty = true; };
  net.onSession = [](const String &sid, ClawdState st) { store.upsert(sid, st); statusDirty = true; };
  net.onDeleted = [](const String &sid) { store.remove(sid); statusDirty = true; };
  net.onLink = [](bool up) {
    if (up) linkState = Link::Connected;
    else if (linkState != Link::AuthFailed) linkState = Link::Connecting;
    statusDirty = true;
  };
  net.onTokenRotate = [](const String &nt) {
    ConfigStore::saveToken(nt);
    usingSaved = true;
    Serial.println("[cfg] rotated token saved to NVS");
  };
  net.onAuthSuspected = []() {
    if (usingSaved) {
      ConfigStore::clearToken();
      usingSaved = false;
      net.useToken(CLAWD_TOKEN);
      Serial.println("[cfg] cleared stale NVS token; retrying secrets.h token");
    } else {
      linkState = Link::AuthFailed;
      statusDirty = true;
    }
  };

  net.begin(CLAWD_HOST, CLAWD_PORT, token);

  tft.fillScreen(TFT_BLACK);
  anim.setState(store.dominant());   // start on idle
  drawStatus();
}

// ── Loop ────────────────────────────────────────────────────────────────────
void loop() {
  if (WiFi.status() != WL_CONNECTED) { delay(200); return; }

  net.loop();
  anim.setState(store.dominant());   // switches the GIF when the state changes
  anim.loop();                       // advance a frame if due

  uint32_t now = millis();
  if (statusDirty || now - lastStatus >= 3000) {
    statusDirty = false;
    lastStatus = now;
    drawStatus();
    Serial.printf("[hb] up %lus  heap %uK  link %s  sessions %u  state %s\n",
                  now / 1000, ESP.getFreeHeap() / 1024,
                  linkState == Link::Connected ? "up" : (linkState == Link::AuthFailed ? "auth" : "down"),
                  (unsigned)store.size(), clawdName(store.dominant()));
  }
}
