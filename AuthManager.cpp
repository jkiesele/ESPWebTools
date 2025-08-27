
#include "AuthManager.h"
#include <esp_system.h>

String AuthManager::createSession() {
  static const char* hex = "0123456789abcdef";
  char buf[33];
  for (int i = 0; i < 32; ++i) {
    uint8_t b = (uint8_t)esp_random();
    buf[i] = hex[b & 0x0F];
  }
  buf[32] = 0;
  session_.token = String(buf);
  session_.lastSeenMs = millis();
  return session_.token;
}

bool AuthManager::validate(const String& token) {
  if (!token.length() || token != session_.token) return false;
  uint32_t now = millis();
  if (idleMs_ && (now - session_.lastSeenMs > idleMs_)) { session_ = {}; return false; }
  session_.lastSeenMs = now;
  return true;
}