#pragma once
// Copy this file to `include/secrets.h` and fill in your values.
// `secrets.h` is gitignored so your credentials never get committed.
//
//   cp include/secrets.example.h include/secrets.h
//
// The bring-up firmware (Phase 1) only needs WIFI_*. The CLAWD_* values are
// used from Phase 2 onward (WebSocket client). Get host/port/token from the
// Clawd desktop app: Settings → Mobile pairing.

#define WIFI_SSID     "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"

#define CLAWD_HOST  "192.168.1.10"
#define CLAWD_PORT  23334
#define CLAWD_TOKEN "0123456789abcdef0123456789abcdef"
