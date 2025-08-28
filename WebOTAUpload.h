#pragma once
#include <Arduino.h>
#include <functional>
#include <WebServer.h>
#include <Update.h>
#include <LoggingBase.h>   // uses global gLogger
#include <esp_ota_ops.h>
#include <WebItem.h>       // provides virtual access to setupRoutes and generateHTML (BasicWebInterface)
#include "passwords.h"   // for default password

// Provide a password validator hook so you can read from NVS/SettingsBlockBase.
using OTAPasswordValidator = std::function<bool(const String& pw)>;

class WebOTAUpload : public WebItem {
public:
    // Fixed password (simple)
    WebOTAUpload(const String& password = secret::otaPassword, const String& route = "/otaupdate");

    // Dynamic validator (e.g. read from settings)
    WebOTAUpload(OTAPasswordValidator validator, const String& route = "/otaupdate");

    // WebItem API
    void setupRoutes(WebServer& server) override;
    String generateHTML() const override;

    // Accessors
    const String& route() const { return route_; }

    // Call this after verifying a successful boot in begin() to prevent rollback
    void markAppValid();

private:
    WebServer* server_ = nullptr;
    String route_;
    OTAPasswordValidator validator_;
    bool uploadStarted_ = false;

    String buildPage_() const;
};