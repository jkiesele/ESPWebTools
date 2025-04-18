
#include "WebLog.h"

WebLog webLog; //global instance

void WebLog::begin(TimeManager* timemanager){
    logMessages.clear();
    logTimestamps.clear();
    this->timemanager = timemanager;
}

void WebLog::addToLog(String message){
    if(!turnedOn){
        return;
    }
    //FIFO max 10
    if(logMessages.size() >= 10){
        logMessages.erase(logMessages.begin());
        logTimestamps.erase(logTimestamps.begin());
    }
    uint32_t timestamp = 0;
    if(timemanager){
        timestamp = timemanager->getUnixTime();
    }
    logMessages.push_back(message);
    logTimestamps.push_back(timestamp);
    if(mirrorToSerial){
        Serial.println(message);
    }
}