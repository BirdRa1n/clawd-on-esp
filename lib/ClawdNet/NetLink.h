#pragma once
// NetLink — owns the WebSocket client and speaks Clawd Mobile Protocol v1.
// It parses snapshot / state / session_deleted / token_rotate and surfaces them
// as callbacks. Token handling (persist/ack) is protocol-level and lives here;
// what to DO with a rotated or rejected token is decided by the owner via
// onTokenRotate / onAuthSuspected. See docs/clawd-esp32/01-PROTOCOL.md.
#include <Arduino.h>
#include <functional>
#include <WebSocketsClient.h>
#include "ClawdState.h"

class NetLink {
public:
  // Callbacks (set before begin()):
  std::function<void()> onSnapshotBegin;                                     // clear the store
  std::function<void(const String &, ClawdState, const String &)> onSession; // upsert one session (sid, state, title)
  std::function<void(const String &)> onDeleted;               // remove one session
  std::function<void(bool)> onLink;                            // link up/down
  std::function<void(const String &)> onTokenRotate;           // persist new token
  std::function<void()> onAuthSuspected;                       // fast close w/o data

  void begin(const String &host, uint16_t port, const String &token);
  void loop();
  void useToken(const String &token);   // switch token used for future connects
  bool isConnected() const { return _connected; }

private:
  void openSocket();
  void onEvent(WStype_t type, uint8_t *payload, size_t length);
  void handleText(uint8_t *payload, size_t length);

  WebSocketsClient _ws;
  String   _host;
  uint16_t _port = 0;
  String   _token;
  String   _path;                       // kept alive for the lib

  bool     _connected = false;
  bool     _hadConnected = false;       // CONNECTED fired this attempt
  bool     _gotData = false;            // any frame received this attempt
  bool     _doReopen = false;           // re-begin with a new token on next loop
  uint32_t _connectAt = 0;
  uint8_t  _authFailStreak = 0;

  static const uint32_t RECONNECT_MS = 3000;  // lib auto-reconnect interval
  static const uint32_t AUTH_FAST_MS = 3000;  // close within this w/o data => auth?
};
