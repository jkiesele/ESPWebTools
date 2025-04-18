#ifndef SMTP_SENDER_H
#define SMTP_SENDER_H

#include <WiFiClientSecure.h>
#include <Arduino.h>

class SMTPSender {
private:
    static constexpr const char* smtpServer = "mail.smtp2go.com";
    static constexpr int smtpPort = 465;  // Use 465 for SSL, 587 for TLS
    
    String smtpUser;
    String smtpPass;
    String fromName;

    static void sendSMTPCommand(const String& cmd, WiFiClientSecure & client);
    static String readSMTPResponse(WiFiClientSecure & client);
    static String base64Encode(const String& input);


public:
    SMTPSender(const char* fromName, const char* smtpUser, const char* smtpPass);
    void begin();
    bool sendEmail(const char* recipient, const char* subject, const char* message);
    bool sendEmail(const char* recipient, const char* subject, const String& message);
    bool sendEmail(const char* recipient, const String& subject, const String& message);
};

#endif