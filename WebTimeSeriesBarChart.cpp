#include "WebTimeSeriesBarChart.h"

#include <algorithm>
#include <math.h>

WebTimeSeriesBarChart::WebTimeSeriesBarChart(const String& id,
                                             uint32_t updateIntervalSecs,
                                             const String& title,
                                             const String& xLabel,
                                             const String& yUnit,
                                             uint32_t binWidthSeconds,
                                             size_t maxBins,
                                             uint32_t binAlignmentTimestamp,
                                             float yAxisMin,
                                             float yAxisMax)
    : WebDisplayBase(id, updateIntervalSecs),
      title_(title),
      xLabel_(xLabel),
      yUnit_(yUnit),
      binWidthSeconds_(binWidthSeconds == 0 ? 1 : binWidthSeconds),
      maxBins_(maxBins),
      binAlignmentTimestamp_(binAlignmentTimestamp),
      yAxisMin_(yAxisMin),
      yAxisMax_(yAxisMax) {
    accessMutex_ = xSemaphoreCreateMutex();
    bins_.reserve(maxBins_);
}

WebTimeSeriesBarChart::~WebTimeSeriesBarChart() {
    if (accessMutex_) {
        vSemaphoreDelete(accessMutex_);
        accessMutex_ = nullptr;
    }
}

bool WebTimeSeriesBarChart::add(uint32_t timestamp, float amount) {
    if (!isfinite(amount) || maxBins_ == 0) {
        return false;
    }

    const uint32_t targetStart = alignedBinStart_(timestamp);

    lock_();

    if (bins_.empty()) {
        bins_.push_back(Bin{targetStart, amount});
        unlock_();
        return true;
    }

    const uint32_t newestStart = bins_.back().startTimestamp;

    if (targetStart > newestStart) {
        const uint64_t missingBins =
            static_cast<uint64_t>(targetStart - newestStart) / binWidthSeconds_;

        if (missingBins >= maxBins_) {
            bins_.clear();

            uint32_t firstStart = targetStart;
            size_t newBinCount = 1;
            while (newBinCount < maxBins_ && firstStart >= binWidthSeconds_) {
                firstStart -= binWidthSeconds_;
                ++newBinCount;
            }

            bins_.reserve(maxBins_);
            for (size_t i = 0; i < newBinCount; ++i) {
                bins_.push_back(Bin{
                    static_cast<uint32_t>(
                        static_cast<uint64_t>(firstStart) +
                        static_cast<uint64_t>(i) * binWidthSeconds_),
                    0.0f
                });
            }
        } else {
            while (bins_.back().startTimestamp < targetStart) {
                const uint32_t nextStart =
                    saturatingAdd_(bins_.back().startTimestamp, binWidthSeconds_);

                if (nextStart <= bins_.back().startTimestamp) {
                    unlock_();
                    return false;
                }

                bins_.push_back(Bin{nextStart, 0.0f});
                if (bins_.size() > maxBins_) {
                    bins_.erase(bins_.begin());
                }
            }
        }

        const float updatedValue = bins_.back().value + amount;
        if (!isfinite(updatedValue)) {
            unlock_();
            return false;
        }

        bins_.back().value = updatedValue;
        unlock_();
        return true;
    }

    for (auto it = bins_.rbegin(); it != bins_.rend(); ++it) {
        if (it->startTimestamp == targetStart) {
            const float updatedValue = it->value + amount;
            if (!isfinite(updatedValue)) {
                unlock_();
                return false;
            }

            it->value = updatedValue;
            unlock_();
            return true;
        }

        if (it->startTimestamp < targetStart) {
            break;
        }
    }

    unlock_();
    return false;
}

void WebTimeSeriesBarChart::clear() {
    lock_();
    bins_.clear();
    unlock_();
}

size_t WebTimeSeriesBarChart::size() const {
    lock_();
    const size_t result = bins_.size();
    unlock_();
    return result;
}

std::vector<WebTimeSeriesBarChart::Bin> WebTimeSeriesBarChart::bins() const {
    lock_();
    const std::vector<Bin> result = bins_;
    unlock_();
    return result;
}

