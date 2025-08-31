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

// ---- Add this next to your helpers -----------------------------------------
template <>
struct _JsonHelper<bool> {
    static String encode(const bool &v) { return v ? F("true") : F("false"); }
    static String dom   (const bool &v) { return v ? F("true") : F("false"); } // not used by the bool UI
};

// ---- WebDisplay<bool> specialization ---------------------------------------
template <>
class WebDisplay<bool> : public WebDisplayBase {
public:
    WebDisplay(const String &id, uint32_t updateIntervalSecs, const bool &initial = false)
    : WebDisplayBase(id, updateIntervalSecs), value_(initial) {}

    void update(const bool &v) { value_ = v; }

    String routeText() const override {
        String json;
        json.reserve(id_.length() + 32);
        json += F("{\"id\":\"");
        json += id();
        json += F("\",\"value\":");
        json += _JsonHelper<bool>::encode(value_);
        json += '}';
        return json;
    }

    String createHtmlFragment() const override {
        String html;
        html.reserve(700);

        // Namespacing prefix to isolate styles per instance
        const String p = "wd-" + id();

        // Initial classes
        const char* redCls   = value_ ? "off"      : "on red";
        const char* greenCls = value_ ? "on green" : "off";

        // Inline, namespaced CSS (safe to repeat; unique per id)
        html += "<style>"
                "." + p + " .led{width:12px;height:12px;border-radius:50%;display:inline-block;"
                          "margin:0 4px 0 0;background:#bfbfbf;vertical-align:middle;"
                          "box-shadow:inset 0 0 2px rgba(0,0,0,.5)}"
                "." + p + " .on.red{background:#d63c3c}"
                "." + p + " .on.green{background:#2bb24c}"
                "." + p + " .off{background:#bfbfbf}"
                "." + p + " .wrap{display:inline-flex;align-items:center;gap:6px}"
                "." + p + " .lbl{font:12px/1.2 sans-serif;opacity:.85}"
                "</style>";

        // HTML
        html += "<div id=\""; html += id(); html += "_wrap\" class=\"";
        html += p; html += " wrap\" role=\"group\" aria-label=\""; html += id(); html += "\">";
        html +=   "<span id=\""; html += id(); html += "_r\" class=\"led "; html += redCls;   html += "\" aria-label=\"red\"></span>";
        html +=   "<span id=\""; html += id(); html += "_g\" class=\"led "; html += greenCls; html += "\" aria-label=\"green\"></span>";
        html +=   "<span class=\"lbl\">"; html += id(); html += "</span>";
        html += "</div>\n";

        // Polling script: fetch JSON {value: true|false} and toggle classes
        html += "<script>\n"
                "(function(){\n"
                "  const id='" + id() + "';\n"
                "  const r=document.getElementById(id+'_r');\n"
                "  const g=document.getElementById(id+'_g');\n"
                "  function apply(v){\n"
                "    if(v){ r.className='led off'; g.className='led on green'; }\n"
                "    else { r.className='led on red'; g.className='led off'; }\n"
                "  }\n"
                "  async function poll(){\n"
                "    try{\n"
                "      const resp=await fetch('" + handle() + "');\n"
                "      if(resp.ok){ const d=await resp.json(); apply(!!d.value); }\n"
                "    }catch(e){}\n"
                "  }\n"
                "  apply(" + String(value_ ? "true" : "false") + ");\n"
                "  poll();\n"
                "  setInterval(poll," + String(updateInterval_ * 1000) + ");\n"
                "})();\n"
                "</script>\n";

        return html;
    }

private:
    bool value_{false};
};


// -----------------------------------------------------------------------------
//  Progress-bar style display (0‒100 %)
// -----------------------------------------------------------------------------
template <typename T>
class WebBarDisplay : public WebDisplayBase {
public:
    WebBarDisplay(const String &id,
                  uint32_t      updateIntervalSecs,
                  const T      &maxVal = 100,
                  const String unit = "%")
        : WebDisplayBase(id, updateIntervalSecs), value_(), maxVal_(maxVal), unit_(unit) {}

    /* push a new percentage (0‒100) from firmware code */
    void update(const T &v) { value_ = v; }
    void setMaxVal(const T &maxVal) {
        maxVal_ = maxVal;
    }

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

        // ---------- static HTML ---------------------------------------------
        const float pctInit = (maxVal_ > 0) ? (value_ * 100.0f / maxVal_) : 0;

        html += "<div id=\""; html += id(); html += "_container\" "
                "style=\"position:relative;width:100%;height:24px;"
                "background:#ddd;border-radius:4px;overflow:hidden;\">\n";

        html += "  <div id=\""; html += id(); html += "_bar\" "
                "style=\"height:100%;width:";
        html += String(pctInit);
        html += "%;background:#4caf50;color:#fff;text-align:center;"
                "line-height:24px;font-size:12px;\">";
        html += String(value_);
        html += unit_;
        html += "</div></div>\n";

        // ---------- polling script ------------------------------------------
        html += "<script>\n"
                "(function(){\n"
                " const bar=document.getElementById('";  html += id(); html += "_bar');\n"
                " const max=";  html += String(maxVal_); html += ";\n"
                " const unit='"; html += unit_;         html += "';\n"
                " async function poll(){\n"
                "   try{\n"
                "     const r=await fetch('"; html += handle(); html += "');\n"
                "     if(r.ok){\n"
                "       const d=await r.json();\n"
                "       const v=parseFloat(d.value);\n"
                "       const pct=Math.min(100, Math.max(0,(v/max)*100));\n"
                "       bar.style.width=pct+'%';\n"
                "       bar.textContent=v.toFixed(1)+unit;\n"
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
    String unit_; // e.g. "%", "L", etc.
    T value_;
    T maxVal_;
};

#endif // WEBDISPLAY_H