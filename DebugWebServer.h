#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include "LoggingBase.h"

class DebugWebServer : public LoggingBase {
public:
    // ctor: HTTP port (usually 80)
    DebugWebServer(String hostname="debugwebserver", uint16_t port = 80)
      : server(port), hostName(hostname)
    {}

    // start the server (call after Wi-Fi is connected)
    void begin() {
        if (!MDNS.begin(hostName)) {  // Set a hostname as desired
            gLogger->println("Error setting up MDNS responder!");
        } else {
          gLogger->println("MDNS responder started. Access at "+hostName+".local");
        }
        server.on("/",    [this]() { handleRoot(); });
        server.on("/log", [this]() { handleLog();  });
        server.begin();
    }

    // call in your loop()
    void handleClient() {
        server.handleClient();
    }
    void loop() {
        handleClient();
    }

    // LoggingBase API
    void print(const String& msg) override {
        addLog(msg, /*newLine=*/false);
        Serial.print(msg);//mirror to serial
    }
    void println(const String& msg) override {
        addLog(msg, /*newLine=*/true);
        Serial.println(msg);//mirror to serial
    }

private:
    WebServer server;
    String hostName;
    static constexpr size_t MAX_LINES = 50;
    std::vector<String> logs;
    bool nextNewLine = false;

    void addLog(const String& msg, bool newLine) {
        if (nextNewLine || logs.empty()) {
            logs.emplace_back(msg);
        } else {
            logs.back() += msg;
        }
        if (logs.size() > MAX_LINES) {
            logs.erase(logs.begin());
        }
        nextNewLine = newLine;
    }

    void handleRoot() {
        String html =
          "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
          "<title>Debug Log</title></head><body>"
          "<h1>Debug Log</h1><pre>";
        for (auto &line : logs) {
            html += line + "\n\n";
        }
        html += "</pre></body></html>";
        server.send(200, "text/html", html);
    }

    void handleLog() {
        String txt;
        for (auto &line : logs) {
            txt += line + "\n";
        }
        server.send(200, "text/plain", txt);
    }
};