String WebTimeSeriesBarChart::routeText() const {
    const std::vector<Bin> copy = bins();

    uint32_t xMin = 0;
    uint32_t xMax = 0;
    if (!copy.empty()) {
        xMin = copy.front().startTimestamp;
        xMax = saturatingAdd_(copy.back().startTimestamp, binWidthSeconds_);
    }

    float yMin = NAN;
    float yMax = NAN;
    const bool fixedYAxis = computeEffectiveYAxis_(copy, yMin, yMax);

    std::vector<uint32_t> xTicks;
    std::vector<float> yTicks;
    uint8_t yTickDecimals = 1;

    if (!copy.empty() && xMax > xMin) {
        xTicks = computeNiceXTicks_(xMin, xMax);
    }
    if (isfinite(yMin) && isfinite(yMax)) {
        yTicks = computeNiceYTicks_(yMin, yMax, yTickDecimals);
    }

    String json;
    json.reserve(260 + copy.size() * 28 + xTicks.size() * 12 + yTicks.size() * 12);

    json += F("{\"id\":\"");
    json += jsonEscape(id());
    json += F("\",\"title\":\"");
    json += jsonEscape(title_);
    json += F("\",\"xLabel\":\"");
    json += jsonEscape(xLabel_);
    json += F("\",\"yUnit\":\"");
    json += jsonEscape(yUnit_);
    json += F("\",\"binWidth\":");
    json += String(binWidthSeconds_);
    json += F(",\"yAxisFixed\":");
    json += fixedYAxis ? F("true") : F("false");
    json += F(",\"xMin\":");
    json += copy.empty() ? F("null") : String(xMin);
    json += F(",\"xMax\":");
    json += copy.empty() ? F("null") : String(xMax);
    json += F(",\"yMin\":");
    json += isfinite(yMin) ? String(yMin, 3) : String(F("null"));
    json += F(",\"yMax\":");
    json += isfinite(yMax) ? String(yMax, 3) : String(F("null"));
    json += F(",\"yTickDecimals\":");
    json += String(yTickDecimals);

    json += F(",\"xTicks\":[");
    for (size_t i = 0; i < xTicks.size(); ++i) {
        if (i) {
            json += ',';
        }
        json += String(xTicks[i]);
    }
    json += F("],\"yTicks\":[");
    for (size_t i = 0; i < yTicks.size(); ++i) {
        if (i) {
            json += ',';
        }
        json += String(yTicks[i], static_cast<unsigned int>(yTickDecimals));
    }
    json += F("],\"bins\":[");
    for (size_t i = 0; i < copy.size(); ++i) {
        if (i) {
            json += ',';
        }
        json += '[';
        json += String(copy[i].startTimestamp);
        json += ',';
        json += String(copy[i].value, 3);
        json += ']';
    }
    json += F("]}");

    return json;
}

