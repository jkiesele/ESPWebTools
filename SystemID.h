
#ifndef SYSTEM_ID_H
#define SYSTEM_ID_H
#include <Arduino.h>
#include <Preferences.h> // Include Preferences library for NVS

class SystemID {
public:
    SystemID() {
    }
    void begin();
    // Get the system name
    String systemName() const;
    // Set a new system name and save it to NVS
    void setSystemName(const String& name);
private:
    String systemName_;
    void loadFromNVS();
    void saveToNVS();
} ;

extern SystemID systemID; // Declare a global instance of SystemID

#endif