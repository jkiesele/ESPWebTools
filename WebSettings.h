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
#include "WebAuthPlugin.h"


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

    virtual void appendHTMLInputs(String& html) const = 0;
    virtual void onPost(WebServer& srv) = 0;
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

    void appendHTMLInputs(String& html) const override {
      if      (valueType == TYPE_BOOL) {
        html += "<input type='checkbox' name='" + String(key) + "' value='1' ";
        html += (value ? "checked " : "");
        html += ">\n";
      } else if (valueType == TYPE_STRING) {
        html += "<input type='text' name='" + String(key) + "' value='" + toString() + "'>\n";
      } else {
        html += "<input type='text' inputmode='decimal' name='" + String(key) + "' value='" + toString() + "'>\n";
      }
    }
    void onPost(WebServer& srv) override {
      if (srv.hasArg(key)) fromString(srv.arg(key));
      else if (valueType == TYPE_BOOL) value = false; // unchecked checkbox
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

    void appendHTMLInputs(String& html) const override {
       html += "<input type='text' name='" + String(key) + "' value='" + toString() + "'>\n";
    }
    void onPost(WebServer& srv) override {
      if (srv.hasArg(key)) fromString(srv.arg(key));
    }
};

/*------------------------------------------------------------*/
/* 4.  Generic “settings block”                               */
/*------------------------------------------------------------*/
class SettingsBlockBase {
public:
    SettingsBlockBase(const char* nvs, const char* url)
      : nvsNS(nvs), urlPath(url) {}

    //overload for arduino string
    SettingsBlockBase(const String& nvs, const String& url)
      : nvsNS(nvs.c_str()), urlPath(url.c_str()) {}

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
            if (WebAuthPlugin::instance().isActive()) {
                if (!WebAuthPlugin::instance().require()) return;   // uses postOnlyLockdown internally
            } else {
                if (!(srv.hasArg("pw") && srv.arg("pw") == kSettingsPassword)) {
                    gLogger->println("Settings: " + String(urlPath) + " update failed: wrong password");
                    srv.send(401, "text/html", "<h3>Wrong password</h3>");
                    return;
                }
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
            s->appendHTMLInputs(html);
            html += "<br>\n";
        }
        if (!WebAuthPlugin::instance().isActive()) {
            html += "Password: <input type='password' name='pw'><br><br>\n";
        }
        html += "<input type='submit' value='Save'></form>\n";
        return html;
    }

    //password as static
    static String kSettingsPassword;