String WebTimeSeriesBarChart::createHtmlFragment() const {
    String html;
    html.reserve(7600);

    const String canvasId = id() + F("_canvas");
    const String infoId = id() + F("_info");

    html += F("<div style=\"width:100%;max-width:720px;margin:6px 0 14px 0;\">");
    html += F("<div id=\"");
    html += htmlEscape_(infoId);
    html += F("\" style=\"font:12px sans-serif;margin-bottom:4px;opacity:0.85;\">");
    html += htmlEscape_(title_);
    html += F("</div>");
    html += F("<canvas id=\"");
    html += htmlEscape_(canvasId);
    html += F("\" width=\"720\" height=\"240\" style=\"width:100%;height:240px;border:1px solid #bbb;border-radius:4px;box-sizing:border-box;\"></canvas>");
    html += F("</div>\n<script>\n(function(){\n");

    html += F("const canvas=document.getElementById(");
    html += jsStringLiteral_(canvasId);
    html += F(");\nconst info=document.getElementById(");
    html += jsStringLiteral_(infoId);
    html += F(");\nif(!canvas||!info) return;\nconst ctx=canvas.getContext('2d');\n");

    html += F("function fmtTime(t,span){\n");
    html += F("  if(!Number.isFinite(t)) return '';\n");
    html += F("  const d=new Date(t*1000);\n");
    html += F("  if(span<172800) return d.toLocaleString([], {hour:'2-digit',minute:'2-digit'});\n");
    html += F("  if(span<15552000) return d.toLocaleDateString([], {month:'short',day:'2-digit'});\n");
    html += F("  return d.toLocaleDateString([], {month:'short',year:'2-digit'});\n}\n");

    html += F("function fmtY(v,dec){\n");
    html += F("  if(!Number.isFinite(v)) return '';\n");
    html += F("  dec=Number.isFinite(dec)?Math.max(0,Math.min(4,dec)):1;\n");
    html += F("  return Number(v).toFixed(dec);\n}\n");

    html += F("function draw(raw){\n");
    html += F("  const data=raw||{};\n");
    html += F("  const bins=(data.bins||[]).filter(b=>Array.isArray(b)&&Number.isFinite(Number(b[0]))&&Number.isFinite(Number(b[1])));\n");
    html += F("  const xTicks=(data.xTicks||[]).map(Number).filter(Number.isFinite);\n");
    html += F("  const yTicks=(data.yTicks||[]).map(Number).filter(Number.isFinite);\n");
    html += F("  const yDec=Number(data.yTickDecimals);\n");
    html += F("  const binWidth=Number(data.binWidth);\n");
    html += F("  const w=canvas.width,h=canvas.height;\n");
    html += F("  const ml=48,mr=10,mt=12,mb=46;\n");
    html += F("  const gw=w-ml-mr,gh=h-mt-mb;\n");
    html += F("  ctx.clearRect(0,0,w,h);ctx.font='12px sans-serif';ctx.lineWidth=1;\n");
    html += F("  const title=data.title||'',unit=data.yUnit||'',xLabel=data.xLabel||'';\n");

    html += F("  if(bins.length===0){\n");
    html += F("    ctx.strokeStyle='#ccc';ctx.strokeRect(ml,mt,gw,gh);\n");
    html += F("    info.textContent=title+': no data';return;\n  }\n");

    html += F("  let xmin=Number(data.xMin),xmax=Number(data.xMax);\n");
    html += F("  let ymin=Number(data.yMin),ymax=Number(data.yMax);\n");
    html += F("  if(!Number.isFinite(xmin)||!Number.isFinite(xmax)||xmax<=xmin){xmin=Number(bins[0][0]);xmax=xmin+Math.max(1,binWidth);}\n");
    html += F("  if(!Number.isFinite(ymin)||!Number.isFinite(ymax)||ymax<=ymin){ymin=0;ymax=1;}\n");
    html += F("  const xSpan=xmax-xmin;\n");
    html += F("  function px(x){return ml+(x-xmin)/(xmax-xmin)*gw;}\n");
    html += F("  function py(y){return mt+gh-(y-ymin)/(ymax-ymin)*gh;}\n");

    html += F("  ctx.strokeStyle='#eee';ctx.fillStyle='#777';ctx.textBaseline='middle';\n");
    html += F("  for(const yt of yTicks){\n");
    html += F("    if(yt<ymin||yt>ymax) continue;const yy=py(yt);\n");
    html += F("    ctx.beginPath();ctx.moveTo(ml,yy);ctx.lineTo(ml+gw,yy);ctx.stroke();\n");
    html += F("    if(Math.abs(yy-mt)>11&&Math.abs(yy-(mt+gh))>11){ctx.textAlign='right';ctx.fillText(fmtY(yt,yDec),ml-5,yy);}\n  }\n");

    html += F("  ctx.textBaseline='top';\n");
    html += F("  for(const xt of xTicks){\n");
    html += F("    if(xt<xmin||xt>xmax) continue;const xx=px(xt);\n");
    html += F("    ctx.beginPath();ctx.moveTo(xx,mt);ctx.lineTo(xx,mt+gh);ctx.stroke();\n");
    html += F("    if(xx>ml+45&&xx<ml+gw-45){ctx.textAlign='center';ctx.fillText(fmtTime(xt,xSpan),xx,mt+gh+6);}\n  }\n");

    html += F("  ctx.fillStyle='#555';ctx.textAlign='right';ctx.textBaseline='middle';\n");
    html += F("  ctx.fillText(fmtY(ymax,yDec),ml-5,mt);ctx.fillText(fmtY(ymin,yDec),ml-5,mt+gh);\n");
    html += F("  ctx.textAlign='left';ctx.textBaseline='top';ctx.fillText(fmtTime(xmin,xSpan),ml,mt+gh+6);\n");
    html += F("  ctx.textAlign='right';ctx.fillText(fmtTime(xmax,xSpan),ml+gw,mt+gh+6);\n");
    html += F("  ctx.textAlign='center';ctx.fillText(xLabel,ml+gw/2,mt+gh+25);\n");

    html += F("  ctx.save();ctx.beginPath();ctx.rect(ml-1,mt-1,gw+2,gh+2);ctx.clip();\n");
    html += F("  const baseline=py(Math.max(ymin,Math.min(ymax,0)));\n");
    html += F("  for(const bin of bins){\n");
    html += F("    const left=px(Number(bin[0])),right=px(Number(bin[0])+binWidth);\n");
    html += F("    const fullWidth=Math.max(1,right-left),gap=Math.min(4,fullWidth*0.16);\n");
    html += F("    const value=Number(bin[1]),valueY=py(Math.max(ymin,Math.min(ymax,value)));\n");
    html += F("    ctx.fillStyle=value<0?'#c95d5d':'#3f7fba';\n");
    html += F("    ctx.fillRect(left+gap/2,Math.min(valueY,baseline),Math.max(1,fullWidth-gap),Math.abs(baseline-valueY));\n  }\n");
    html += F("  ctx.restore();ctx.strokeStyle='#ccc';ctx.lineWidth=1;ctx.strokeRect(ml,mt,gw,gh);\n");

    html += F("  const last=bins[bins.length-1];\n");
    html += F("  info.textContent=title+': '+Number(last[1]).toFixed(2)+' '+unit+' | '+bins.length+' bins'+(data.yAxisFixed?' | fixed y-axis':'');\n");
    html += F("}\n");

    html += F("async function poll(){\n  try{\n    const r=await fetch(");
    html += jsStringLiteral_(handle());
    html += F(");\n    if(r.ok) draw(await r.json());\n  }catch(e){info.textContent='chart update failed';}\n}\n");
    html += F("poll();\n");
    if (updateInterval() > 0) {
        html += F("setInterval(poll,");
        html += String(updateInterval() * 1000UL);
        html += F(");\n");
    }
    html += F("})();\n</script>\n");

    return html;
}

