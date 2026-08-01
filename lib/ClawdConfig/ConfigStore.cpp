#include "ConfigStore.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include "mbedtls/sha256.h"

namespace {
const char *NVS_NS  = "clawd";
const char *KEY_CFG = "cfg";      // JSON blob
const char *KEY_TOK = "token";    // legacy single-token key (migrated on load)
}  // namespace

ClawdConfigData ConfigStore::load() {
  ClawdConfigData c;
  Preferences p;
  p.begin(NVS_NS, /*readOnly=*/true);
  String js = p.getString(KEY_CFG, "");
  String legacyToken = p.getString(KEY_TOK, "");
  p.end();

  if (js.length()) {
    JsonDocument d;
    if (deserializeJson(d, js) == DeserializationError::Ok) {
      for (JsonObject n : d["networks"].as<JsonArray>()) {
        WiFiNet w;
        w.ssid = n["s"].as<String>();
        w.pass = n["p"].as<String>();
        w.priority = n["pr"] | 0;
        c.networks.push_back(w);
      }
      for (JsonObject h : d["hosts"].as<JsonArray>()) {
        ClawdHost hh;
        hh.host = h["h"].as<String>();
        hh.port = (uint16_t)(h["p"] | 23334);
        c.hosts.push_back(hh);
      }
      c.activeHost  = d["active"] | 0;
      c.token       = d["token"].as<String>();
      c.adminHash   = d["admin"].as<String>();
      c.provisioned = d["prov"] | false;
    }
  }
  // Migrate a token written by the older single-key scheme.
  if (c.token.isEmpty() && legacyToken.length()) c.token = legacyToken;
  return c;
}

bool ConfigStore::save(const ClawdConfigData &c) {
  JsonDocument d;
  JsonArray na = d["networks"].to<JsonArray>();
  for (const auto &n : c.networks) {
    JsonObject o = na.add<JsonObject>();
    o["s"] = n.ssid;
    o["p"] = n.pass;
    o["pr"] = n.priority;
  }
  JsonArray ha = d["hosts"].to<JsonArray>();
  for (const auto &h : c.hosts) {
    JsonObject o = ha.add<JsonObject>();
    o["h"] = h.host;
    o["p"] = h.port;
  }
  d["active"] = c.activeHost;
  d["token"]  = c.token;
  d["admin"]  = c.adminHash;
  d["prov"]   = c.provisioned;

  String js;
  serializeJson(d, js);

  Preferences p;
  p.begin(NVS_NS, false);
  bool ok = p.putString(KEY_CFG, js) > 0;
  p.remove(KEY_TOK);  // consolidated into the blob
  p.end();
  return ok;
}

void ConfigStore::clear() {
  Preferences p;
  p.begin(NVS_NS, false);
  p.clear();
  p.end();
}

String ConfigStore::sha256Hex(const String &input) {
  uint8_t out[32];
  mbedtls_sha256((const uint8_t *)input.c_str(), input.length(), out, 0);
  char hex[65];
  for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", out[i]);
  hex[64] = '\0';
  return String(hex);
}

void ConfigStore::setAdminPassword(ClawdConfigData &cfg, const String &plaintext) {
  cfg.adminHash = sha256Hex(plaintext);
}

bool ConfigStore::checkAdminPassword(const ClawdConfigData &cfg, const String &plaintext) {
  return cfg.hasAdmin() && cfg.adminHash == sha256Hex(plaintext);
}

// ── Legacy token helpers (keep existing callers working) ────────────────────
String ConfigStore::loadToken() { return load().token; }

void ConfigStore::saveToken(const String &t) {
  ClawdConfigData c = load();
  c.token = t;
  save(c);
}

void ConfigStore::clearToken() {
  ClawdConfigData c = load();
  c.token = "";
  save(c);
}
