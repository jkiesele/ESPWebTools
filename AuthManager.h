// AuthManager.h
#pragma once
#include <Arduino.h>

class AuthManager {
public:
  struct Session { String token; uint32_t lastSeenMs = 0; };

  void setIdleTimeoutMs(uint32_t t) { idleMs_ = t; }
  void clear() { session_ = {}; }

  String createSession();                 // creates token and returns it
  bool validate(const String& token);     // updates lastSeen if ok

private:
  Session session_{};
  uint32_t idleMs_ = 24u*60u*60u*1000u;       // default: 24 hours
};