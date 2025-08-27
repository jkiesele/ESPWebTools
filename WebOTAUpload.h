#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Update.h>
#include <LoggingBase.h>   // uses global gLogger
#include <esp_ota_ops.h>
#include <WebItem.h> //provides virtual access to setupRoutes and generateHTML (can be added to BasicWebInterface)

// Provide a password validator hook so you can read from NVS/SettingsBlockBase.
using OtaPasswordValidator = std::function<bool(const String& pw)>;


class WebOtaUpload : public WebItem {
public:
    // Fixed password (simple)
    WebOtaUpload(const String& password,
                 const String& route = "/otaupdate")
        : route_(route)
        , validator_([password](const String& pw){ return pw == password; })
    {}

    // Dynamic validator (e.g. read from settings)
    WebOtaUpload(OtaPasswordValidator validator,
                 const String& route = "/otaupdate")
        : route_(route)
        , validator_(std::move(validator))
    {}

    void setupRoutes(WebServer& server) {
        server_ = &server;

        // GET: show upload page
        server.on(route_.c_str(), HTTP_GET, [this]() {
            server_->send(200, "text/html", buildPage_());
        });

        // POST: finalize (called after all chunks processed)
        server.on(route_.c_str(), HTTP_POST,
            [this]() {
                // Validate password (non-empty & correct)
                const String pw = server_->arg("password");
                const bool pwOk = (pw.length() > 0) && validator_(pw);

                // If password is wrong, ensure any started Update is aborted
                if (!pwOk) {
                    if (uploadStarted_) {
                        Update.abort();
                        uploadStarted_ = false;
                    }
                    server_->send(403, "text/plain", "Forbidden: wrong password.");
                    gLogger->println(F("[OTA] Denied: wrong password"));
                    return;
                }

                // Password OK — check if update succeeded
                const bool ok = uploadStarted_ && !Update.hasError();
                server_->send(ok ? 200 : 500, "text/plain",
                              ok ? "Update Success. Rebooting..." : "Update Failed!");
                gLogger->println(ok ? F("[OTA] Update success") : F("[OTA] Update failed"));
                delay(200);
                if (ok) ESP.restart();

                // Reset state for next cycle
                uploadStarted_ = false;
            },
            // Upload stream handler (receives file chunks)
            [this]() {
                HTTPUpload& up = server_->upload();

                switch (up.status) {
                    case UPLOAD_FILE_START: {
                        uploadStarted_ = false;

                        // IMPORTANT: We only start Update if password is already present & correct.
                        // WebServer usually parses form args by now; if not, we will still deny in POST.
                        const String pw = server_->arg("password");
                        const bool pwOk = (pw.length() > 0) && validator_(pw);
                        if (!pwOk) {
                            gLogger->println(F("[OTA] Upload started with wrong/missing password; discarding data"));
                            // Do NOT call Update.begin — we’ll just consume chunks.
                            break;
                        }

                        if (!up.filename.endsWith(".bin")) {
                            gLogger->println(F("[OTA] Reject: only .bin allowed"));
                            break;
                        }

                        gLogger->print(F("[OTA] Start: "));
                        gLogger->println(up.filename);

                        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                            gLogger->println(Update.errorString());
                            break;
                        }
                        uploadStarted_ = true;
                        break;
                    }

                    case UPLOAD_FILE_WRITE: {
                        if (!uploadStarted_) break; // discard if not authorized / not started
                        size_t w = Update.write(up.buf, up.currentSize);
                        if (w != up.currentSize) {
                            gLogger->println(Update.errorString());
                        }
                        break;
                    }

                    case UPLOAD_FILE_END: {
                        if (!uploadStarted_) break; // nothing to end
                        if (Update.end(true)) {
                            gLogger->print(F("[OTA] Written bytes: "));
                            gLogger->println(String(up.totalSize));
                        } else {
                            gLogger->println(Update.errorString());
                        }
                        break;
                    }

                    case UPLOAD_FILE_ABORTED: {
                        gLogger->println(F("[OTA] Aborted"));
                        if (uploadStarted_) {
                            Update.abort();
                            uploadStarted_ = false;
                        }
                        break;
                    }

                    default: break;
                }
            }
        );

        // Tiny status endpoint (optional)
        server.on((route_ + F("/status")).c_str(), HTTP_GET, [this]() {
            server_->send(200, "application/json", F("{\"ota\":\"ready\"}"));
        });
    }

    // Snippet you can place on a dashboard page
    String generateHTML() const {
        String s;
        s.reserve(256);
        s  = F("<div class='card'><h3>Firmware Update</h3>"
               "<p>Upload a compiled <code>.bin</code> image.</p>"
               "<a class='btn' href='");
        s += route_;
        s += F("'>Open OTA Page</a></div>");
        return s;
    }

    const String& route() const { return route_; }

    //call this after a verifying a successful update in begin() to prevent rollback
    void markAppValid() {
        esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
        gLogger->println(err == ESP_OK ? F("[OTA] Marked app valid (cancelled rollback).")
                                       : F("[OTA] Failed to mark app valid!"));
    }

private:
    WebServer* server_ = nullptr;
    String route_;
    OtaPasswordValidator validator_;
    bool uploadStarted_ = false;

    String buildPage_() const {
        // Minimalistic, no JS required; password field is mandatory.
        String s;
        s.reserve(1200);
        s = F(
            "<!doctype html><html><head><meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>OTA Update</title>"
            "<style>"
            "body{font-family:sans-serif;max-width:680px;margin:2rem auto;padding:0 1rem}"
            "h1{font-size:1.6rem}"
            ".box{border:1px solid #ccc;padding:1rem;border-radius:.5rem}"
            "label{display:block;margin:.5rem 0 .25rem}"
            "input[type=password],input[type=file]{width:100%}"
            "button{margin-top:1rem}"
            "</style></head><body>"
            "<h1>Firmware OTA</h1>"
            "<div class='box'>"
            "<form method='POST' action='");
        s += route_;
        s += F("' enctype='multipart/form-data'>"
               "<label>Password</label>"
               "<input type='password' name='password' required>"
               "<label>Firmware (.bin)</label>"
               "<input type='file' name='firmware' accept='.bin' required>"
               "<button type='submit'>Upload & Update</button>"
               "</form>"
               "<p><small>The device will reboot after a successful update.</small></p>"
               "</div></body></html>");
        return s;
    }
};