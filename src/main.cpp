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
#include "Console.h"

#ifdef CLAWD_ENABLE_OTA
  #include <SPI.h>
  #include <SD.h>
  static SPIClass sdSPI(VSPI);
#else
  #include <LittleFS.h>
#endif

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

static const char *FW_VERSION = "0.6.0";
volatile uint32_t g_loopHz = 0;      // main-loop iterations/sec (spare-capacity proxy)

static String jsonEsc(const String &s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += ' ';
    else if ((uint8_t)c >= 0x20) o += c;
  }
  return o;
}

static const int STATUS_Y = 238;
static const int BL_CHANNEL = 0;   // LEDC channel for backlight PWM

static void applyBrightness(uint8_t pct) {
  if (pct < 10) pct = 10;
  if (pct > 100) pct = 100;
  ledcWrite(BL_CHANNEL, map(pct, 0, 100, 0, 255));
}

// Mounts the asset filesystem: SD card in OTA builds (flash is full of app
// slots), LittleFS otherwise. Returns null if nothing is mounted.
static fs::FS *mountAssetFs() {
#ifdef CLAWD_ENABLE_OTA
  sdSPI.begin(18, 19, 23, 5);            // CYD microSD on VSPI: SCK18 MISO19 MOSI23 CS5
  if (SD.begin(5, sdSPI)) { console.log("[sd] mounted"); return &SD; }
  console.log("[sd] mount failed (no card?)");
  return nullptr;
#else
  if (LittleFS.begin(false)) return &LittleFS;
  console.log("[anim] LittleFS mount failed (run 'uploadfs'?)");
  return nullptr;
#endif
}

// ── Status bar (STA / running) ──────────────────────────────────────────────
static String truncateToWidth(const String &s, int maxw) {
  if (tft.textWidth(s) <= maxw) return s;
  String out = s;
  while (out.length() > 1 && tft.textWidth(out + "...") > maxw) out.remove(out.length() - 1);
  return out + "...";
}

static void drawStatus() {
  const uint16_t BG = TFT_BLACK;
  const uint16_t MUTED = tft.color565(0x8b, 0x8f, 0x9a);
  tft.fillRect(0, STATUS_Y, tft.width(), tft.height() - STATUS_Y, BG);
  tft.drawFastHLine(0, STATUS_Y, tft.width(), tft.color565(0x2a, 0x2e, 0x3a));

  ClawdState d = store.dominant();

  // connection dot
  uint16_t dot = linkState == Link::Connected ? TFT_GREEN
               : linkState == Link::AuthFailed ? TFT_RED : TFT_ORANGE;
  tft.fillCircle(15, STATUS_Y + 23, 5, dot);

  // state "pill"
  int px = 32, py = STATUS_Y + 9, pw = tft.width() - px - 12, ph = 28;
  tft.fillRoundRect(px, py, pw, ph, ph / 2, clawdColor(d));
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(d == ClawdState::Sleeping ? TFT_BLACK : TFT_WHITE);
  tft.drawString(clawdName(d), px + pw / 2, py + ph / 2 + 1);

  // current task title (real context the protocol provides); fallback to IP
  String line = store.dominantTitle();
  if (!line.length()) line = WiFi.localIP().toString();
  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(MUTED, BG);
  tft.drawString(truncateToWidth(line, tft.width() - 16), tft.width() / 2, STATUS_Y + 48);
}

// ── clawd-on-desk WebSocket link ────────────────────────────────────────────
static void connectActiveHost() {
  if (cfg.hosts.empty()) { console.log("[net] no host configured"); return; }
  if (cfg.activeHost < 0 || cfg.activeHost >= (int)cfg.hosts.size()) cfg.activeHost = 0;
  ClawdHost &h = cfg.hosts[cfg.activeHost];
  console.logf("[net] using host %s:%u\n", h.host.c_str(), h.port);
  net.begin(h.host, h.port, cfg.token);
}

static void wireNet() {
  net.onSnapshotBegin = []() { store.clear(); statusDirty = true; };
  net.onSession = [](const String &sid, ClawdState st, const String &title) { store.upsert(sid, st, title); gotState = true; statusDirty = true; };
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
    console.log("[cfg] seeded from secrets.h");
  }
#endif
}

// ── State transitions ───────────────────────────────────────────────────────
static void enterProvisioning() {
  app = App::Provisioning;
  portal.beginSetup();
  console.log("[app] provisioning (AP + captive portal)");
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
  portal.statusProvider = []() -> String { // live status + telemetry for /api/state
    String s = "{";
    s += "\"state\":\"";      s += clawdName(store.dominant());            s += "\",";
    s += "\"title\":\"";      s += jsonEsc(store.dominantTitle());          s += "\",";
    s += "\"link\":\"";       s += (linkState == Link::Connected ? "up" : linkState == Link::AuthFailed ? "auth" : "down"); s += "\",";
    s += "\"sessions\":";     s += store.size();                            s += ",";
    s += "\"ip\":\"";         s += WiFi.localIP().toString();               s += "\",";
    s += "\"rssi\":";         s += WiFi.RSSI();                             s += ",";
    s += "\"heap\":";         s += ESP.getFreeHeap() / 1024;                s += ",";
    s += "\"heapTotal\":";    s += ESP.getHeapSize() / 1024;                s += ",";
    s += "\"up\":";           s += millis() / 1000;                         s += ",";
    s += "\"temp\":";         s += String(temperatureRead(), 1);            s += ",";
    s += "\"flashUsed\":";    s += ESP.getSketchSize() / 1024;              s += ",";
    s += "\"flashTotal\":";   s += (ESP.getSketchSize() + ESP.getFreeSketchSpace()) / 1024; s += ",";
    s += "\"loop\":";         s += (uint32_t)g_loopHz;                      s += ",";
    s += "\"fw\":\"";         s += FW_VERSION;                              s += "\"";
    s += "}";
    return s;
  };
  portal.beginDashboard(cfg);
  wireNet();
  connectActiveHost();
  runningStart = millis();
  showInfo = true; infoDrawn = false; gotState = false;
  console.log("[app] running (STA + dashboard)");
}

// ── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  console.begin(115200);
  delay(300);
  console.log("=== clawd-on-esp ===");

#ifdef TFT_BL
  ledcSetup(BL_CHANNEL, 5000, 8);          // PWM backlight for brightness control
  ledcAttachPin(TFT_BL, BL_CHANNEL);
  applyBrightness(100);
#endif
  tft.init();
  tft.setRotation(0);
  tft.fillScreen(TFT_BLACK);

  anim.begin(&tft, mountAssetFs());

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
  static uint32_t loopCount = 0, loopWin = 0;
  loopCount++;
  if (millis() - loopWin >= 1000) { g_loopHz = loopCount; loopCount = 0; loopWin = millis(); }

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
      console.logf("[net] failover -> host %d\n", cfg.activeHost);
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
