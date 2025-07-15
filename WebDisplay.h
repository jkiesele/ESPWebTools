#ifndef WEBDISPLAY_H
#define WEBDISPLAY_H

#include <Arduino.h>

// -----------------------------------------------------------------------------
//  Minimal base class
// -----------------------------------------------------------------------------
class WebDisplayBase {
public:
    WebDisplayBase(const String &id, uint32_t updateIntervalSecs)
        : id_(id), updateInterval_(updateIntervalSecs) {}

    virtual ~WebDisplayBase() {}

    /** HTML/JS snippet to embed in the client page */
    virtual String createHtmlFragment() const = 0;

    /** Payload your HTTP handler returns (e.g. JSON) */
    virtual String routeText() const = 0;

    // helpers --------------------------------------------------------------
    const char *id() const { return id_.c_str(); }
    const String &idStr() const { return id_; }
    uint32_t updateInterval() const { return updateInterval_; }

protected:
    String   id_;
    uint32_t updateInterval_;
};

// -----------------------------------------------------------------------------
//  Utilities – tiny JSON escaping and type-agnostic helpers
// -----------------------------------------------------------------------------
inline String jsonEscape(const String &in) {
    String out;
    out.reserve(in.length() + 4);
    for (size_t i = 0; i < in.length(); ++i) {
        char c = in[i];
        switch (c) {
            case '"': out += F("\\\""); break;
            case '\\': out += F("\\\\"); break;
            case '\n': out += F("\\n");  break;
            case '\r': out += F("\\r");  break;
            case '\t': out += F("\\t");  break;
            default:   out += c;             break;
        }
    }
    return out;
}

// Generic fallback – numeric types and everything convertible to Arduino String
template <typename U>
struct _JsonHelper {
    static String encode(const U &v) { return String(v); }          // -> "42"
    static String dom  (const U &v) { return String(v); }          // -> 42
};
// Specialisation for Arduino String (quoted in JSON, not in DOM)
template <>
struct _JsonHelper<String> {
    static String encode(const String &v) { return String('"') + jsonEscape(v) + '"'; }
    static String dom  (const String &v) { return v; }
};

// -----------------------------------------------------------------------------
//  Concrete templated display – stores any value convertible to String
// -----------------------------------------------------------------------------
template <typename T>
class WebDisplay : public WebDisplayBase {
public:
    WebDisplay(const String &id, uint32_t updateIntervalSecs, const T &initial = T())
        : WebDisplayBase(id, updateIntervalSecs), value_(initial) {}

    // Firmware code calls this to push new data
    void update(const T &v) { value_ = v; }

    // ------------------------------------------------------ WebDisplayBase ----
    String routeText() const override {
        String json;
        json.reserve(id_.length() + 32);
        json += F("{\"id\":\"");
        json += id_;
        json += F("\",\"value\":");
        json += _JsonHelper<T>::encode(value_);
        json += '}';
        return json;
    }

    String createHtmlFragment() const override {
        String html;
        html.reserve(200);
        html += "<span id=\"";  html += id_;  html += "\">";
        html += _JsonHelper<T>::dom(value_);
        html += "</span>\n<script>\n(function(){\n  const el=document.getElementById('"; html += id_; html += "');\n  async function poll(){\n    try{const r=await fetch('/"; html += id_; html += "');\n        if(r.ok){const d=await r.json(); el.textContent=d.value;}}catch(e){}\n  }\n  poll();\n  setInterval(poll,"; html += String(updateInterval_ * 1000); html += ");\n})();\n</script>\n";
        return html;
    }

private:
    T value_;
};

#endif // WEBDISPLAY_H