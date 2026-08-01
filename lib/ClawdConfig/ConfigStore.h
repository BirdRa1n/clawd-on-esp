#pragma once
// Persists the (possibly rotated) mobile token in NVS so a reboot after a 24h
// token rotation does not lock us out past the 5-min grace window.
// See 01-PROTOCOL.md §5.
#include <Arduino.h>
#include <Preferences.h>

namespace ConfigStore {

inline String loadToken() {
  Preferences p;
  p.begin("clawd", /*readOnly=*/true);
  String t = p.getString("token", "");
  p.end();
  return t;
}

inline void saveToken(const String &t) {
  Preferences p;
  p.begin("clawd", false);
  p.putString("token", t);
  p.end();
}

inline void clearToken() {
  Preferences p;
  p.begin("clawd", false);
  p.remove("token");
  p.end();
}

}  // namespace ConfigStore
