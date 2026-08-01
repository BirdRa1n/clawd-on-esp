#pragma once
// Decides between Station mode (connect to a known network by priority) and
// Access-Point mode (start the setup portal). Handles STA reconnection.
#include <Arduino.h>
#include <functional>
#include "Config.h"

class WiFiConnection {
public:
  enum class Mode { Boot, Station, AccessPoint };

  void begin(const ClawdConfigData &cfg);   // fires onStation or onAP synchronously
  void loop();                              // maintains the STA link

  Mode   mode() const { return _mode; }
  bool   isAP() const { return _mode == Mode::AccessPoint; }
  bool   isStation() const { return _mode == Mode::Station; }
  bool   staConnected() const;
  String ip() const;                        // AP IP or STA IP
  String apSsid() const { return _apSsid; }

  std::function<void()> onStation;          // STA connected
  std::function<void()> onAP;               // AP/setup mode started

private:
  bool tryConnectByPriority();
  void startAP();

  ClawdConfigData _cfg;
  Mode     _mode = Mode::Boot;
  String   _apSsid;
  uint32_t _lastReconnect = 0;
};
