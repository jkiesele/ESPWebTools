#include <Arduino.h>
class WebServer;

// base class for web items (displays, settings blocks, buttons, etc) that defines the setupRoutes(server) and the generateHTML
class WebItem {
public:
    virtual void setupRoutes(WebServer& server) = 0;
    virtual String generateHTML() const = 0;
};
