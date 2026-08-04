#include "WiFiConnection.h"
#include "Console.h"
#include <WiFi.h>

static String macSuffix() {
  uint8_t m[6];
  WiFi.macAddress(m);
  char b[5];
  sprintf(b, "%02X%02X", m[4], m[5]);
  return String(b);
}

void WiFiConnection::begin(const ClawdConfigData &cfg) {
  _cfg = cfg;
  WiFi.persistent(false);
  WiFi.setSleep(false);
  if (_cfg.hasNetworks() && tryConnectByPriority()) {
    _mode = Mode::Station;
    console.logf("[wifi] STA connected: %s\n", WiFi.localIP().toString().c_str());
    if (onStation) onStation();
  } else {
    startAP();
  }
}

// Scans, then connects to the highest-priority configured network that is
// visible. Falls back to the highest-priority configured network (hidden SSID).
bool WiFiConnection::tryConnectByPriority() {
  WiFi.mode(WIFI_STA);
  int found = WiFi.scanNetworks();
  int bestIdx = -1, bestPrio = -1;
  for (size_t i = 0; i < _cfg.networks.size(); i++) {
    for (int j = 0; j < found; j++) {
      if (WiFi.SSID(j) == _cfg.networks[i].ssid && _cfg.networks[i].priority > bestPrio) {
        bestPrio = _cfg.networks[i].priority;
        bestIdx = (int)i;
      }
    }
  }
  if (bestIdx < 0) {  // none visible — try the highest-priority known one anyway
    for (size_t i = 0; i < _cfg.networks.size(); i++) {
      if (_cfg.networks[i].priority > bestPrio) { bestPrio = _cfg.networks[i].priority; bestIdx = (int)i; }
    }
  }
  if (bestIdx < 0) return false;

  const WiFiNet &net = _cfg.networks[bestIdx];
  console.logf("[wifi] connecting to '%s' (prio %d)\n", net.ssid.c_str(), net.priority);
  WiFi.begin(net.ssid.c_str(), net.pass.c_str());
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) delay(200);
  return WiFi.status() == WL_CONNECTED;
}

void WiFiConnection::startAP() {
  WiFi.mode(WIFI_AP);
  _apSsid = "Clawd-Setup-" + macSuffix();
  WiFi.softAP(_apSsid.c_str());   // open network
  _mode = Mode::AccessPoint;
  console.logf("[wifi] AP mode: %s  http://%s\n", _apSsid.c_str(), WiFi.softAPIP().toString().c_str());
  if (onAP) onAP();
}

bool WiFiConnection::staConnected() const { return WiFi.status() == WL_CONNECTED; }

String WiFiConnection::ip() const {
  return _mode == Mode::AccessPoint ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}

void WiFiConnection::loop() {
  if (_mode == Mode::Station && WiFi.status() != WL_CONNECTED) {
    if (millis() - _lastReconnect > 10000) {
      _lastReconnect = millis();
      console.log("[wifi] STA lost, reconnecting...");
      WiFi.reconnect();
    }
  }
}
