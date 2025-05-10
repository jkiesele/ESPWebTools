
#ifndef _WEBLOG_H_
#define _WEBLOG_H_

#include "Arduino.h"
#include <vector>
#include "LoggingBase.h"
#include <atomic>


class WebLog : public LoggingBase{
public:
    WebLog(uint8_t logSize=10):logSize(logSize){ 
        accessMutex = xSemaphoreCreateMutex();
        if(accessMutex == nullptr){
            //this will necessarily fall back to the previous gLogger
            gLogger->println("WebLog: Failed to create mutex");
            return;
        }
        setLogSize(logSize);
    }   
    ~WebLog(){}

    void setLogSize(uint8_t size){
        if (!accessMutex) {
            logSize = size;
            return;
        }
        xSemaphoreTake(accessMutex, portMAX_DELAY);
        logSize = size;  
        logMessages.reserve(logSize);
        logTimestamps.reserve(logSize);
        xSemaphoreGive(accessMutex);
    }

    void begin(){/*empty*/}//for compatability with other loggers
    
    //convenience
    void print(const String&  message){
        xSemaphoreTake(accessMutex, portMAX_DELAY);
        addToLog(message, nextEntryNewTimeStamp);
        nextEntryNewTimeStamp = false; //for the next entry
        xSemaphoreGive(accessMutex);
    }
    void println(const String&  message){
        xSemaphoreTake(accessMutex, portMAX_DELAY);
        addToLog(message, nextEntryNewTimeStamp);
        nextEntryNewTimeStamp = true; //for the next entry
        xSemaphoreGive(accessMutex);
    }

    std::vector<String> getLogMessages(){
        xSemaphoreTake(accessMutex, portMAX_DELAY);
        auto cp = logMessages;
        xSemaphoreGive(accessMutex);
        return cp;
    }
    std::vector<uint32_t> getLogTimestamps(){
        xSemaphoreTake(accessMutex, portMAX_DELAY);
        auto cp = logTimestamps;
        xSemaphoreGive(accessMutex);
        return cp;
    }
    uint8_t getLogSize() const {
        xSemaphoreTake(accessMutex, portMAX_DELAY);
        uint8_t sz = logSize;
        xSemaphoreGive(accessMutex);
        return sz;
    }
    std::atomic<bool> mirrorToSerial{false};
    std::atomic<bool> turnedOn{true};
    void turnOn(){turnedOn = true;}
    void turnOff(){turnedOn = false;}
    
    
private:
    SemaphoreHandle_t accessMutex{nullptr};
    
    //needs to be called with the mutex held
    void addToLog(String message, bool newTimeStamp=true);

    std::vector<String> logMessages;
    std::vector<uint32_t> logTimestamps;
    uint8_t logSize;
    bool nextEntryNewTimeStamp{true}; //for the next entry
};

extern WebLog webLog; //global instance

#endif // _WEBLOG_H_