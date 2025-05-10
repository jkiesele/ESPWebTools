
#include "WebLog.h"
#include <TimeProviderBase.h>

WebLog webLog; //global instance

void WebLog::addToLog(String message, bool newTimeStamp){
    if(!turnedOn){
        return;
    }

    if(! newTimeStamp){//append to the last entry
        if(logMessages.size() > 0){
            logMessages.back() += message;
            if(mirrorToSerial)
                Serial.print(message);
            return;
        }
    }

    //FIFO max 
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