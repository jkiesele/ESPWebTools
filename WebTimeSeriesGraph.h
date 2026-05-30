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
 *
 * Y-axis behavior:
 *   - yAxisMin <= yAxisMax: fixed y-axis range
 *   - yAxisMin >  yAxisMax: dynamic y-axis range from current valid points
 *
 * Boundary behavior:
 *   - points exactly at yAxisMin or yAxisMax are displayed on the plot border
 *   - points outside a fixed y-axis range are not rejected, but drawing is clipped
 */
class WebTimeSeriesGraph : public WebDisplayBase {
public:
    struct Point {
        uint32_t x;  // normally UTC Unix timestamp in seconds
        float y;
    };

    WebTimeSeriesGraph(const String& id,
                       uint32_t updateIntervalSecs,
                       const String& title,
                       const String& xLabel,
                       const String& yUnit,
                       size_t maxEntries = 144,
                       float yAxisMin = 1.0f,
                       float yAxisMax = 0.0f)
        : WebDisplayBase(id, updateIntervalSecs),
          title_(title),
          xLabel_(xLabel),
          yUnit_(yUnit),
          maxEntries_(maxEntries),
          yAxisMin_(yAxisMin),
          yAxisMax_(yAxisMax) {
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

    Point lastPoint() const {
        lock_();
        Point p{0, NAN};
        if (!points_.empty()) {
            p = points_.back();
        }
        unlock_();
        return p;
    }

    void setYAxisRange(float yMin, float yMax) {
        lock_();
        yAxisMin_ = yMin;
        yAxisMax_ = yMax;
        unlock_();
    }

    void setDynamicYAxis() {
        setYAxisRange(1.0f, 0.0f);
    }

    bool hasFixedYAxis() const {
        lock_();
        const bool fixed = hasFixedYAxisUnlocked_();
        unlock_();
        return fixed;
    }

    float configuredYAxisMin() const {
        lock_();
        const float v = yAxisMin_;
        unlock_();
        return v;
    }

    float configuredYAxisMax() const {
        lock_();
        const float v = yAxisMax_;
        unlock_();
        return v;
    }

    String routeText() const override {
        const std::vector<Point> copy = points();

        float effectiveYMin = NAN;
        float effectiveYMax = NAN;
        const bool fixedYAxis = computeEffectiveYAxis_(copy, effectiveYMin, effectiveYMax);

        String json;
        json.reserve(140 + copy.size() * 24);

        json += F("{\"id\":\"");
        json += jsonEscape(id());
        json += F("\",\"title\":\"");
        json += jsonEscape(title_);
        json += F("\",\"xLabel\":\"");
        json += jsonEscape(xLabel_);
        json += F("\",\"yUnit\":\"");
        json += jsonEscape(yUnit_);

        json += F("\",\"yAxisFixed\":");
        json += fixedYAxis ? F("true") : F("false");

        json += F(",\"yMin\":");
        if (isfinite(effectiveYMin)) {
            json += String(effectiveYMin, 3);
        } else {
            json += F("null");
        }

        json += F(",\"yMax\":");
        if (isfinite(effectiveYMax)) {
            json += String(effectiveYMax, 3);
        } else {
            json += F("null");
        }

        json += F(",\"points\":[");

        for (size_t i = 0; i < copy.size(); ++i) {
            if (i) {
                json += ',';
            }

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
        html.reserve(5400);

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

        html += F("  const title=data.title||'';\n");
        html += F("  const unit=data.yUnit||'';\n");

        html += F("  if(pts.length===0){\n");
        html += F("    ctx.strokeStyle='#ccc';\n");
        html += F("    ctx.strokeRect(ml,mt,gw,gh);\n");
        html += F("    info.textContent=title + ': no valid data';\n");
        html += F("    return;\n");
        html += F("  }\n");

        html += F("  let xmin=Number(pts[0][0]), xmax=Number(pts[0][0]);\n");
        html += F("  for(const p of pts){\n");
        html += F("    const x=Number(p[0]);\n");
        html += F("    if(x<xmin) xmin=x;\n");
        html += F("    if(x>xmax) xmax=x;\n");
        html += F("  }\n");

        html += F("  if(xmax===xmin){ xmax=xmin+1; }\n");

        html += F("  let ymin=Number(data.yMin);\n");
        html += F("  let ymax=Number(data.yMax);\n");
        html += F("  if(!Number.isFinite(ymin)||!Number.isFinite(ymax)||ymax===ymin){\n");
        html += F("    ymin=Number(pts[0][1])-0.5;\n");
        html += F("    ymax=Number(pts[0][1])+0.5;\n");
        html += F("  }\n");

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

        html += F("  ctx.save();\n");
        html += F("  ctx.beginPath();\n");
        html += F("  ctx.rect(ml-1, mt-1, gw+2, gh+2);\n");
        html += F("  ctx.clip();\n");

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

        html += F("  ctx.restore();\n");

        html += F("  ctx.strokeStyle='#ccc';\n");
        html += F("  ctx.lineWidth=1;\n");
        html += F("  ctx.strokeRect(ml,mt,gw,gh);\n");

        html += F("  const last=pts[pts.length-1];\n");
        html += F("  info.textContent=title + ': ' + Number(last[1]).toFixed(2) + ' ' + unit + ' | ' + pts.length + ' points' + (data.yAxisFixed ? ' | fixed y-axis' : '');\n");
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

    bool hasFixedYAxisUnlocked_() const {
        return isfinite(yAxisMin_) && isfinite(yAxisMax_) && yAxisMin_ <= yAxisMax_;
    }

    bool computeEffectiveYAxis_(const std::vector<Point>& points,
                                float& yMin,
                                float& yMax) const {
        lock_();
        const bool fixed = hasFixedYAxisUnlocked_();
        const float configuredMin = yAxisMin_;
        const float configuredMax = yAxisMax_;
        unlock_();

        if (fixed) {
            yMin = configuredMin;
            yMax = configuredMax;

            // Degenerate fixed axis, e.g. 20..20.
            // Constructor contract allows yMin <= yMax, but rendering needs nonzero height.
            if (yMax == yMin) {
                yMin = configuredMin - 0.5f;
                yMax = configuredMax + 0.5f;
            }

            return true;
        }

        yMin = NAN;
        yMax = NAN;

        for (const auto& p : points) {
            if (!isfinite(p.y)) {
                continue;
            }

            if (!isfinite(yMin) || p.y < yMin) {
                yMin = p.y;
            }

            if (!isfinite(yMax) || p.y > yMax) {
                yMax = p.y;
            }
        }

        if (!isfinite(yMin) || !isfinite(yMax)) {
            return false;
        }

        if (yMax == yMin) {
            yMin -= 0.5f;
            yMax += 0.5f;
        } else {
            const float yPad = (yMax - yMin) * 0.08f;
            yMin -= yPad;
            yMax += yPad;
        }

        return false;
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

    float yAxisMin_;
    float yAxisMax_;

    mutable SemaphoreHandle_t accessMutex_{nullptr};
};

/*
 * Examples
 *
 * Dynamic y-axis, old constructor style still works:
 *
 * WebTimeSeriesGraph reservoirTempGraph(
 *     "reservoir_temp_graph",
 *     30,
 *     "Reservoir temperature",
 *     "Time",
 *     "C",
 *     144
 * );
 *
 * Fixed y-axis, e.g. percentage:
 *
 * WebTimeSeriesGraph fillLevelGraph(
 *     "fill_level_graph",
 *     30,
 *     "Reservoir fill level",
 *     "Time",
 *     "%",
 *     144,
 *     0.0f,
 *     100.0f
 * );
 */