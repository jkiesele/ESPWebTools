
#ifndef _WEBLOG_H_
#define _WEBLOG_H_

#include "Arduino.h"
#include <vector>
#include "LoggingBase.h"

#ifdef WEBLOG_USES_TIMEMANAGER
#include "TimeManager.h"
#else //MOCK class
class TimeManager {
public:
    uint32_t getUnixTime() const { return millis(); } // Mock implementation
};
#endif

class WebLog : public LoggingBase{
public:
    WebLog(){ 
        logMessages.reserve(10);//only last ten messages
        logTimestamps.reserve(10);
        timemanager = 0;
    }   
    ~WebLog(){}

    void begin(TimeManager* timemanager=0);
    
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
    TimeManager * timemanager;
};

extern WebLog webLog; //global instance

#endif // _WEBLOG_H_