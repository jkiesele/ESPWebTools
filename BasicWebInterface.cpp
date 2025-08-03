#include <BasicWebInterface.h>
#include <SystemID.h>
#include <ESPmDNS.h>
#include "WebStatus.h"

void BasicWebInterface::begin() {

    if (!MDNS.begin(systemID.systemName())) {  // Replace with your desired hostname
        Serial.println("Error setting up MDNS responder!");
    } else {
        Serial.println("mDNS responder started. Access at http://"+systemID.systemName()+ ".local");
    }
    server.begin();
    setupRoutes();
}
void BasicWebInterface::setupRoutes() {
    server.on("/", HTTP_GET, [this]() {
        server.send(200, "text/html", generateHTML());
    });

    for (const auto& display : displays_) {
        server.on(display.second->handle(), HTTP_GET, [display,this]() {
           server.send(200, "application/json", display.second->routeText());
        });
    }
    for(const auto& settingsDisplay : settingsDisplays_) {
        settingsDisplay.second->setupRoutes(server);
    }

    // System status route (return JSON)
    server.on("/status", HTTP_GET, [this]() {
        server.send(200, "application/json", WebStatus::getSystemStatus());
    });

    // Log route (for dynamic log updates)
    server.on("/log", HTTP_GET, [this]() {
        server.send(200, "text/plain", WebStatus::createLogText());
    });

    server.onNotFound([this]() {
        server.send(404, "text/plain", "Not Found");
    });
}


String BasicWebInterface::generateHeaderAndStatusHtml() const{
    String html = R"(
    <!DOCTYPE html>
    <html>
    <head>
    <meta charset="UTF-8">
    <title>ESP32 )" + systemID.systemName() + R"( Control</title>
    </head>
    <body>
    <h2>System Status</h2>
    )";

    html += WebStatus::createSystemStatHtmlFragment();
    return html;
}

String BasicWebInterface::generateDisplayHtml() const{
    String html;
    html.reserve(1024);
    for (const auto& display : displays_) {
        html += "<h2>" + display.first + "</h2>";
        html += display.second->createHtmlFragment();
    }
    return html;

}
String BasicWebInterface::generateSettingsHtml() const{
    String html;
    html.reserve(1024);
    for(const auto& settingsDisplay : settingsDisplays_) {
        html += "<h3>" + settingsDisplay.first + "</h3>";
        html += settingsDisplay.second->generateHTML();
    }
    return html;

}

String BasicWebInterface::generateHTML() const {
    String html = generateHeaderAndStatusHtml();

    html += generateDisplayHtml();
    html += generateSettingsHtml();
    html += generateFooterHtml();

    return html;
}