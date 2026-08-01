#pragma once
// Persistent device configuration model.
// Stored as JSON in NVS (survives firmware/filesystem re-flash) — see ConfigStore.
#include <Arduino.h>
#include <vector>

struct WiFiNet {
  String ssid;
  String pass;
  int    priority = 0;   // higher = preferred when multiple are in range
};

struct ClawdHost {
  String   host;
  uint16_t port = 23334;
};

struct ClawdConfigData {
  std::vector<WiFiNet>   networks;   // known Wi-Fi networks (priority-ordered at use)
  std::vector<ClawdHost> hosts;      // clawd-on-desk endpoints to try, in order
  int      activeHost = 0;           // index of the currently preferred host
  String   token;                    // mobile pairing token (rotates at runtime)
  String   adminHash;                // SHA-256 hex of the admin password ("" = unset)
  bool     provisioned = false;      // true once the user completes setup
  uint8_t  mascotScale = 100;        // mascot render size, percent (30..100)
  uint8_t  brightness = 100;         // backlight brightness, percent (10..100)

  bool hasNetworks() const { return !networks.empty(); }
  bool hasAdmin() const { return adminHash.length() == 64; }
};
