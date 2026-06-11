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
 *
 * Tick behavior:
 *   - x-axis ticks are chosen from fixed "nice" time steps, UTC-aligned
 *   - y-axis ticks use 1, 2, 2.5, 5 * 10^n steps
 *   - tick count is approximate, usually around 4-7
 *   - min/max labels are always shown separately
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

        uint32_t effectiveXMin = 0;
        uint32_t effectiveXMax = 0;
        const bool hasValidX = computeEffectiveXAxis_(copy, effectiveXMin, effectiveXMax);

        float effectiveYMin = NAN;
        float effectiveYMax = NAN;
        const bool fixedYAxis = computeEffectiveYAxis_(copy, effectiveYMin, effectiveYMax);

        std::vector<uint32_t> xTicks;
        std::vector<float> yTicks;
        uint8_t yTickDecimals = 1;

        if (hasValidX) {
            xTicks = computeNiceXTicks_(effectiveXMin, effectiveXMax);
        }

        if (isfinite(effectiveYMin) && isfinite(effectiveYMax)) {
            yTicks = computeNiceYTicks_(effectiveYMin, effectiveYMax, yTickDecimals);
        }

        String json;
        json.reserve(220 + copy.size() * 24 + xTicks.size() * 12 + yTicks.size() * 12);

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

        json += F(",\"xMin\":");
        if (hasValidX) {
            json += String(effectiveXMin);
        } else {
            json += F("null");
        }

        json += F(",\"xMax\":");
        if (hasValidX) {
            json += String(effectiveXMax);
        } else {
            json += F("null");
        }

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

        json += F(",\"yTickDecimals\":");
        json += String(yTickDecimals);

        json += F(",\"xTicks\":[");
        for (size_t i = 0; i < xTicks.size(); ++i) {
            if (i) {
                json += ',';
            }
            json += String(xTicks[i]);
        }
        json += F("]");

        json += F(",\"yTicks\":[");
        for (size_t i = 0; i < yTicks.size(); ++i) {
            if (i) {
                json += ',';
            }
            json += String(yTicks[i], static_cast<unsigned int>(yTickDecimals));
        }
        json += F("]");

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
        html.reserve(7200);

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

        html += F("function fmtTime(t,span){\n");
        html += F("  if(!Number.isFinite(t)) return '';\n");
        html += F("  const d=new Date(t*1000);\n");
        html += F("  if(span < 3600) return d.toLocaleString([], {hour:'2-digit', minute:'2-digit', second:'2-digit'});\n");
        html += F("  if(span < 86400) return d.toLocaleString([], {hour:'2-digit', minute:'2-digit'});\n");
        html += F("  return d.toLocaleString([], {month:'2-digit', day:'2-digit', hour:'2-digit', minute:'2-digit'});\n");
        html += F("}\n");

        html += F("function fmtY(v,dec){\n");
        html += F("  if(!Number.isFinite(v)) return '';\n");
        html += F("  dec=Number.isFinite(dec)?Math.max(0,Math.min(4,dec)):1;\n");
        html += F("  return Number(v).toFixed(dec);\n");
        html += F("}\n");

        html += F("function draw(raw){\n");
        html += F("  const data=raw||{};\n");
        html += F("  const pts=(data.points||[]).filter(p => Array.isArray(p) && Number.isFinite(Number(p[0])) && Number.isFinite(Number(p[1])));\n");
        html += F("  const xTicks=(data.xTicks||[]).map(Number).filter(Number.isFinite);\n");
        html += F("  const yTicks=(data.yTicks||[]).map(Number).filter(Number.isFinite);\n");
        html += F("  const yDec=Number(data.yTickDecimals);\n");
        html += F("  const w=canvas.width;\n");
        html += F("  const h=canvas.height;\n");
        html += F("  const ml=48, mr=10, mt=12, mb=30;\n");
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

        html += F("  let xmin=Number(data.xMin);\n");
        html += F("  let xmax=Number(data.xMax);\n");
        html += F("  let ymin=Number(data.yMin);\n");
        html += F("  let ymax=Number(data.yMax);\n");

        html += F("  if(!Number.isFinite(xmin)||!Number.isFinite(xmax)||xmax===xmin){\n");
        html += F("    xmin=Number(pts[0][0]);\n");
        html += F("    xmax=xmin+1;\n");
        html += F("  }\n");

        html += F("  if(!Number.isFinite(ymin)||!Number.isFinite(ymax)||ymax===ymin){\n");
        html += F("    ymin=Number(pts[0][1])-0.5;\n");
        html += F("    ymax=Number(pts[0][1])+0.5;\n");
        html += F("  }\n");

        html += F("  const xSpan=xmax-xmin;\n");

        html += F("  function px(x){ return ml + (x-xmin)/(xmax-xmin)*gw; }\n");
        html += F("  function py(y){ return mt + gh - (y-ymin)/(ymax-ymin)*gh; }\n");

        html += F("  ctx.strokeStyle='#eee';\n");
        html += F("  ctx.fillStyle='#777';\n");
        html += F("  ctx.textBaseline='middle';\n");

        html += F("  for(const yt of yTicks){\n");
        html += F("    if(yt<ymin || yt>ymax) continue;\n");
        html += F("    const yy=py(yt);\n");
        html += F("    ctx.beginPath();\n");
        html += F("    ctx.moveTo(ml,yy);\n");
        html += F("    ctx.lineTo(ml+gw,yy);\n");
        html += F("    ctx.stroke();\n");
        html += F("    if(Math.abs(yy-mt)>11 && Math.abs(yy-(mt+gh))>11){\n");
        html += F("      ctx.textAlign='right';\n");
        html += F("      ctx.fillText(fmtY(yt,yDec), ml-5, yy);\n");
        html += F("    }\n");
        html += F("  }\n");

        html += F("  ctx.textBaseline='top';\n");
        html += F("  for(const xt of xTicks){\n");
        html += F("    if(xt<xmin || xt>xmax) continue;\n");
        html += F("    const xx=px(xt);\n");
        html += F("    ctx.beginPath();\n");
        html += F("    ctx.moveTo(xx,mt);\n");
        html += F("    ctx.lineTo(xx,mt+gh);\n");
        html += F("    ctx.stroke();\n");
        html += F("    if(xx>ml+58 && xx<ml+gw-58){\n");
        html += F("      ctx.textAlign='center';\n");
        html += F("      ctx.fillText(fmtTime(xt,xSpan), xx, mt+gh+6);\n");
        html += F("    }\n");
        html += F("  }\n");

        html += F("  ctx.fillStyle='#555';\n");
        html += F("  ctx.textAlign='right';\n");
        html += F("  ctx.textBaseline='middle';\n");
        html += F("  ctx.fillText(fmtY(ymax,yDec), ml-5, mt);\n");
        html += F("  ctx.fillText(fmtY(ymin,yDec), ml-5, mt+gh);\n");

        html += F("  ctx.textAlign='left';\n");
        html += F("  ctx.textBaseline='top';\n");
        html += F("  ctx.fillText(fmtTime(xmin,xSpan), ml, mt+gh+6);\n");

        html += F("  ctx.textAlign='right';\n");
        html += F("  ctx.fillText(fmtTime(xmax,xSpan), ml+gw, mt+gh+6);\n");

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

    static uint32_t ceilToStep_(uint32_t value, uint32_t step) {
        if (step == 0) {
            return value;
        }

        const uint64_t v = value;
        const uint64_t s = step;
        const uint64_t r = ((v + s - 1ULL) / s) * s;

        if (r > 0xFFFFFFFFULL) {
            return 0xFFFFFFFFUL;
        }

        return static_cast<uint32_t>(r);
    }

    static uint32_t floorToStep_(uint32_t value, uint32_t step) {
        if (step == 0) {
            return value;
        }

        return static_cast<uint32_t>((static_cast<uint64_t>(value) / step) * step);
    }

    static int countTicksForStep_(uint32_t xMin, uint32_t xMax, uint32_t step) {
        if (step == 0 || xMax < xMin) {
            return 0;
        }

        const uint32_t first = ceilToStep_(xMin, step);
        const uint32_t last = floorToStep_(xMax, step);

        if (first > last) {
            return 0;
        }

        return static_cast<int>((static_cast<uint64_t>(last - first) / step) + 1ULL);
    }

    bool computeEffectiveXAxis_(const std::vector<Point>& points,
                                uint32_t& xMin,
                                uint32_t& xMax) const {
        bool found = false;

        for (const auto& p : points) {
            if (!isfinite(p.y)) {
                continue;
            }

            if (!found) {
                xMin = p.x;
                xMax = p.x;
                found = true;
            } else {
                if (p.x < xMin) {
                    xMin = p.x;
                }
                if (p.x > xMax) {
                    xMax = p.x;
                }
            }
        }

        if (!found) {
            xMin = 0;
            xMax = 0;
            return false;
        }

        if (xMax == xMin) {
            if (xMax < 0xFFFFFFFFUL) {
                ++xMax;
            } else if (xMin > 0) {
                --xMin;
            }
        }

        return true;
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

    static std::vector<uint32_t> computeNiceXTicks_(uint32_t xMin, uint32_t xMax) {
        std::vector<uint32_t> ticks;

        if (xMax <= xMin) {
            return ticks;
        }

        static const uint32_t niceSteps[] = {
            1UL,
            2UL,
            5UL,
            10UL,
            15UL,
            30UL,

            60UL,
            2UL * 60UL,
            5UL * 60UL,
            10UL * 60UL,
            15UL * 60UL,
            30UL * 60UL,

            60UL * 60UL,
            2UL * 60UL * 60UL,
            3UL * 60UL * 60UL,
            6UL * 60UL * 60UL,
            12UL * 60UL * 60UL,

            24UL * 60UL * 60UL,
            2UL * 24UL * 60UL * 60UL,
            3UL * 24UL * 60UL * 60UL,
            7UL * 24UL * 60UL * 60UL,
            14UL * 24UL * 60UL * 60UL,

            30UL * 24UL * 60UL * 60UL,
            60UL * 24UL * 60UL * 60UL,
            90UL * 24UL * 60UL * 60UL,
            180UL * 24UL * 60UL * 60UL,
            365UL * 24UL * 60UL * 60UL
        };

        static const int targetTicks = 5;

        uint32_t bestStep = niceSteps[0];
        int bestScore = 1000000;
        int bestCount = 0;

        for (size_t i = 0; i < sizeof(niceSteps) / sizeof(niceSteps[0]); ++i) {
            const uint32_t step = niceSteps[i];
            const int count = countTicksForStep_(xMin, xMax, step);

            if (count <= 0) {
                continue;
            }

            int score = abs(count - targetTicks);

            if (count < 3 || count > 8) {
                score += 100;
            }

            if (score < bestScore || (score == bestScore && count < bestCount)) {
                bestScore = score;
                bestStep = step;
                bestCount = count;
            }
        }

        const uint32_t first = ceilToStep_(xMin, bestStep);
        const uint32_t last = floorToStep_(xMax, bestStep);

        if (first > last) {
            return ticks;
        }

        ticks.reserve(bestCount > 0 ? bestCount : 5);

        for (uint64_t t = first; t <= last; t += bestStep) {
            ticks.push_back(static_cast<uint32_t>(t));

            if (ticks.size() >= 12) {
                break;
            }

            if (bestStep == 0) {
                break;
            }
        }

        return ticks;
    }

    static uint8_t decimalsForStep_(float step) {
        if (!isfinite(step) || step <= 0.0f) {
            return 1;
        }

        for (uint8_t decimals = 0; decimals <= 4; ++decimals) {
            const float scale = powf(10.0f, static_cast<float>(decimals));
            const float scaled = step * scale;
            const float rounded = roundf(scaled);

            if (fabsf(scaled - rounded) < 0.0001f) {
                return decimals;
            }
        }

        return 4;
    }

    static float niceYStepCandidate_(float range, int exponentOffset, uint8_t baseIndex) {
        static const float bases[] = {1.0f, 2.0f, 2.5f, 5.0f};

        const float raw = range / 5.0f;
        if (!isfinite(raw) || raw <= 0.0f) {
            return NAN;
        }

        const int expBase = static_cast<int>(floorf(log10f(raw)));
        const int exp = expBase + exponentOffset;

        return bases[baseIndex] * powf(10.0f, static_cast<float>(exp));
    }

    static int countYTicksForStep_(float yMin, float yMax, float step) {
        if (!isfinite(yMin) || !isfinite(yMax) || !isfinite(step) || step <= 0.0f || yMax < yMin) {
            return 0;
        }

        const float first = ceilf(yMin / step) * step;
        const float last = floorf(yMax / step) * step;

        if (first > last) {
            return 0;
        }

        return static_cast<int>(floorf((last - first) / step + 0.5f)) + 1;
    }

    static std::vector<float> computeNiceYTicks_(float yMin, float yMax, uint8_t& decimals) {
        std::vector<float> ticks;
        decimals = 1;

        if (!isfinite(yMin) || !isfinite(yMax) || yMax <= yMin) {
            return ticks;
        }

        static const int targetTicks = 5;

        float bestStep = NAN;
        int bestScore = 1000000;
        int bestCount = 0;

        const float range = yMax - yMin;

        for (int expOffset = -1; expOffset <= 2; ++expOffset) {
            for (uint8_t baseIndex = 0; baseIndex < 4; ++baseIndex) {
                const float step = niceYStepCandidate_(range, expOffset, baseIndex);
                const int count = countYTicksForStep_(yMin, yMax, step);

                if (count <= 0) {
                    continue;
                }

                int score = abs(count - targetTicks);

                if (count < 3 || count > 8) {
                    score += 100;
                }

                if (score < bestScore || (score == bestScore && count < bestCount)) {
                    bestScore = score;
                    bestStep = step;
                    bestCount = count;
                }
            }
        }

        if (!isfinite(bestStep) || bestStep <= 0.0f) {
            return ticks;
        }

        decimals = decimalsForStep_(bestStep);

        const float first = ceilf(yMin / bestStep) * bestStep;
        const float last = floorf(yMax / bestStep) * bestStep;

        if (first > last) {
            return ticks;
        }

        ticks.reserve(bestCount > 0 ? bestCount : 5);

        for (float y = first; y <= last + bestStep * 0.001f; y += bestStep) {
            float yOut = y;

            if (fabsf(yOut) < bestStep * 0.0001f) {
                yOut = 0.0f;
            }

            ticks.push_back(yOut);

            if (ticks.size() >= 12) {
                break;
            }
        }

        return ticks;
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