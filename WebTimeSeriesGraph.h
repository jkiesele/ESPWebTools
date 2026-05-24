#pragma once

#include <Arduino.h>
#include "WebDisplay.h"
#include <vector>
#include <math.h>

/*
 * Generic bounded time-series graph for the ESP32 web interface.
 *
 * Dumb display buffer:
 *   - stores appended (x, y) points
 *   - keeps at most maxEntries points
 *   - exposes JSON via routeText()
 *   - renders a small canvas graph via createHtmlFragment()
 *
 * It deliberately does NOT:
 *   - decide when to sample
 *   - generate timestamps
 *   - validate timestamp order
 *   - check sensor validity
 *   - smooth/filter data
 */
class WebTimeSeriesGraph : public WebDisplayBase {
public:
    struct Point {
        uint32_t x;
        float y;
    };

    WebTimeSeriesGraph(const String& id,
                       uint32_t updateIntervalSecs,
                       const String& title,
                       const String& xLabel,
                       const String& yUnit,
                       size_t maxEntries = 144)
        : WebDisplayBase(id, updateIntervalSecs),
          title_(title),
          xLabel_(xLabel),
          yUnit_(yUnit),
          maxEntries_(maxEntries) {
        accessMutex_ = xSemaphoreCreateMutex();
        points_.reserve(maxEntries_);
    }

    ~WebTimeSeriesGraph() override {
        if (accessMutex_) {
            vSemaphoreDelete(accessMutex_);
            accessMutex_ = nullptr;
        }
    }

    WebTimeSeriesGraph(const WebTimeSeriesGraph&) = delete;
    WebTimeSeriesGraph& operator=(const WebTimeSeriesGraph&) = delete;

    bool append(uint32_t x, float y) {
        if (maxEntries_ == 0) {
            return false;
        }

        lock_();

        if (points_.size() >= maxEntries_) {
            points_.erase(points_.begin());
        }

        points_.push_back(Point{x, y});

        unlock_();
        return true;
    }

    void clear() {
        lock_();
        points_.clear();
        unlock_();
    }

    size_t size() const {
        lock_();
        const size_t s = points_.size();
        unlock_();
        return s;
    }

    size_t maxEntries() const {
        return maxEntries_;
    }

    void setMaxEntries(size_t maxEntries) {
        lock_();

        maxEntries_ = maxEntries;

        if (points_.size() > maxEntries_) {
            const size_t excess = points_.size() - maxEntries_;
            points_.erase(points_.begin(), points_.begin() + excess);
        }

        points_.reserve(maxEntries_);

        unlock_();
    }

    std::vector<Point> points() const {
        lock_();
        std::vector<Point> copy = points_;
        unlock_();
        return copy;
    }

    String routeText() const override {
        const std::vector<Point> copy = points();

        String json;
        json.reserve(96 + copy.size() * 24);

        json += F("{\"id\":\"");
        json += jsonEscape(id());
        json += F("\",\"title\":\"");
        json += jsonEscape(title_);
        json += F("\",\"xLabel\":\"");
        json += jsonEscape(xLabel_);
        json += F("\",\"yUnit\":\"");
        json += jsonEscape(yUnit_);
        json += F("\",\"points\":[");

        for (size_t i = 0; i < copy.size(); ++i) {
            if (i) json += ',';

            json += '[';
            json += String(copy[i].x);
            json += ',';

            if (isfinite(copy[i].y)) {
                json += String(copy[i].y, 3);
            } else {
                json += F("null");
            }

            json += ']';
        }

        json += F("]}");
        return json;
    }

