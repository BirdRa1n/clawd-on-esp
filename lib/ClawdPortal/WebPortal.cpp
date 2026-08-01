#include "WebPortal.h"
#include "ConfigStore.h"
#include <WiFi.h>
#include "mbedtls/base64.h"

// ── Shared theme (Clawd: dark + coral accent) ───────────────────────────────
static const char *STYLE = R"CSS(
<style>
*{box-sizing:border-box}
body{background:#111318;color:#e7e7ea;font-family:system-ui,-apple-system,sans-serif;margin:0;padding:16px}
.card{max-width:460px;margin:14px auto;background:#1b1e26;border:1px solid #2a2e3a;border-radius:14px;padding:20px}
h1{color:#d97757;font-size:20px;margin:0 0 2px}
h2{font-size:15px;color:#e7e7ea;margin:18px 0 6px;border-top:1px solid #2a2e3a;padding-top:14px}
.sub{color:#8b8f9a;font-size:13px;margin:0 0 8px}
label{display:block;font-size:12px;color:#b9bdc8;margin:10px 0 4px}
input,select{width:100%;background:#0e1016;color:#fff;border:1px solid #2a2e3a;border-radius:8px;padding:10px;font-size:15px}
button{width:100%;margin-top:16px;background:#d97757;color:#161821;border:0;border-radius:8px;padding:12px;font-size:16px;font-weight:600;cursor:pointer}
button.sec{background:#2a2e3a;color:#e7e7ea;font-weight:500}
.row{display:flex;gap:8px}.row>*{flex:1}
.item{display:flex;justify-content:space-between;align-items:center;background:#0e1016;border:1px solid #2a2e3a;border-radius:8px;padding:8px 12px;margin-top:6px;font-size:14px}
.item a{color:#ef4444;text-decoration:none;font-weight:600}
.mini{font-size:12px;color:#8b8f9a;margin-top:8px}
a{color:#d97757}
</style>)CSS";

static String head(const char *title) {
  String s = "<!doctype html><html><head><meta charset=utf-8>"
             "<meta name=viewport content='width=device-width,initial-scale=1'>";
  s += "<title>Clawd</title>";
  s += STYLE;
  s += "</head><body><div class=card><h1>\xF0\x9F\xA6\x80 ";
  s += title;
  s += "</h1>";
  return s;
}
static String foot() { return "</div></body></html>"; }
static String esc(const String &v) {
  String o; for (char c : v) { if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '"') o += "&quot;"; else o += c; }
  return o;
}

// ── Setup (AP captive) ──────────────────────────────────────────────────────
void WebPortal::beginSetup() {
  _isSetup = true;
  _dns.start(53, "*", WiFi.softAPIP());
  routesSetup();
  _server.begin();
}

void WebPortal::routesSetup() {
  _server.on("/", [this]() {
    String p = head("Configurar dispositivo");
    p += "<p class=sub>Conecte seu Clawd \xC3\xA0 sua rede Wi-Fi.</p>";
    p += "<form method=POST action=/save>";
    p += "<label>Rede Wi-Fi (SSID)</label><input name=ssid list=nets autocomplete=off required>";
    p += "<datalist id=nets></datalist>";
    p += "<label>Senha do Wi-Fi</label><input name=pass type=password>";
    p += "<h2>Clawd on Desk</h2>";
    p += "<label>Host / IP</label><input name=host placeholder=192.168.0.10 required>";
    p += "<label>Porta</label><input name=port value=23334>";
    p += "<label>Token</label><input name=token placeholder='token do pareamento'>";
    p += "<h2>Acesso</h2>";
    p += "<label>Senha de admin (para gerenciar depois)</label><input name=admin type=password required>";
    p += "<button>Salvar e conectar</button></form>";
    p += "<p class=mini>Dica: pegue host/porta/token no app desktop em Settings \xE2\x86\x92 Mobile.</p>";
    p += "<script>fetch('/scan').then(r=>r.json()).then(a=>{let d=document.getElementById('nets');"
         "a.forEach(s=>{let o=document.createElement('option');o.value=s;d.appendChild(o)})}).catch(()=>{})</script>";
    p += foot();
    _server.send(200, "text/html", p);
  });

  _server.on("/scan", [this]() {
    int n = WiFi.scanNetworks();
    String j = "[";
    for (int i = 0; i < n; i++) { if (i) j += ","; j += "\""; j += esc(WiFi.SSID(i)); j += "\""; }
    j += "]";
    _server.send(200, "application/json", j);
  });

  _server.on("/save", HTTP_POST, [this]() {
    ClawdConfigData c;
    WiFiNet net;
    net.ssid = _server.arg("ssid"); net.pass = _server.arg("pass"); net.priority = 10;
    if (net.ssid.length()) c.networks.push_back(net);
    ClawdHost h;
    h.host = _server.arg("host"); h.port = (uint16_t)_server.arg("port").toInt(); if (!h.port) h.port = 23334;
    if (h.host.length()) c.hosts.push_back(h);
    c.token = _server.arg("token");
    if (_server.arg("admin").length()) ConfigStore::setAdminPassword(c, _server.arg("admin"));
    c.provisioned = true;
    ConfigStore::save(c);

    String p = head("Salvo");
    p += "<p class=sub>Configura\xC3\xA7\xC3\xA3o salva. O dispositivo vai reiniciar e conectar \xC3\xA0 rede.</p>";
    p += foot();
    _server.send(200, "text/html", p);
    _reboot = true;
  });

  _server.onNotFound([this]() {  // captive-portal redirect
    _server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString(), true);
    _server.send(302, "text/plain", "");
  });
}

// ── Dashboard (STA, admin-protected) ────────────────────────────────────────
void WebPortal::beginDashboard(const ClawdConfigData &cfg) {
  _isSetup = false;
  _cfg = cfg;
  const char *hdrs[] = {"Authorization"};
  _server.collectHeaders(hdrs, 1);
  routesDashboard();
  _server.begin();
}

bool WebPortal::requireAdmin() {
  ClawdConfigData cfg = ConfigStore::load();
  if (!cfg.hasAdmin()) return true;   // not set yet -> allow so the user can set it
  String h = _server.hasHeader("Authorization") ? _server.header("Authorization") : "";
  if (h.startsWith("Basic ")) {
    String b64 = h.substring(6);
    uint8_t out[160]; size_t olen = 0;
    if (mbedtls_base64_decode(out, sizeof(out), &olen, (const uint8_t *)b64.c_str(), b64.length()) == 0) {
      String creds((char *)out, olen);
      int colon = creds.indexOf(':');
      String pass = colon >= 0 ? creds.substring(colon + 1) : "";
      if (ConfigStore::checkAdminPassword(cfg, pass)) return true;
    }
  }
  _server.sendHeader("WWW-Authenticate", "Basic realm=\"Clawd\"");
  _server.send(401, "text/plain", "Auth required");
  return false;
}

void WebPortal::routesDashboard() {
  auto redirect = [this]() { _server.sendHeader("Location", "/", true); _server.send(303, "text/plain", ""); };

  _server.on("/", [this]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    String p = head("Painel do dispositivo");
    p += "<p class=sub>IP: " + WiFi.localIP().toString() + "</p>";

    if (!c.hasAdmin()) {
      p += "<h2>Definir senha de admin</h2><form method=POST action=/admin>";
      p += "<input name=admin type=password placeholder='nova senha' required>";
      p += "<button>Definir senha</button></form>";
    }

    p += "<h2>Redes Wi-Fi</h2>";
    for (size_t i = 0; i < c.networks.size(); i++)
      p += "<div class=item><span>" + esc(c.networks[i].ssid) + " <span class=mini>prio " +
           String(c.networks[i].priority) + "</span></span><a href=/net/del?i=" + String(i) + ">remover</a></div>";
    p += "<form method=POST action=/net/add><div class=row>"
         "<input name=ssid placeholder=SSID required><input name=prio type=number value=10 style=max-width:80px></div>"
         "<input name=pass type=password placeholder=senha style=margin-top:6px>"
         "<button class=sec>Adicionar rede</button></form>";

    p += "<h2>Hosts do Clawd on Desk</h2>";
    for (size_t i = 0; i < c.hosts.size(); i++)
      p += "<div class=item><span>" + esc(c.hosts[i].host) + ":" + String(c.hosts[i].port) + "</span>"
           "<a href=/host/del?i=" + String(i) + ">remover</a></div>";
    p += "<form method=POST action=/host/add><div class=row>"
         "<input name=host placeholder=IP required><input name=port type=number value=23334 style=max-width:110px></div>"
         "<button class=sec>Adicionar host</button></form>";

    p += "<h2>Token</h2><form method=POST action=/token>";
    p += "<input name=token placeholder='novo token' value=\"" + esc(c.token) + "\">";
    p += "<button class=sec>Salvar token</button></form>";

    p += "<h2>Manuten\xC3\xA7\xC3\xA3o</h2><form method=POST action=/reboot><button>Reiniciar</button></form>";
    p += foot();
    _server.send(200, "text/html", p);
  });

  _server.on("/net/add", HTTP_POST, [this, redirect]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    WiFiNet n; n.ssid = _server.arg("ssid"); n.pass = _server.arg("pass");
    n.priority = _server.arg("prio").toInt();
    if (n.ssid.length()) { c.networks.push_back(n); ConfigStore::save(c); }
    redirect();
  });
  _server.on("/net/del", [this, redirect]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    int i = _server.arg("i").toInt();
    if (i >= 0 && i < (int)c.networks.size()) { c.networks.erase(c.networks.begin() + i); ConfigStore::save(c); }
    redirect();
  });
  _server.on("/host/add", HTTP_POST, [this, redirect]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    ClawdHost h; h.host = _server.arg("host"); h.port = (uint16_t)_server.arg("port").toInt(); if (!h.port) h.port = 23334;
    if (h.host.length()) { c.hosts.push_back(h); ConfigStore::save(c); }
    redirect();
  });
  _server.on("/host/del", [this, redirect]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    int i = _server.arg("i").toInt();
    if (i >= 0 && i < (int)c.hosts.size()) { c.hosts.erase(c.hosts.begin() + i); ConfigStore::save(c); }
    redirect();
  });
  _server.on("/token", HTTP_POST, [this, redirect]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    c.token = _server.arg("token"); ConfigStore::save(c);
    redirect();
  });
  _server.on("/admin", HTTP_POST, [this, redirect]() {
    if (!requireAdmin()) return;
    ClawdConfigData c = ConfigStore::load();
    if (_server.arg("admin").length()) { ConfigStore::setAdminPassword(c, _server.arg("admin")); ConfigStore::save(c); }
    redirect();
  });
  _server.on("/reboot", HTTP_POST, [this]() {
    if (!requireAdmin()) return;
    _server.send(200, "text/html", head("Reiniciando") + "<p class=sub>O dispositivo est\xC3\xA1 reiniciando...</p>" + foot());
    _reboot = true;
  });
}

void WebPortal::loop() {
  if (_isSetup) _dns.processNextRequest();
  _server.handleClient();
}
