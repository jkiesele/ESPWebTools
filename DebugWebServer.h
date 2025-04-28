#pragma once
#include <WiFi.h>
#include <WebServer.h>
#include "LoggingBase.h"

class DebugWebServer : public LoggingBase {
public:
    // ctor: HTTP port (usually 80)
    DebugWebServer(uint16_t port = 80)
      : server(port)
    {}

    // start the server (call after Wi-Fi is connected)
    void begin() {
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
    }
    void println(const String& msg) override {
        addLog(msg, /*newLine=*/true);
    }

private:
    WebServer server;
    static constexpr size_t MAX_LINES = 50;
    std::vector<String> logs;

    void addLog(const String& msg, bool newLine) {
        if (newLine || logs.empty()) {
            logs.emplace_back(msg);
        } else {
            logs.back() += msg;
        }
        if (logs.size() > MAX_LINES) {
            logs.erase(logs.begin());
        }
    }

    void handleRoot() {
        String html =
          "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
          "<title>Debug Log</title></head><body>"
          "<h1>Debug Log</h1><pre>";
        for (auto &line : logs) {
            html += line + "\n";
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