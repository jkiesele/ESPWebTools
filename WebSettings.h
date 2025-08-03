/* ===========================================================
   settings_framework_sync_cpp11_no_rtti.hpp

   C++11 only – no RTTI, no dynamic_cast.
   Drop into your /src and `#include` it.

   Requires:
     #include <WebServer.h>      (ESP32 / ESP8266)
     #include <Preferences.h>
   =========================================================== */
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WebServer.h>
#include <vector>
#include <type_traits>
#include <LoggingBase.h>


/*------------------------------------------------------------*/
/* 2.  Abstract base for every setting                        */
/*------------------------------------------------------------*/
struct SettingBase {
    enum ValueType {
        TYPE_FLOAT,
        TYPE_INT,
        TYPE_BOOL,
        TYPE_STRING         
    } valueType;
    const char* key;      // HTML name= / NVS key
    const char* label;    // human text
    float       step;     // HTML step (ignored for bool)
    unsigned int precision = 2;   // HTML precision (ignored for bool)

    SettingBase(): valueType(TYPE_FLOAT), key(nullptr), label(nullptr), step(0.01f) {}
    virtual ~SettingBase() {}

    virtual void   fromString(const String& raw)         = 0;
    virtual String toString()                      const = 0;
    virtual void   load(Preferences&)                    = 0;
    virtual void   save(Preferences&)              const = 0;

    void setPrecisionFromStep(float step) {
        const float eps = 1e-6f;  // tolerance for float rounding
        float x = step;
        unsigned int prec = 0;
        // loop up to, say, 9 decimal places
        while (prec < 9 && fabsf(x - roundf(x)) > eps) {
            x *= 10.0f;
            prec++;
        }
        precision = prec;
    }
};

template<typename T>
class Setting : public SettingBase {
public:
    T value;

    template<class Block>
    Setting(Block& owner,
            const char* k,
            const char* lbl,
            T defaultVal,
            float st = 0.01f)
        : value(defaultVal)
    {
        key   = k;
        label = lbl;
        step  = st;

        if      (std::is_same<T,bool>::value) valueType = TYPE_BOOL;
        else if (std::is_integral<T>::value)  valueType = TYPE_INT;
        else                                  valueType = TYPE_FLOAT;

        setPrecisionFromStep(step);
        owner.registerSetting(this);
    }

    /* implicit cast */
    operator T() const      { return value; }
    Setting& operator=(T v) { value = v; return *this; }

    /* conversions --------------------------------------------------*/
    void fromString(const String& raw) override {
        if      (valueType == TYPE_BOOL) value = (raw == "1" || raw == "true" || raw == "on");
        else if (valueType == TYPE_INT)  value = static_cast<T>(raw.toInt());
        else                             value = static_cast<T>(raw.toFloat());
    }

    String toString() const override {
        if      (valueType == TYPE_BOOL)  return value ? "1" : "0";
        else if (valueType == TYPE_FLOAT) return String(static_cast<float>(value), precision);
        else                              return String(value);          // int
    }

    void load(Preferences& p) override {
        if      (valueType == TYPE_BOOL) value = p.getBool(key, value);
        else if (valueType == TYPE_INT)  value = static_cast<T>(p.getInt(key, (int)value));
        else                             value = static_cast<T>(p.getFloat(key, value));
    }

    void save(Preferences& p) const override {
        if      (valueType == TYPE_BOOL) p.putBool (key, value);
        else if (valueType == TYPE_INT)  p.putInt  (key, (int)value);
        else                             p.putFloat(key, value);
    }
};

/*------------------------------------------------------------*/
/* 3b. Full specialization for Arduino String                 */
/*------------------------------------------------------------*/
template<>
class Setting<String> : public SettingBase {
public:
    String value;

    template<class Block>
    Setting(Block& owner,
            const char* k,
            const char* lbl,
            const String& defaultVal = String(),
            float /*st*/ = 1.0f)
        : value(defaultVal)
    {
        key        = k;
        label      = lbl;
        step       = 1.0f;          // not used
        precision  = 0;
        valueType  = TYPE_STRING;
        owner.registerSetting(this);
    }

    operator String() const        { return value; }
    Setting& operator=(const String& v) { value = v; return *this; }

    /* conversions --------------------------------------------------*/
    void fromString(const String& raw) override { value = raw; }
    String toString() const override            { return value; }

    void load(Preferences& p) override {
        value = p.getString(key, value);
    }
    void save(Preferences& p) const override {
        p.putString(key, value);
    }
};

