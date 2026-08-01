// clawd-on-esp — main / composition root
//
// Boot flow:
//   load config -> connect to a known Wi-Fi by priority
//     connected  -> STA: start dashboard + WebSocket client, animate the mascot
//     no network -> AP:  captive setup portal + on-screen QR to join & configure
//
// First-time setup is done entirely from the phone (no secrets.h needed).
// include/secrets.h, if present with real values, only SEEDS the first boot.

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
#include "Config.h"
#include "ConfigStore.h"
#include "NetLink.h"
#include "AnimationManager.h"
#include "WiFiConnection.h"
#include "WebPortal.h"
#include "InfoScreen.h"

// ── Globals ─────────────────────────────────────────────────────────────────
TFT_eSPI         tft = TFT_eSPI();
AnimationManager anim;
NetLink          net;
SessionStore     store;
WiFiConnection   wifi;
WebPortal        portal;
ClawdConfigData  cfg;

enum class App : uint8_t { Provisioning, Running };
App      app = App::Provisioning;

enum class Link : uint8_t { Connecting, Connected, AuthFailed };
Link     linkState = Link::Connecting;

bool     statusDirty = true;
uint32_t lastStatus = 0;
bool     showInfo = true;      // show the connected/QR screen briefly on connect
bool     infoDrawn = false;
bool     gotState = false;
uint32_t runningStart = 0;
uint32_t lastConnCheck = 0;

static const int STATUS_Y = 238;
static const int BL_CHANNEL = 0;   // LEDC channel for backlight PWM

static void applyBrightness(uint8_t pct) {
  if (pct < 10) pct = 10;
  if (pct > 100) pct = 100;
  ledcWrite(BL_CHANNEL, map(pct, 0, 100, 0, 255));
}

// ── Status bar (STA / running) ──────────────────────────────────────────────
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
  tft.drawString("s:" + String(store.size()) + "  " + WiFi.localIP().toString(), 6, STATUS_Y + 26, 1);
}

// ── clawd-on-desk WebSocket link ────────────────────────────────────────────
static void connectActiveHost() {
  if (cfg.hosts.empty()) { Serial.println("[net] no host configured"); return; }
  if (cfg.activeHost < 0 || cfg.activeHost >= (int)cfg.hosts.size()) cfg.activeHost = 0;
  ClawdHost &h = cfg.hosts[cfg.activeHost];
  Serial.printf("[net] using host %s:%u\n", h.host.c_str(), h.port);
  net.begin(h.host, h.port, cfg.token);
}

static void wireNet() {
  net.onSnapshotBegin = []() { store.clear(); statusDirty = true; };
  net.onSession = [](const String &sid, ClawdState st) { store.upsert(sid, st); gotState = true; statusDirty = true; };
  net.onDeleted = [](const String &sid) { store.remove(sid); statusDirty = true; };
  net.onLink = [](bool up) {
    if (up) linkState = Link::Connected;
    else if (linkState != Link::AuthFailed) linkState = Link::Connecting;
    statusDirty = true;
  };
  net.onTokenRotate = [](const String &nt) { ConfigStore::saveToken(nt); cfg.token = nt; };
  net.onAuthSuspected = []() { linkState = Link::AuthFailed; statusDirty = true; };
}

// ── Optional seed from secrets.h (dev convenience only) ─────────────────────
static void seedFromSecrets() {
#if __has_include("secrets.h")
  if (String(WIFI_SSID).length() && String(WIFI_SSID) != "your-wifi-ssid") {
    WiFiNet n; n.ssid = WIFI_SSID; n.pass = WIFI_PASSWORD; n.priority = 10;
    cfg.networks.push_back(n);
    ClawdHost h; h.host = CLAWD_HOST; h.port = CLAWD_PORT; cfg.hosts.push_back(h);
    cfg.token = CLAWD_TOKEN;
    cfg.provisioned = true;
    ConfigStore::save(cfg);
    Serial.println("[cfg] seeded from secrets.h");
  }
#endif
}

