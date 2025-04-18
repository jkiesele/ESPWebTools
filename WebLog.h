
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

    void setLogSize(uint8_t logSize){
        logSize = logSize;  
        logMessages.reserve(logSize);
        logTimestamps.reserve(logSize);
    }

    void begin();
    
    void addToLog(String message);
    //convenience
    void print(const String&  message){addToLog(message);}
    void println(const String&  message){addToLog(message);}

    const std::vector<String>& getLogMessages(){
        return logMessages;
    }
    const std::vector<uint32_t>& getLogTimestamps(){
        return logTimestamps;
    }
    bool mirrorToSerial = false;
    bool turnedOn = true;
    void turnOn(){turnedOn = true;}
    void turnOff(){turnedOn = false;}
    
    
private:
    std::vector<String> logMessages;
    std::vector<uint32_t> logTimestamps;
    uint8_t logSize;
};

extern WebLog webLog; //global instance

#endif // _WEBLOG_H_