/*------------------------------------------------------------*/
/* 4.  Generic “settings block”                               */
/*------------------------------------------------------------*/
class SettingsBlockBase {
public:
    SettingsBlockBase(const char* nvs, const char* url)
      : nvsNS(nvs), urlPath(url) {}

    /* lifecycle */
    void begin() { prefs.begin(nvsNS); load(); }
    void load()  { for (auto* s : registry) s->load(prefs); }
    void save()  { for (auto* s : registry) s->save(prefs); }

    /* expose this so Setting<T> can call it */
    void registerSetting(SettingBase* s) { registry.push_back(s); }

    virtual bool sanityCheck() {return true;} // override in derived classes if needed, can change values

    /* web integration */
    void setupRoutes(WebServer& srv)
    {
        /* GET -> form */
        srv.on(urlPath, HTTP_GET, [this, &srv](){
            srv.send(200, "text/html", generateHTML());
        });

        /* POST -> check pw, update, save */
        String postPath = String(urlPath) + "/update";
        srv.on(postPath.c_str(), HTTP_POST, [this, &srv](){
            if (!(srv.hasArg("pw") &&
                  srv.arg("pw") == kSettingsPassword)) {
                gLogger->println("Settings: " + String(urlPath)+ "update failed: wrong password");
                srv.send(401, "text/html", "<h3>Wrong password</h3>");
                return;
            }
            handlePost(srv);
            save();
            gLogger->println("Settings: " + String(urlPath) + " updated");
            srv.sendHeader("Location", "/");  
            srv.send(303);   
        });
    }

    /* public so you can embed it in your own pages */
    String generateHTML() const {
        String html;
        html.reserve(1024);
        html  = "<form method='POST' action='";
        html += String(urlPath) + "/update'>\n";
    
        for (auto* s : registry) {
            html += String(s->label) + ": ";
            if (s->valueType == SettingBase::TYPE_BOOL) {
                auto* b = static_cast<Setting<bool>*>(s);
                html += "<input type='checkbox' name='" + String(s->key) +
                        "' value='1' " + (b->value ? "checked " : "") +
                        "><br>\n";
            } else if (s->valueType == SettingBase::TYPE_STRING) {          // NEW
                html += "<input type='text' name='" + String(s->key) +
                        "' value='" + s->toString() + "'><br>\n";
            } else {
                html += "<input type='text' inputmode='decimal' name='" +
                        String(s->key) + "' value='" + s->toString() +
                        "'><br>\n";
            }
        }
    
        html += "Password: <input type='password' name='pw'><br><br>\n";
        html += "<input type='submit' value='Save'></form>\n";
        return html;
    }

    //password as static
    static String kSettingsPassword;

private:
    void handlePost(WebServer& srv)
    {
        for (auto* s : registry) {
            if (srv.hasArg(s->key)) {
                s->fromString(srv.arg(s->key));
            } else if (s->valueType == SettingBase::TYPE_BOOL) {
                // unchecked checkbox → false
                static_cast<Setting<bool>*>(s)->value = false;
            }
        }
        if (!sanityCheck()) {
            gLogger->println("Settings: " + String(urlPath) + " sanity check failed (values might have been changed)");
            return;
        }
    }

    /* data */
    const char*               nvsNS;
    const char*               urlPath;
    Preferences               prefs;
    std::vector<SettingBase*> registry;
};

/*------------------------------------------------------------*/
/* 5.  One-liner setting declarations                         */
/*------------------------------------------------------------*/
#define DEF_SETTING(TYPE, NAME, LABEL, DEFAULT_VAL, STEP) \
    Setting<TYPE> NAME{ *this, #NAME, LABEL, DEFAULT_VAL, STEP }

/* ===========================================================
   6.  Example usage                                         *
   ===========================================================*/
/*
class HydroSettings : public SettingsBlockBase {
public:
    HydroSettings() : SettingsBlockBase("hydro", "/hydro") {}

    DEF_SETTING(float, pumpA_relVol,
                "Parts nutrient pump A", 1.0f, 0.01f);
    DEF_SETTING(float, mlPerDeltaEC,
                "Nutrients per ΔEC [ml/mS]", 0.01f, 0.01f);
    DEF_SETTING(float, targetEC,
                "Target EC [mS]", 1.20f, 0.01f);
    DEF_SETTING(bool,  dryRun,
                "Dry-run mode", true, 1);
};

class LightingSettings : public SettingsBlockBase {
public:
    LightingSettings() : SettingsBlockBase("light", "/light") {}

    DEF_SETTING(int,   dayLength,
                "Day length [h]", 16, 1);
    DEF_SETTING(float, dawnFade,
                "Dawn fade [min]", 30.0f, 0.1f);
};
*/