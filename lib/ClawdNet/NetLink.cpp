#include "NetLink.h"
#include <ArduinoJson.h>

// Filter so we only materialise the fields we care about (state drives the UI).
// recentEvents, titles, timestamps etc. are dropped -> tiny RAM footprint.
static JsonDocument makeFilter() {
  JsonDocument f;
  f["type"] = true;
  f["sessionId"] = true;
  f["newToken"] = true;
  f["data"]["state"] = true;
  f["data"]["title"] = true;
  f["sessions"]["*"]["state"] = true;
  f["sessions"]["*"]["title"] = true;
  return f;
}

void NetLink::begin(const String &host, uint16_t port, const String &token) {
  _host = host;
  _port = port;
  _token = token;
  _ws.onEvent([this](WStype_t t, uint8_t *p, size_t l) { this->onEvent(t, p, l); });
  openSocket();
}

void NetLink::useToken(const String &token) {
  if (token.length() < 32 || token == _token) return;
  _token = token;
  _doReopen = true;   // re-begin with the new token on the next loop()
}

void NetLink::openSocket() {
  _path = "/ws?token=" + _token;
  _hadConnected = false;
  _gotData = false;
  Serial.printf("[net] connecting ws://%s:%u/ws\n", _host.c_str(), _port);
  _ws.begin(_host.c_str(), _port, _path.c_str());
  // Sane reconnect interval — the library drives reconnection itself using the
  // URL from begin(). (Do NOT set a huge value: the loop() throttle compares
  // millis()-_lastConnectionFail against it, and _lastConnectionFail starts at
  // 0, so a huge interval blocks the very first connect for that whole window.)
  _ws.setReconnectInterval(RECONNECT_MS);
}

void NetLink::loop() {
  _ws.loop();
  if (_doReopen) { _doReopen = false; openSocket(); }
}

void NetLink::onEvent(WStype_t type, uint8_t *payload, size_t length) {
  switch (type) {
    case WStype_CONNECTED:
      _connected = true;
      _hadConnected = true;
      _connectAt = millis();
      _gotData = false;
      Serial.println("[net] connected");
      if (onLink) onLink(true);
      break;

    case WStype_DISCONNECTED: {
      bool was = _connected;
      _connected = false;
      if (was) Serial.println("[net] disconnected");
      if (onLink) onLink(false);
      // Heuristic auth check: server accepts the upgrade then closes 1008 fast.
      if (_hadConnected && !_gotData && millis() - _connectAt < AUTH_FAST_MS) {
        if (++_authFailStreak >= 3) {
          Serial.println("[net] auth suspected (fast close, no data)");
          if (onAuthSuspected) onAuthSuspected();
          _authFailStreak = 0;
        }
      }
      break;
    }

    case WStype_TEXT:
      _gotData = true;
      _authFailStreak = 0;
      handleText(payload, length);
      break;

    case WStype_ERROR:
      Serial.println("[net] ws error");
      break;

    default:
      break;  // PING/PONG handled by the library
  }
}

void NetLink::handleText(uint8_t *payload, size_t length) {
  static JsonDocument filter = makeFilter();
  JsonDocument doc;
  DeserializationError err =
      deserializeJson(doc, payload, length, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("[net] json err: %s\n", err.c_str());
    return;
  }

  const char *type = doc["type"] | "";

  if (!strcmp(type, "snapshot")) {
    if (onSnapshotBegin) onSnapshotBegin();
    JsonObject sessions = doc["sessions"].as<JsonObject>();
    int n = 0;
    for (JsonPair kv : sessions) {
      ClawdState st = clawdParse(kv.value()["state"] | "idle");
      String title = kv.value()["title"] | "";
      if (onSession) onSession(String(kv.key().c_str()), st, title);
      n++;
    }
    Serial.printf("[net] snapshot: %d sessions\n", n);

  } else if (!strcmp(type, "state")) {
    const char *sid = doc["sessionId"] | "";
    ClawdState st = clawdParse(doc["data"]["state"] | "idle");
    String title = doc["data"]["title"] | "";
    if (sid[0] && onSession) onSession(String(sid), st, title);
    Serial.printf("[net] state: %s -> %s\n", sid, clawdName(st));

  } else if (!strcmp(type, "session_deleted")) {
    const char *sid = doc["sessionId"] | "";
    if (sid[0] && onDeleted) onDeleted(String(sid));
    Serial.printf("[net] session_deleted: %s\n", sid);

  } else if (!strcmp(type, "token_rotate")) {
    const char *nt = doc["newToken"] | "";
    if (nt[0]) {
      String ack = "{\"type\":\"token_rotate_ack\"}";
      _ws.sendTXT(ack);   // ack per protocol
      useToken(String(nt));
      if (onTokenRotate) onTokenRotate(String(nt));
      Serial.println("[net] token rotated + acked");
    }
  }
}