void WebTimeSeriesBarChart::lock_() const {
    if (accessMutex_) {
        xSemaphoreTake(accessMutex_, portMAX_DELAY);
    }
}

void WebTimeSeriesBarChart::unlock_() const {
    if (accessMutex_) {
        xSemaphoreGive(accessMutex_);
    }
}

uint32_t WebTimeSeriesBarChart::alignedBinStart_(uint32_t timestamp) const {
    const uint32_t offset = binAlignmentTimestamp_ % binWidthSeconds_;
    if (timestamp < offset) {
        return 0;
    }
    return timestamp - ((timestamp - offset) % binWidthSeconds_);
}

bool WebTimeSeriesBarChart::hasFixedYAxisUnlocked_() const {
    return isfinite(yAxisMin_) && isfinite(yAxisMax_) && yAxisMin_ <= yAxisMax_;
}

uint32_t WebTimeSeriesBarChart::saturatingAdd_(uint32_t value, uint32_t increment) {
    const uint64_t result = static_cast<uint64_t>(value) + increment;
    return result > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : static_cast<uint32_t>(result);
}

uint32_t WebTimeSeriesBarChart::ceilToStep_(uint32_t value, uint32_t step) {
    if (step == 0) {
        return value;
    }
    const uint64_t result =
        ((static_cast<uint64_t>(value) + step - 1ULL) / step) * step;
    return result > 0xFFFFFFFFULL ? 0xFFFFFFFFUL : static_cast<uint32_t>(result);
}

uint32_t WebTimeSeriesBarChart::floorToStep_(uint32_t value, uint32_t step) {
    if (step == 0) {
        return value;
    }
    return static_cast<uint32_t>((static_cast<uint64_t>(value) / step) * step);
}

