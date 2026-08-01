#pragma once
// Loads/saves ClawdConfigData as JSON in NVS, and handles admin-password hashing.
// NVS survives `upload`/`uploadfs`, so user config is not lost on a re-flash.
#include "Config.h"

namespace ConfigStore {

ClawdConfigData load();                 // returns defaults (empty) if nothing stored
bool           save(const ClawdConfigData &cfg);
void           clear();                 // wipe stored config

// Admin password (never stored in plaintext)
String sha256Hex(const String &input);
void   setAdminPassword(ClawdConfigData &cfg, const String &plaintext);
bool   checkAdminPassword(const ClawdConfigData &cfg, const String &plaintext);

// Legacy helpers kept so existing callers keep working; operate on cfg.token.
String loadToken();
void   saveToken(const String &token);
void   clearToken();

}  // namespace ConfigStore
