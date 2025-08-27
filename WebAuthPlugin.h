// WebAuthPlugin.h
#pragma once
#include <WebServer.h>
#include "AuthManager.h"
#include "passwords.h"

class WebAuthPlugin {
public:
  static WebAuthPlugin& instance();        // singleton

  // Configuration
  void setEnabled(bool on)             { enabled_ = on; }
  void setPostOnlyLockdown(bool on)    { postOnlyLockdown_ = on; }
  bool enabled() const                 { return enabled_; }
  bool postOnlyLockdown() const        { return postOnlyLockdown_; }

  // API (simplified)
  void install(WebServer& srv);  // 
  bool require(std::initializer_list<const char*> extraWhitelist = {});


  void setIdleTimeoutMs(uint32_t ms) { auth_.setIdleTimeoutMs(ms); }
  bool isActive() const { return installed_ && enabled_; }

private:
  WebAuthPlugin() = default;
  WebAuthPlugin(const WebAuthPlugin&) = delete;
  WebAuthPlugin& operator=(const WebAuthPlugin&) = delete;

  bool isWhitelistedPath_(const String& path,
                          std::initializer_list<const char*> extra) const;
  static String parseCookieToken_(WebServer& srv, const char* cookieName);
  static void sendRedirect_(WebServer& srv, const String& to);
  static String htmlLogin_();

  AuthManager auth_;
  bool installed_ = false;
  static constexpr const char* kCookieName_ = "BWI_SESSION";

  bool enabled_ = true;              // NEW: global enable/disable
  bool postOnlyLockdown_ = false;    // NEW: true → lock POSTs only

  WebServer* server_ = nullptr; // not owned
};