int WebTimeSeriesBarChart::countTicksForStep_(uint32_t xMin,
                                              uint32_t xMax,
                                              uint32_t step) {
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

std::vector<uint32_t> WebTimeSeriesBarChart::computeNiceXTicks_(uint32_t xMin,
                                                                 uint32_t xMax) {
    std::vector<uint32_t> ticks;
    if (xMax <= xMin) {
        return ticks;
    }

    static const uint32_t niceSteps[] = {
        1UL, 2UL, 5UL, 10UL, 15UL, 30UL,
        60UL, 2UL * 60UL, 5UL * 60UL, 10UL * 60UL, 15UL * 60UL, 30UL * 60UL,
        60UL * 60UL, 2UL * 60UL * 60UL, 3UL * 60UL * 60UL,
        6UL * 60UL * 60UL, 12UL * 60UL * 60UL,
        24UL * 60UL * 60UL, 2UL * 24UL * 60UL * 60UL,
        3UL * 24UL * 60UL * 60UL, 7UL * 24UL * 60UL * 60UL,
        14UL * 24UL * 60UL * 60UL, 30UL * 24UL * 60UL * 60UL,
        60UL * 24UL * 60UL * 60UL, 90UL * 24UL * 60UL * 60UL,
        180UL * 24UL * 60UL * 60UL, 365UL * 24UL * 60UL * 60UL
    };

    static const int targetTicks = 5;
    uint32_t bestStep = niceSteps[0];
    int bestScore = 1000000;
    int bestCount = 0;

    for (const uint32_t step : niceSteps) {
        const int count = countTicksForStep_(xMin, xMax, step);
        if (count <= 0) {
            continue;
        }

        int score = count > targetTicks ? count - targetTicks : targetTicks - count;
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
    for (uint64_t tick = first; tick <= last; tick += bestStep) {
        ticks.push_back(static_cast<uint32_t>(tick));
        if (ticks.size() >= 12 || bestStep == 0) {
            break;
        }
    }
    return ticks;
}

uint8_t WebTimeSeriesBarChart::decimalsForStep_(float step) {
    if (!isfinite(step) || step <= 0.0f) {
        return 1;
    }
    for (uint8_t decimals = 0; decimals <= 4; ++decimals) {
        const float scale = powf(10.0f, static_cast<float>(decimals));
        const float scaled = step * scale;
        if (fabsf(scaled - roundf(scaled)) < 0.0001f) {
            return decimals;
        }
    }
    return 4;
}

float WebTimeSeriesBarChart::niceYStepCandidate_(float range,
                                                 int exponentOffset,
                                                 uint8_t baseIndex) {
    static const float bases[] = {1.0f, 2.0f, 2.5f, 5.0f};
    const float raw = range / 5.0f;
    if (!isfinite(raw) || raw <= 0.0f) {
        return NAN;
    }
    const int exponent = static_cast<int>(floorf(log10f(raw))) + exponentOffset;
    return bases[baseIndex] * powf(10.0f, static_cast<float>(exponent));
}

int WebTimeSeriesBarChart::countYTicksForStep_(float yMin,
                                               float yMax,
                                               float step) {
    if (!isfinite(yMin) || !isfinite(yMax) || !isfinite(step) ||
        step <= 0.0f || yMax < yMin) {
        return 0;
    }
    const float first = ceilf(yMin / step) * step;
    const float last = floorf(yMax / step) * step;
    if (first > last) {
        return 0;
    }
    return static_cast<int>(floorf((last - first) / step + 0.5f)) + 1;
}

std::vector<float> WebTimeSeriesBarChart::computeNiceYTicks_(float yMin,
                                                              float yMax,
                                                              uint8_t& decimals) {
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

    for (int exponentOffset = -1; exponentOffset <= 2; ++exponentOffset) {
        for (uint8_t baseIndex = 0; baseIndex < 4; ++baseIndex) {
            const float step = niceYStepCandidate_(range, exponentOffset, baseIndex);
            const int count = countYTicksForStep_(yMin, yMax, step);
            if (count <= 0) {
                continue;
            }

            int score = count > targetTicks ? count - targetTicks : targetTicks - count;
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
    ticks.reserve(bestCount > 0 ? bestCount : 5);

    for (float tick = first; tick <= last + bestStep * 0.001f; tick += bestStep) {
        ticks.push_back(fabsf(tick) < bestStep * 0.0001f ? 0.0f : tick);
        if (ticks.size() >= 12) {
            break;
        }
    }
    return ticks;
}

bool WebTimeSeriesBarChart::computeEffectiveYAxis_(const std::vector<Bin>& bins,
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
            yMin -= 0.5f;
            yMax += 0.5f;
        }
        return true;
    }

    if (bins.empty()) {
        yMin = NAN;
        yMax = NAN;
        return false;
    }

    yMin = 0.0f;
    yMax = 0.0f;
    for (const Bin& bin : bins) {
        if (bin.value < yMin) {
            yMin = bin.value;
        }
        if (bin.value > yMax) {
            yMax = bin.value;
        }
    }

    if (yMin == 0.0f && yMax == 0.0f) {
        yMax = 1.0f;
        return false;
    }

    const float padding = (yMax - yMin) * 0.08f;
    if (yMin < 0.0f) {
        yMin -= padding;
    }
    if (yMax > 0.0f) {
        yMax += padding;
    }
    return false;
}

String WebTimeSeriesBarChart::htmlEscape_(const String& in) {
    String out;
    out.reserve(in.length() + 8);
    for (size_t i = 0; i < in.length(); ++i) {
        switch (in[i]) {
            case '&': out += F("&amp;"); break;
            case '<': out += F("&lt;"); break;
            case '>': out += F("&gt;"); break;
            case '"': out += F("&quot;"); break;
            case '\'': out += F("&#39;"); break;
            default: out += in[i]; break;
        }
    }
    return out;
}

String WebTimeSeriesBarChart::jsStringLiteral_(const String& in) {
    return String('"') + jsonEscape(in) + String('"');
}
