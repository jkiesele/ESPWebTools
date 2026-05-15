#pragma once

#include "WiFiWrapper.h"
#include "WebDisplay.h"


class WiFiRSSIDisplay : public WebDisplayBase {
public:
    WiFiRSSIDisplay(const String& id,
                    const WiFiWrapper& wifi,
                    uint32_t updateIntervalSecs = 10)
        : WebDisplayBase(id, updateIntervalSecs),
          wifi_(wifi) {}

    String routeText() const override {
        const int32_t rssi = wifi_.getSignalStrength();
        const String summary = wifi_.getConnectionSummary();

        String json;
        json.reserve(id_.length() + summary.length() + 160);
        json += F("{\"id\":\"");
        json += id();
        json += F("\",\"value\":");
        json += String(rssi);
        json += F(",\"level\":\"");
        json += levelText(rssi);
        json += F("\",\"percent\":");
        json += String(rssiToPercent(rssi));
        json += F(",\"summary\":\"");
        json += jsonEscape(summary);
        json += F("\"}");
        return json;
    }

    String createHtmlFragment() const override {
        const int32_t rssi = wifi_.getSignalStrength();
        const int pct = rssiToPercent(rssi);
        const String summary = wifi_.getConnectionSummary();

        String html;
        html.reserve(2400 + summary.length());

        html += "<div id=\""; html += id(); html += "_container\" "
                "style=\"width:520px;max-width:100%;font-family:sans-serif;"
                "font-size:12px;margin:4px 0;\">\n";

        html += "  <div style=\"display:flex;justify-content:space-between;"
                "align-items:center;margin-bottom:2px;\">\n";
        html += "    <span>WiFi RSSI</span>\n";
        html += "    <span id=\""; html += id(); html += "_text\">";
        html += String(rssi);
        html += " dBm / ";
        html += levelText(rssi);
        html += "</span>\n";
        html += "  </div>\n";

        html += "  <div style=\"position:relative;width:100%;height:16px;"
                "background:#ddd;border-radius:4px;overflow:hidden;\">\n";

        html += "    <div id=\""; html += id(); html += "_bar\" "
                "style=\"height:100%;width:";
        html += String(pct);
        html += "%;background:";
        html += colorFor(rssi);
        html += ";\"></div>\n";

        html += "  </div>\n";

        html += "  <div id=\""; html += id(); html += "_summary\" "
                "style=\"margin-top:3px;color:#555;white-space:normal;"
                "overflow-wrap:anywhere;line-height:1.25;\">";
        html += htmlEscape(summary);
        html += "</div>\n";

        html += "</div>\n";

        html += "<script>\n"
                "(function(){\n"
                "  const bar=document.getElementById('"; html += id(); html += "_bar');\n";
        html += "  const txt=document.getElementById('"; html += id(); html += "_text');\n";
        html += "  const sum=document.getElementById('"; html += id(); html += "_summary');\n";
        html += "  function colorFor(rssi){\n"
                "    if(rssi <= -126) return '#777';\n"
                "    if(rssi >= -60) return '#4caf50';\n"
                "    if(rssi >= -70) return '#8bc34a';\n"
                "    if(rssi >= -80) return '#ff9800';\n"
                "    return '#f44336';\n"
                "  }\n"
                "  async function poll(){\n"
                "    try{\n"
                "      const r=await fetch('"; html += handle(); html += "');\n";
        html += "      if(r.ok){\n"
                "        const d=await r.json();\n"
                "        const v=parseInt(d.value);\n"
                "        const pct=Math.min(100,Math.max(0,parseInt(d.percent)));\n"
                "        bar.style.width=pct+'%';\n"
                "        bar.style.background=colorFor(v);\n"
                "        txt.textContent=(v <= -126) ? 'disconnected' : (v + ' dBm / ' + d.level);\n"
                "        if(sum && typeof d.summary === 'string') sum.textContent=d.summary;\n"
                "      }\n"
                "    }catch(e){}\n"
                "  }\n"
                "  poll();\n"
                "  setInterval(poll,"; html += String(updateInterval_ * 1000); html += ");\n"
                "})();\n"
                "</script>\n";

        return html;
    }

private:
    const WiFiWrapper& wifi_;

    static int rssiToPercent(int32_t rssi) {
        // Map useful WiFi range roughly:
        // -90 dBm -> 0 %
        // -50 dBm -> 100 %
        // disconnected / -127 -> 0 %
        if (rssi <= -126) return 0;
        if (rssi <= -90) return 0;
        if (rssi >= -50) return 100;
        return static_cast<int>((rssi + 90) * 100 / 40);
    }

    static const char* levelText(int32_t rssi) {
        if (rssi >= -50) return "excellent";
        if (rssi >= -60) return "good";
        if (rssi >= -70) return "fair";
        if (rssi >= -80) return "poor";
        if (rssi > -127)  return "very poor";
        return "disconnected";
    }

    static const char* colorFor(int32_t rssi) {
        if (rssi <= -126) return "#777";
        if (rssi >= -60) return "#4caf50";
        if (rssi >= -70) return "#8bc34a";
        if (rssi >= -80) return "#ff9800";
        return "#f44336";
    }

    static String htmlEscape(const String& in) {
        String out;
        out.reserve(in.length() + 8);

        for (size_t i = 0; i < in.length(); ++i) {
            const char c = in[i];
            switch (c) {
                case '&': out += F("&amp;");  break;
                case '<': out += F("&lt;");   break;
                case '>': out += F("&gt;");   break;
                case '"': out += F("&quot;"); break;
                case '\'': out += F("&#39;"); break;
                default: out += c; break;
            }
        }

        return out;
    }
};