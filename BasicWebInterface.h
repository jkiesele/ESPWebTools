#include <Arduino.h>
#include <WebServer.h>
#include <WebDisplay.h>
#include <WebSettings.h>
#include <WebItem.h>

#ifndef BASICWEBINTERFACE_H
#define BASICWEBINTERFACE_H


class BasicWebInterface {
public:

    BasicWebInterface(){}
    void begin(bool authEnabled = true, bool postOnlyLockdown = true);
    void loop(){
        server.handleClient();
    }

    void setupRoutes();
    // can override any of those to change the HTML output
    virtual String generateHTML() const;
    virtual String generateHeaderAndStatusHtml() const;
    virtual String generateDisplayHtml() const;
    virtual String generateSettingsHtml() const;
    virtual String generateWebItemsHtml() const;
    String generateFooterHtml() const {
        return "</body></html>";
    }

    void addDisplay(const String & descText, WebDisplayBase *display) {
        displays_.emplace_back(descText, display);
    }
    void addSettings(const String & descText, SettingsBlockBase *settings) {
        settingsDisplays_.emplace_back(descText, settings);
    }
    void addWebItem(WebItem* item) {
        webItems_.push_back(item);
    }

    WebServer& getServer() { return server; }
    const WebServer& getServer() const { return server; }

private:
    WebServer server;
    std::vector<std::pair<String, WebDisplayBase*>> displays_;
    std::vector<std::pair<String, SettingsBlockBase*>> settingsDisplays_;
    std::vector<WebItem*> webItems_;

};
#endif