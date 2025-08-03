#include <Arduino.h>
#include <WebServer.h>
#include <WebDisplay.h>
#include <WebSettings.h>

#ifndef WEBINTERFACE_H
#define WEBINTERFACE_H

class BasicWebInterface {
public:

    BasicWebInterface(){}
    void begin();
    void loop(){
        server.handleClient();
    }

    void setupRoutes();
    // can override any of those to change the HTML output
    virtual String generateHTML() const;
    virtual String generateHeaderAndStatusHtml() const;
    virtual String generateDisplayHtml() const;
    virtual String generateSettingsHtml() const;
    String generateFooterHtml() const {
        return "</body></html>";
    }

    void addDisplay(const String & descText, WebDisplayBase *display) {
        displays_.emplace_back(descText, display);
    }
    void addSettings(const String & descText, SettingsBlockBase *settings) {
        settingsDisplays_.emplace_back(descText, settings);
    }


private:
    WebServer server;
    std::vector<std::pair<String, WebDisplayBase*>> displays_;
    std::vector<std::pair<String, SettingsBlockBase*>> settingsDisplays_;

};
#endif