
#include "WebLog.h"
#include <TimeProviderBase.h>

WebLog webLog; //global instance

void WebLog::begin(){
    logMessages.clear();
    logTimestamps.clear();
}

void WebLog::addToLog(String message){
    if(!turnedOn){
        return;
    }
    //FIFO max 10
    if(logMessages.size() >= logSize){
        logMessages.erase(logMessages.begin());
        logTimestamps.erase(logTimestamps.begin());
    }
    uint32_t timestamp = 0;
    if(gTimeProvider){
        timestamp = gTimeProvider->getUnixTime();
    }
    logMessages.push_back(message);
    logTimestamps.push_back(timestamp);
    if(mirrorToSerial){
        Serial.println(message);
    }
}