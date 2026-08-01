#pragma once
// Full-screen info page: title + QR code + up to three text lines.
// Used for the provisioning screen (Wi-Fi join QR) and the connected screen
// (dashboard URL QR). Themed to match Clawd (dark + coral).
#include <TFT_eSPI.h>

namespace InfoScreen {
void show(TFT_eSPI &tft, const char *title, const char *qrData,
          const String &l1, const String &l2 = "", const String &l3 = "");
}