// ── State transitions ───────────────────────────────────────────────────────
static void enterProvisioning() {
  app = App::Provisioning;
  portal.beginSetup();
  Serial.println("[app] provisioning (AP + captive portal)");
}

static void enterRunning() {
  app = App::Running;
  cfg = ConfigStore::load();
  anim.setScale(cfg.mascotScale);
  applyBrightness(cfg.brightness);
  portal.onConfigChanged = []() {          // apply dashboard changes live
    cfg = ConfigStore::load();
    anim.setScale(cfg.mascotScale);
    applyBrightness(cfg.brightness);
    net.useToken(cfg.token);               // no-op unless the token changed
  };
  portal.beginDashboard(cfg);
  wireNet();
  connectActiveHost();
  runningStart = millis();
  showInfo = true; infoDrawn = false; gotState = false;
  Serial.println("[app] running (STA + dashboard)");
}

// ── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n\n=== clawd-on-esp ===");

#ifdef TFT_BL
  ledcSetup(BL_CHANNEL, 5000, 8);          // PWM backlight for brightness control
  ledcAttachPin(TFT_BL, BL_CHANNEL);
  applyBrightness(100);
#endif
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  anim.begin(&tft);

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_ORANGE, TFT_BLACK);
  tft.drawString("Clawd", tft.width() / 2, tft.height() / 2 - 12, 4);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("iniciando...", tft.width() / 2, tft.height() / 2 + 16, 2);

  cfg = ConfigStore::load();
  if (!cfg.hasNetworks()) seedFromSecrets();
  anim.setScale(cfg.mascotScale);
  applyBrightness(cfg.brightness);

  wifi.onAP = enterProvisioning;
  wifi.onStation = enterRunning;
  wifi.begin(cfg);   // fires one of the callbacks above
}

// ── Provisioning display: alternate mascot (waiting) and the QR/join screen ──
static void provisioningTick() {
  static uint32_t t0 = 0;
  static bool qrPhase = true;
  static bool drawn = false;
  if (millis() - t0 > 6000) { t0 = millis(); qrPhase = !qrPhase; drawn = false; }

  if (qrPhase) {
    if (!drawn) {
      String join = "WIFI:T:nopass;S:" + wifi.apSsid() + ";;";
      InfoScreen::show(tft, "Configurar", join.c_str(),
                       "Rede: " + wifi.apSsid(), "http://" + wifi.ip(), "Escaneie para conectar");
      drawn = true;
    }
  } else {
    if (!drawn) { tft.fillScreen(TFT_BLACK); drawn = true; }
    anim.setState(ClawdState::Notification);
    anim.loop();
  }
}

// ── Loop ────────────────────────────────────────────────────────────────────
void loop() {
  wifi.loop();
  portal.loop();
  if (portal.rebootRequested()) { delay(400); ESP.restart(); }

  if (app == App::Provisioning) { provisioningTick(); return; }

  // Running (STA)
  net.loop();

  // basic host failover if the current endpoint stays unreachable
  if (!net.isConnected() && cfg.hosts.size() > 1) {
    if (millis() - lastConnCheck > 20000) {
      lastConnCheck = millis();
      cfg.activeHost = (cfg.activeHost + 1) % cfg.hosts.size();
      Serial.printf("[net] failover -> host %d\n", cfg.activeHost);
      connectActiveHost();
    }
  } else if (net.isConnected()) {
    lastConnCheck = millis();
  }

  if (showInfo) {
    if (!infoDrawn) {
      String url = "http://" + wifi.ip();
      InfoScreen::show(tft, "Conectado", (url + "/").c_str(), url, "Painel de controle");
      infoDrawn = true;
    }
    if (gotState || millis() - runningStart > 8000) {
      showInfo = false;
      tft.fillScreen(TFT_BLACK);
      statusDirty = true;
    }
    return;
  }

  anim.setState(store.dominant());
  anim.loop();
  uint32_t now = millis();
  if (statusDirty || now - lastStatus >= 3000) {
    statusDirty = false;
    lastStatus = now;
    drawStatus();
  }
}