private:
    void handlePost(WebServer& srv)
    {
        for (auto* s : registry) s->onPost(srv);
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

/// Array types
/***************  Dynamic Array Support  *********************/
/* Primary IO template (must come before specializations) */
template<typename T, typename Enable = void>
struct SettingArrayIO {};

/* floats */
template<typename T>
struct SettingArrayIO<T, typename std::enable_if<std::is_floating_point<T>::value>::type> {
  static T load(Preferences& p, const char* k, T def) { return (T)p.getFloat(k, (float)def); }
  static void save(Preferences& p, const char* k, T v){ p.putFloat(k, (float)v); }
  static String toStr(const T& v, unsigned precision){ return String((float)v, precision); }
  static T fromStr(const String& s){ return (T)s.toFloat(); }
  static void appendInput(String& html, const String& name, const String& val, bool /*checked*/) {
    html += "<input type='text' inputmode='decimal' name='" + name + "' value='" + val + "'>";
  }
};

/* integral (except bool) */
template<typename T>
struct SettingArrayIO<T, typename std::enable_if<std::is_integral<T>::value && !std::is_same<T,bool>::value>::type> {
  static T load(Preferences& p, const char* k, T def) { return (T)p.getInt(k, (int)def); }
  static void save(Preferences& p, const char* k, T v){ p.putInt(k, (int)v); }
  static String toStr(const T& v, unsigned){ return String((int)v); }
  static T fromStr(const String& s){ return (T)s.toInt(); }
  static void appendInput(String& html, const String& name, const String& val, bool /*checked*/) {
    html += "<input type='text' inputmode='decimal' name='" + name + "' value='" + val + "'>";
  }
};

/* bool */
template<typename T>
struct SettingArrayIO<T, typename std::enable_if<std::is_same<T,bool>::value>::type> {
  static T load(Preferences& p, const char* k, T def) { return p.getBool(k, (bool)def); }
  static void save(Preferences& p, const char* k, T v){ p.putBool(k, (bool)v); }
  static String toStr(const T& v, unsigned){ return v ? "1" : "0"; }
  static T fromStr(const String& s){ return (s == "1" || s == "true" || s == "on"); }
  static void appendInput(String& html, const String& name, const String& /*val*/, bool checked) {
    html += "<input type='checkbox' name='" + name + "' value='1' ";
    html += (checked ? "checked " : "");
    html += ">";
  }
};

/* Arduino String */
template<typename T>
struct SettingArrayIO<T, typename std::enable_if<std::is_same<T, String>::value>::type> {
  static T load(Preferences& p, const char* k, const T& def) { return p.getString(k, def); }
  static void save(Preferences& p, const char* k, const T& v){ p.putString(k, v); }
  static String toStr(const T& v, unsigned){ return v; }
  static T fromStr(const String& s){ return s; }
  static void appendInput(String& html, const String& name, const String& val, bool /*checked*/) {
    html += "<input type='text' name='" + name + "' value='" + htmlEscape_(val) + "'>";
  }
private:
  static String htmlEscape_(const String& s) {
    String out; out.reserve(s.length()+8);
    for (size_t i=0;i<s.length();++i) {
      char c=s[i];
      if (c=='&') out += "&amp;";
      else if (c=='<') out += "&lt;";
      else if (c=='>') out += "&gt;";
      else if (c=='"') out += "&quot;";
      else out += c;
    }
    return out;
  }
};


/***************  SettingDynArray<T>  ************************/

template<typename T>
class SettingDynArray : public SettingBase {
public:
  std::vector<T> value;

  /* ctor: fixed length + fill default */
  template<class Block>
  SettingDynArray(Block& owner,
                  const char* k,
                  const char* lbl,
                  size_t len,
                  T fillDefault,
                  float st = 0.01f)
  {
    key   = k;
    label = lbl;
    step  = st;
    setPrecisionFromStep(step);
    setValueType_();
    value.assign(len, fillDefault);
    owner.registerSetting(this);
    default_ = fillDefault;  // remember for resizing
  }

  /* ctor: explicit defaults (initializer list) */
  template<class Block>
  SettingDynArray(Block& owner,
                  const char* k,
                  const char* lbl,
                  std::initializer_list<T> initVals,
                  float st = 0.01f)
  {
    key   = k;
    label = lbl;
    step  = st;
    setPrecisionFromStep(step);
    setValueType_();
    value.assign(initVals.begin(), initVals.end());
    owner.registerSetting(this);
    
  }

  void ensureSize(size_t len) {
    if (value.size() != len){
        value.assign(len, default_);
        gLogger->println("Settings: array " + String(key) + " resized to " + String((unsigned)len));
    } 
  }
  /*access to elements */
  T& operator[](size_t i) {if (i < value.size()) return value[i]; gLogger->println("Settings: array index out of range for " + String(key)); return default_; }
  const T& operator[](size_t i) const { if (i < value.size()) return value[i]; gLogger->println("Settings: array index out of range for " + String(key)); return default_; }

  /* SettingBase interface not used for arrays as a whole */
  void   fromString(const String&) override {}
  String toString() const override { return String(); }

  /* NVS I/O: one key per element: key_0, key_1, ... */
  void load(Preferences& p) override {
    const size_t N = value.size();
    for (size_t i = 0; i < N; ++i) {
      String ki = String(key) + "_" + String((unsigned)i);
      value[i] = SettingArrayIO<T>::load(p, ki.c_str(), value[i]);
    }
  }
  void save(Preferences& p) const override {
    const size_t N = value.size();
    for (size_t i = 0; i < N; ++i) {
      String ki = String(key) + "_" + String((unsigned)i);
      SettingArrayIO<T>::save(p, ki.c_str(), value[i]);
    }
  }

  /* HTML rendering */
  void appendHTMLInputs(String& html) const override {
    const size_t N = value.size();
    html += "<span>";
    for (size_t i = 0; i < N; ++i) {
      const String fname = String(key) + "_" + String((unsigned)i);
      const String sval  = SettingArrayIO<T>::toStr(value[i], precision);
      SettingArrayIO<T>::appendInput(html, fname, sval, checked_(i));
      html += " ";
    }
    html += "</span>";
  }

  /* POST handling */
  void onPost(WebServer& srv) override {
    const size_t N = value.size();
    for (size_t i = 0; i < N; ++i) {
      String fname = String(key) + "_" + String((unsigned)i);
      if (srv.hasArg(fname)) {
        value[i] = SettingArrayIO<T>::fromStr(srv.arg(fname));
      } else {
        setMissing_(i); // only flips to false for bool; no-op for others
      }
    }
  }

private:
  T default_;
  void setValueType_() {
    if      (std::is_same<T,bool>::value)    valueType = TYPE_BOOL;
    else if (std::is_same<T, String>::value) valueType = TYPE_STRING;
    else if (std::is_integral<T>::value)     valueType = TYPE_INT;
    else                                     valueType = TYPE_FLOAT;
  }

  /* SFINAE: checkbox state only for bool */
  template<typename U=T>
  typename std::enable_if<std::is_same<U,bool>::value, bool>::type
  checked_(size_t i) const { return (bool)value[i]; }

  template<typename U=T>
  typename std::enable_if<!std::is_same<U,bool>::value, bool>::type
  checked_(size_t) const { return false; }

  /* SFINAE: set false on missing POST only for bool */
  template<typename U=T>
  typename std::enable_if<std::is_same<U,bool>::value, void>::type
  setMissing_(size_t i) { value[i] = false; }

  template<typename U=T>
  typename std::enable_if<!std::is_same<U,bool>::value, void>::type
  setMissing_(size_t) { /* no-op */ }
};

#define DEF_SETTING_ARRAY(TYPE, NAME, LABEL, DEFAULT_VAL, STEP, LENGTH) \
  SettingDynArray<TYPE> NAME{ *this, #NAME, LABEL, (size_t)(LENGTH), (TYPE)(DEFAULT_VAL), STEP }

#define DEF_SETTING_ARRAY_INIT(TYPE, NAME, LABEL, STEP, ...) \
  SettingDynArray<TYPE> NAME{ *this, #NAME, LABEL, std::initializer_list<TYPE>{ __VA_ARGS__ }, STEP }
