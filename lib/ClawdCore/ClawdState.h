#pragma once
// Clawd display states — mirrors src/state-priority.js and the PWA STATE_CONFIG
// from clawd-on-desk. See docs/clawd-esp32/01-PROTOCOL.md §6.
#include <Arduino.h>

enum class ClawdState : uint8_t {
  Sleeping = 0,
  Idle,
  Thinking,
  Working,
  Juggling,
  Carrying,
  Attention,
  Sweeping,
  Notification,
  Error,
  Roam,
};

// Priority for choosing the dominant state across sessions (higher wins).
// Values copied verbatim from state-priority.js STATE_PRIORITY.
inline uint8_t clawdPriority(ClawdState s) {
  switch (s) {
    case ClawdState::Error:        return 8;
    case ClawdState::Notification: return 7;
    case ClawdState::Sweeping:     return 6;
    case ClawdState::Attention:    return 5;
    case ClawdState::Carrying:     return 4;
    case ClawdState::Juggling:     return 4;
    case ClawdState::Working:      return 3;
    case ClawdState::Thinking:     return 2;
    case ClawdState::Idle:         return 1;
    case ClawdState::Roam:         return 1;
    case ClawdState::Sleeping:     return 0;
  }
  return 0;
}

// Parse a protocol state string. Unknown/transitional states (yawning, dozing,
// waking, …) fall back to Idle so they never wrongly dominate; caller may log.
inline ClawdState clawdParse(const char *s) {
  if (!s) return ClawdState::Idle;
  if (!strcmp(s, "error"))        return ClawdState::Error;
  if (!strcmp(s, "notification")) return ClawdState::Notification;
  if (!strcmp(s, "sweeping"))     return ClawdState::Sweeping;
  if (!strcmp(s, "attention"))    return ClawdState::Attention;
  if (!strcmp(s, "carrying"))     return ClawdState::Carrying;
  if (!strcmp(s, "juggling"))     return ClawdState::Juggling;
  if (!strcmp(s, "working"))      return ClawdState::Working;
  if (!strcmp(s, "thinking"))     return ClawdState::Thinking;
  if (!strcmp(s, "idle"))         return ClawdState::Idle;
  if (!strcmp(s, "roam"))         return ClawdState::Roam;
  if (!strcmp(s, "sleeping"))     return ClawdState::Sleeping;
  return ClawdState::Idle;
}

inline const char *clawdName(ClawdState s) {
  switch (s) {
    case ClawdState::Error:        return "ERROR";
    case ClawdState::Notification: return "NOTIFICATION";
    case ClawdState::Sweeping:     return "SWEEPING";
    case ClawdState::Attention:    return "ATTENTION";
    case ClawdState::Carrying:     return "CARRYING";
    case ClawdState::Juggling:     return "JUGGLING";
    case ClawdState::Working:      return "WORKING";
    case ClawdState::Thinking:     return "THINKING";
    case ClawdState::Idle:         return "IDLE";
    case ClawdState::Roam:         return "ROAM";
    case ClawdState::Sleeping:     return "SLEEPING";
  }
  return "IDLE";
}

// RGB565 colour per state — matches the PWA STATE_CONFIG hex values.
inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
inline uint16_t clawdColor(ClawdState s) {
  switch (s) {
    case ClawdState::Error:        return rgb565(0xef, 0x44, 0x44); // red
    case ClawdState::Attention:    return rgb565(0xb4, 0x53, 0x09); // amber
    case ClawdState::Working:      return rgb565(0x22, 0xc5, 0x5e); // green
    case ClawdState::Juggling:     return rgb565(0x22, 0xc5, 0x5e); // green
    case ClawdState::Thinking:     return rgb565(0x3b, 0x82, 0xf6); // blue
    case ClawdState::Notification: return rgb565(0xd9, 0x77, 0x57); // coral
    case ClawdState::Sweeping:     return rgb565(0x71, 0x71, 0x7a); // grey
    case ClawdState::Carrying:     return rgb565(0x71, 0x71, 0x7a); // grey
    case ClawdState::Idle:         return rgb565(0x71, 0x71, 0x7a); // grey
    case ClawdState::Roam:         return rgb565(0x71, 0x71, 0x7a); // grey
    case ClawdState::Sleeping:     return rgb565(0xa1, 0xa1, 0xaa); // light grey
  }
  return rgb565(0x71, 0x71, 0x7a);
}
