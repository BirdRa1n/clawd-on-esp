#pragma once
// Local mirror of the desktop session cache: sessionId -> state.
// Fed by snapshot / state / session_deleted (see 01-PROTOCOL.md §4).
#include <Arduino.h>
#include <map>
#include "ClawdState.h"

class SessionStore {
public:
  std::map<String, ClawdState> sessions;

  void clear() { sessions.clear(); }
  void upsert(const String &sid, ClawdState st) { sessions[sid] = st; }
  void remove(const String &sid) { sessions.erase(sid); }
  size_t size() const { return sessions.size(); }

  // Dominant state = highest priority across sessions; empty -> Idle.
  ClawdState dominant() const {
    if (sessions.empty()) return ClawdState::Idle;
    ClawdState best = ClawdState::Sleeping;
    for (const auto &kv : sessions) {
      if (clawdPriority(kv.second) > clawdPriority(best)) best = kv.second;
    }
    return best;
  }
};
