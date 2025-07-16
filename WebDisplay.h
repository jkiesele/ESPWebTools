#ifndef WEBDISPLAY_H
#define WEBDISPLAY_H

#include <Arduino.h>
#include <LoggingBase.h> //DEBUG
// -----------------------------------------------------------------------------
//  Minimal base class
// -----------------------------------------------------------------------------
class WebDisplayBase {
public:
    WebDisplayBase(const String &id, uint32_t updateIntervalSecs)
        : id_(id), path_('/' + id), updateInterval_(updateIntervalSecs) {}

    virtual ~WebDisplayBase() {}

    /** HTML/JS snippet to embed in the client page */
    virtual String createHtmlFragment() const = 0;

    /** Payload your HTTP handler returns (e.g. JSON) */
    virtual String routeText() const = 0;

    // helpers --------------------------------------------------------------
    const String & id() const { return id_; }
    String handle() const { return path_; } // for WebServer routing
    const String &idStr() const { return id_; }
    uint32_t updateInterval() const { return updateInterval_; }

protected:
    String id_;
    String path_;
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
        json += id();
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
        html += "</span>\n<script>\n(function(){\n  const el=document.getElementById('"; 
        html += id(); 
        html += "');\n  async function poll(){\n    try{const r=await fetch('"; 
        html += handle(); 
        html += "');\n        if(r.ok){const d=await r.json(); el.textContent=d.value;}}catch(e){}\n  }\n  poll();\n  setInterval(poll,"; 
        html += String(updateInterval_ * 1000); html += ");\n})();\n</script>\n";
        return html;
    }

private:
    T value_;
};


// -----------------------------------------------------------------------------
//  Progress-bar style display (0‒100 %)
// -----------------------------------------------------------------------------
template <typename T>
class WebBarDisplay : public WebDisplayBase {
public:
    WebBarDisplay(const String &id,
                  uint32_t      updateIntervalSecs,
                  const T      &maxVal = 100)
        : WebDisplayBase(id, updateIntervalSecs), value_(), maxVal_(maxVal) {}

    /* push a new percentage (0‒100) from firmware code */
    void update(const T &v) { value_ = v; }

    // -------------------------- WebDisplayBase overrides -----------------------
    String routeText() const override {

        String json;
        json.reserve(id_.length() + 24);
        json += F("{\"id\":\"");
        json += id();
        json += F("\",\"value\":");
        json += String(value_);
        json += '}';
        return json;
    }

    String createHtmlFragment() const override {
        String html;
        html.reserve(400);

        // --- static HTML ------------------------------------------------------
        html += "<div id=\""; html += id(); html += "_container\" "
                "style=\"position:relative;width:100%;height:24px;"
                "background:#ddd;border-radius:4px;overflow:hidden;\">\n";

        html += "  <div id=\""; html += id(); html += "_bar\" "
                "style=\"height:100%;width:";
        html += String(value_);
        html += "%;background:#4caf50;color:#fff;text-align:center;"
                "line-height:24px;font-size:12px;\">";
        html += String(value_);
        html += "%</div></div>\n";

        // --- polling script ---------------------------------------------------
        html += "<script>\n"
                "(function(){\n"
                " const bar=document.getElementById('";  html += id(); html += "_bar');\n"
                " async function poll(){\n"
                "   try{\n"
                "     const r=await fetch('"; html += handle(); html += "');\n"
                "     if(r.ok){\n"
                "       const d=await r.json();\n"
                "       const v=parseFloat(d.value);\n"
                "       const pct=Math.min(100,Math.max(0,v));\n"
                "       bar.style.width=pct+'%';\n"
                "       bar.textContent=Math.round(pct)+'%';\n"
                "     }\n"
                "   }catch(e){}\n"
                " }\n"
                " poll();\n"
                " setInterval(poll,"; html += String(updateInterval_ * 1000); html += ");\n"
                "})();\n"
                "</script>\n";

        return html;
    }

private:
    T value_;
    T maxVal_;
};

#endif // WEBDISPLAY_H