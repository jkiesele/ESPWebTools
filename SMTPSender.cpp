#include "SMTPSender.h"
#include <WiFi.h>
#include <base64.h>  // ESP32 built-in Base64 library

SMTPSender::SMTPSender(const char* fromName, const char* smtpUser, const char* smtpPass)
    : fromName(fromName), smtpUser(smtpUser), smtpPass(smtpPass) {
         //client.setBufferSizes(2048,2048); // Increase buffer size for large emails
    }

void SMTPSender::begin() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("⚠️ ERROR: WiFi not connected!");
    }
}

bool SMTPSender::sendEmail(const char* recipient, const char* subject, const String& message) {
    return sendEmail(recipient, subject, message.c_str());
}

bool SMTPSender::sendEmail(const char* recipient, const String& subject, const String& message) {
    return sendEmail(recipient, subject.c_str(), message);
}

bool SMTPSender::sendEmail(const char* recipient, const char* subject, const char* message) {
    Serial.println("📩 Connecting to SMTP server...");

    WiFiClientSecure client;
    client.setInsecure();  // Allow self-signed certificates

    if (!client.connect(smtpServer, smtpPort)) {
        Serial.println("❌ Connection to SMTP server failed.");
        return false;
    }
    Serial.println("✅ Connected to SMTP server.");

    readSMTPResponse(client);
    sendSMTPCommand("EHLO esp32.local",client);
    sendSMTPCommand("AUTH LOGIN",client);
    sendSMTPCommand(base64Encode(smtpUser),client);
    sendSMTPCommand(base64Encode(smtpPass),client);
    sendSMTPCommand("MAIL FROM:<" + smtpUser + ">",client);
    sendSMTPCommand("RCPT TO:<" + String(recipient) + ">",client);
    sendSMTPCommand("DATA",client);

    // ✅ Construct email headers properly
    String emailContent = "From: \"" + fromName + "\" <" + smtpUser + ">\r\n"; 
    emailContent += "To: " + String(recipient) + "\r\n";
    emailContent += "Subject: " + String(subject) + "\r\n";
    emailContent += "MIME-Version: 1.0\r\n";
    emailContent += "Content-Type: text/plain; charset=UTF-8\r\n";
    emailContent += "\r\n";  // Blank line separates headers from body
    emailContent += String(message) + "\r\n.\r\n";  // ermination of email

    sendSMTPCommand(emailContent,client);
    sendSMTPCommand("QUIT",client);
    
    client.stop();
    //client will be destroyed at the end

    Serial.println("✅ Email sent successfully!");
    return true;
}

void SMTPSender::sendSMTPCommand(const String& cmd, WiFiClientSecure & client) {
    client.println(cmd);
    readSMTPResponse(client);
}

String SMTPSender::readSMTPResponse(WiFiClientSecure & client) {
    String response = "";
        while (client.available()) {
            response += char(client.read());
        }
    return response;
}

// ✅ Proper Base64 Encoding
String SMTPSender::base64Encode(const String& input) {
    return base64::encode(input);
}
