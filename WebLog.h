
#ifndef _WEBLOG_H_
#define _WEBLOG_H_

#include "Arduino.h"
#include <vector>
#include "LoggingBase.h"


class WebLog : public LoggingBase{
public:
    WebLog(uint8_t logSize=10):logSize(logSize){ 
        setLogSize(logSize);
    }   
    ~WebLog(){}

    void setLogSize(uint8_t size){
        logSize = size;  
        logMessages.reserve(logSize);
        logTimestamps.reserve(logSize);
    }

    void begin();
    
    void addToLog(String message, bool newTimeStamp=true);
    //convenience
    void print(const String&  message){
        addToLog(message, nextEntryNewTimeStamp);
        nextEntryNewTimeStamp = false; //for the next entry
    }
    void println(const String&  message){
        addToLog(message, nextEntryNewTimeStamp);
        nextEntryNewTimeStamp = true; //for the next entry
    }

    const std::vector<String>& getLogMessages(){
        return logMessages;
    }
    const std::vector<uint32_t>& getLogTimestamps(){
        return logTimestamps;
    }
    uint8_t getLogSize() const {
        return logSize;
    }
    bool mirrorToSerial = false;
    bool turnedOn = true;
    void turnOn(){turnedOn = true;}
    void turnOff(){turnedOn = false;}
    
    
private:
    std::vector<String> logMessages;
    std::vector<uint32_t> logTimestamps;
    uint8_t logSize;
    bool nextEntryNewTimeStamp = true; //for the next entry
};

extern WebLog webLog; //global instance

#endif // _WEBLOG_H_