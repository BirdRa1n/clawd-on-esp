#pragma once
// Web server for provisioning + configuration.
//  - Setup mode (AP): captive portal to enter Wi-Fi + clawd host/token + admin pass.
//  - Dashboard mode (STA): admin-protected page to manage networks, hosts and token.
// Pages are embedded in the firmware (PROGMEM) so no filesystem upload is needed.
#include <Arduino.h>
#include <functional>
#include <WebServer.h>
#include <DNSServer.h>
#include "Config.h"

class WebPortal {
public:
  void beginSetup();                              // AP captive setup
  void beginDashboard(const ClawdConfigData &cfg);  // STA dashboard
  void loop();
  bool rebootRequested() const { return _reboot; }

  std::function<void()> onConfigChanged;          // fired after a dashboard save
  std::function<String()> statusProvider;         // returns live status+telemetry JSON object

private:
  void routesSetup();
  void routesDashboard();
  bool authed();                                  // HTTP Basic vs cfg.adminHash (no response)
  bool requireAdmin();                            // authed() else send 401

  WebServer       _server{80};
  DNSServer       _dns;
  ClawdConfigData _cfg;
  bool            _isSetup = false;
  bool            _reboot = false;
  bool            _otaAuth = false;   // OTA upload authorised this request
};
