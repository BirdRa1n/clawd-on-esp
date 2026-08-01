#pragma once
// AnimationManager — plays the GIF for the current dominant state on the TFT.
// GIFs are decoded straight from LittleFS (file-mode) so RAM stays tiny
// regardless of file size. See docs/clawd-esp32/02-ARCHITECTURE.md §3/§5.
#include <Arduino.h>
#include <TFT_eSPI.h>
#include "ClawdState.h"

class AnimationManager {
public:
  bool begin(TFT_eSPI *tft);          // mounts LittleFS, inits decoder
  void setState(ClawdState s);        // switch animation when the state changes
  void loop();                        // advance a frame when its delay elapses
  ClawdState current() const { return _current; }
  bool assetsOk() const { return _assetsOk; }

private:
  bool openState(ClawdState s);
  void drawFallback(ClawdState s);    // coloured block if the GIF is missing

  TFT_eSPI  *_tft = nullptr;
  ClawdState _current = (ClawdState)0xFF;
  bool       _open = false;
  bool       _assetsOk = false;
  uint32_t   _nextFrameAt = 0;
};