    String createHtmlFragment() const override {
        String html;
        html.reserve(6200);

        const String canvasId = id() + F("_canvas");
        const String infoId   = id() + F("_info");

        html += F("<div style=\"width:100%;max-width:720px;margin:6px 0 14px 0;\">");
        html += F("<div id=\"");
        html += htmlEscape_(infoId);
        html += F("\" style=\"font:12px sans-serif;margin-bottom:4px;opacity:0.85;\">");
        html += htmlEscape_(title_);
        html += F("</div>");
        html += F("<canvas id=\"");
        html += htmlEscape_(canvasId);
        html += F("\" width=\"720\" height=\"220\" style=\"width:100%;height:220px;border:1px solid #bbb;border-radius:4px;box-sizing:border-box;\"></canvas>");
        html += F("</div>\n");

        html += F("<script>\n");
        html += F("(function(){\n");

        html += F("const canvas=document.getElementById(");
        html += jsStringLiteral_(canvasId);
        html += F(");\n");

        html += F("const info=document.getElementById(");
        html += jsStringLiteral_(infoId);
        html += F(");\n");

        html += F("if(!canvas||!info) return;\n");
        html += F("const ctx=canvas.getContext('2d');\n");

        html += F("function fmtTime(t){\n");
        html += F("  if(!Number.isFinite(t)) return '';\n");
        html += F("  const d=new Date(t*1000);\n");
        html += F("  return d.toLocaleString([], {hour:'2-digit', minute:'2-digit'});\n");
        html += F("}\n");

        html += F("function draw(raw){\n");
        html += F("  const data=raw||{};\n");
        html += F("  const pts=(data.points||[]).filter(p => Array.isArray(p) && Number.isFinite(Number(p[0])) && Number.isFinite(Number(p[1])));\n");
        html += F("  const w=canvas.width;\n");
        html += F("  const h=canvas.height;\n");
        html += F("  const ml=44, mr=10, mt=12, mb=28;\n");
        html += F("  const gw=w-ml-mr;\n");
        html += F("  const gh=h-mt-mb;\n");

        html += F("  ctx.clearRect(0,0,w,h);\n");
        html += F("  ctx.font='12px sans-serif';\n");
        html += F("  ctx.lineWidth=1;\n");
        html += F("  ctx.strokeStyle='#ccc';\n");
        html += F("  ctx.strokeRect(ml,mt,gw,gh);\n");

        html += F("  const title=data.title||'';\n");
        html += F("  const unit=data.yUnit||'';\n");

        html += F("  if(pts.length===0){\n");
        html += F("    info.textContent=title + ': no valid data';\n");
        html += F("    return;\n");
        html += F("  }\n");

        html += F("  let xmin=Number(pts[0][0]), xmax=Number(pts[0][0]);\n");
        html += F("  let ymin=Number(pts[0][1]), ymax=Number(pts[0][1]);\n");
        html += F("  for(const p of pts){\n");
        html += F("    const x=Number(p[0]);\n");
        html += F("    const y=Number(p[1]);\n");
        html += F("    if(x<xmin) xmin=x;\n");
        html += F("    if(x>xmax) xmax=x;\n");
        html += F("    if(y<ymin) ymin=y;\n");
        html += F("    if(y>ymax) ymax=y;\n");
        html += F("  }\n");

        html += F("  if(xmax===xmin){ xmax=xmin+1; }\n");
        html += F("  if(ymax===ymin){ ymax=ymin+0.5; ymin=ymin-0.5; }\n");
        html += F("  const ypad=(ymax-ymin)*0.08;\n");
        html += F("  ymin-=ypad;\n");
        html += F("  ymax+=ypad;\n");

        html += F("  function px(x){ return ml + (x-xmin)/(xmax-xmin)*gw; }\n");
        html += F("  function py(y){ return mt + gh - (y-ymin)/(ymax-ymin)*gh; }\n");

        html += F("  ctx.fillStyle='#555';\n");
        html += F("  ctx.textAlign='right';\n");
        html += F("  ctx.textBaseline='middle';\n");
        html += F("  ctx.fillText(ymax.toFixed(1), ml-5, mt);\n");
        html += F("  ctx.fillText(ymin.toFixed(1), ml-5, mt+gh);\n");

        html += F("  ctx.textAlign='left';\n");
        html += F("  ctx.textBaseline='top';\n");
        html += F("  ctx.fillText(fmtTime(xmin), ml, mt+gh+6);\n");

        html += F("  ctx.textAlign='right';\n");
        html += F("  ctx.fillText(fmtTime(xmax), ml+gw, mt+gh+6);\n");

        html += F("  if(pts.length===1){\n");
        html += F("    ctx.beginPath();\n");
        html += F("    ctx.arc(px(Number(pts[0][0])), py(Number(pts[0][1])), 3, 0, Math.PI*2);\n");
        html += F("    ctx.fillStyle='#222';\n");
        html += F("    ctx.fill();\n");
        html += F("  } else {\n");
        html += F("    ctx.beginPath();\n");
        html += F("    for(let i=0;i<pts.length;i++){\n");
        html += F("      const x=px(Number(pts[i][0]));\n");
        html += F("      const y=py(Number(pts[i][1]));\n");
        html += F("      if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);\n");
        html += F("    }\n");
        html += F("    ctx.strokeStyle='#222';\n");
        html += F("    ctx.lineWidth=2;\n");
        html += F("    ctx.stroke();\n");
        html += F("  }\n");

        html += F("  const last=pts[pts.length-1];\n");
        html += F("  info.textContent=title + ': ' + Number(last[1]).toFixed(2) + ' ' + unit + ' | ' + pts.length + ' points';\n");
        html += F("}\n");

        html += F("async function poll(){\n");
        html += F("  try{\n");
        html += F("    const r=await fetch(");
        html += jsStringLiteral_(handle());
        html += F(");\n");
        html += F("    if(r.ok){ draw(await r.json()); }\n");
        html += F("  }catch(e){\n");
        html += F("    info.textContent='graph update failed';\n");
        html += F("  }\n");
        html += F("}\n");

        html += F("poll();\n");

        if (updateInterval() > 0) {
            html += F("setInterval(poll,");
            html += String(updateInterval() * 1000UL);
            html += F(");\n");
        }

        html += F("})();\n");
        html += F("</script>\n");

        return html;
    }

private:
    void lock_() const {
        if (accessMutex_) {
            xSemaphoreTake(accessMutex_, portMAX_DELAY);
        }
    }

    void unlock_() const {
        if (accessMutex_) {
            xSemaphoreGive(accessMutex_);
        }
    }

    static String htmlEscape_(const String& in) {
        String out;
        out.reserve(in.length() + 8);

        for (size_t i = 0; i < in.length(); ++i) {
            const char c = in[i];
            switch (c) {
                case '&': out += F("&amp;"); break;
                case '<': out += F("&lt;"); break;
                case '>': out += F("&gt;"); break;
                case '"': out += F("&quot;"); break;
                case '\'': out += F("&#39;"); break;
                default: out += c; break;
            }
        }

        return out;
    }

    static String jsStringLiteral_(const String& in) {
        return String('"') + jsonEscape(in) + String('"');
    }

    String title_;
    String xLabel_;
    String yUnit_;

    size_t maxEntries_;
    std::vector<Point> points_;

    mutable SemaphoreHandle_t accessMutex_{nullptr};
};

/*
* example

WebTimeSeriesGraph reservoirTempGraph(
    "reservoir_temp_graph",
    30,                 // web poll interval in seconds
    "Reservoir temperature",
    "Time",
    "C",
    144                 // e.g. 24 h if sampled every 10 min
);
*/