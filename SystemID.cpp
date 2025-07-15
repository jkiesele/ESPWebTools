#include <Preferences.h>
#include "SystemID.h"

SystemID systemID;
void SystemID::begin() {
    loadFromNVS();
}
String SystemID::systemName() const {
    return systemName_;
}
void SystemID::setSystemName(const String& name) {
    systemName_ = name;
    saveToNVS();
}
void SystemID::loadFromNVS() {
    Preferences prefs;
    prefs.begin("system", false);
    systemName_ = prefs.getString("system_name", "no_name");
    prefs.end();
}
void SystemID::saveToNVS() {
    // save system name to NVS
    Preferences prefs;
    prefs.begin("system", false);
    prefs.putString("system_name", systemName_);
    prefs.end();
}
