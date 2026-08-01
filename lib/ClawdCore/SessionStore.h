#pragma once
// Local mirror of the desktop session cache: sessionId -> {state, title}.
// Fed by snapshot / state / session_deleted (see 01-PROTOCOL.md §4).
#include <Arduino.h>
#include <map>
#include "ClawdState.h"

struct Session {
  ClawdState state = ClawdState::Idle;
  String     title;
};

class SessionStore {
public:
  std::map<String, Session> sessions;

  void clear() { sessions.clear(); }
  void upsert(const String &sid, ClawdState st, const String &title) {
    Session &s = sessions[sid];
    s.state = st;
    if (title.length()) s.title = title;
  }
  void remove(const String &sid) { sessions.erase(sid); }
  size_t size() const { return sessions.size(); }

  // Dominant state = highest priority across sessions; empty -> Idle.
  ClawdState dominant() const {
    if (sessions.empty()) return ClawdState::Idle;
    ClawdState best = ClawdState::Sleeping;
    for (const auto &kv : sessions)
      if (clawdPriority(kv.second.state) > clawdPriority(best)) best = kv.second.state;
    return best;
  }

  // Title of the highest-priority session (what the mascot is reacting to).
  String dominantTitle() const {
    const Session *best = nullptr;
    for (const auto &kv : sessions)
      if (!best || clawdPriority(kv.second.state) > clawdPriority(best->state)) best = &kv.second;
    return best ? best->title : String();
  }
};
