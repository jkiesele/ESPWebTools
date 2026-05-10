#include "WebStatus.h"
#include <WebLog.h>
#include <TimeManager.h>
#include <ESP.h>

//----------------------------------------------------------------------------
// JSON status
//----------------------------------------------------------------------------
String WebStatus::getSystemStatus() {
  char buf[128];
  snprintf(buf, sizeof(buf),
    "{\"heap\":%u,\"maxAlloc\":%u,\"heapTotal\":%u,\"tempC\":%.1f}",
    ESP.getFreeHeap() / 1024,
    ESP.getMaxAllocHeap() / 1024,
    ESP.getHeapSize() / 1024,
    temperatureRead()
  );
  return String(buf);
}

//----------------------------------------------------------------------------
// Log text
//----------------------------------------------------------------------------
String WebStatus::createLogText() {
  String txt;
  auto msgs = webLog.getLogMessages();
  auto ts   = webLog.getLogTimestamps();

  for (size_t i = 0; i < msgs.size(); ++i) {
    txt += "<li>"
        + TimeManager::formattedDateAndTime(ts[i])
        + ": " + msgs[i]
        + "</li>\n";
  }

  return txt;
}

//----------------------------------------------------------------------------
// HTML / CSS / JavaScript
//----------------------------------------------------------------------------
static const char STATUS_FRAGMENT[] PROGMEM = R"rawliteral(
<style>
  body {
    font-family: Arial, sans-serif;
  }

  .status-section {
    display: flex;
    flex-wrap: wrap;
    justify-content: center;
    gap: 12px;
    margin-bottom: 20px;
  }

  .status-wrapper {
    width: 260px;
    max-width: 100%;
    font-family: sans-serif;
    font-size: 12px;
    margin: 4px 0;
  }

  .status-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 2px;
  }

  .status-bar-container {
    position: relative;
    width: 100%;
    height: 16px;
    background: #ddd;
    border-radius: 4px;
    overflow: hidden;
  }

  .status-bar-container .fill {
    height: 100%;
    width: 0%;
    background: #777;
    transition: width 0.5s, background 0.5s;
  }

  #logContainer {
    background-color: #f9f9f9;
    border: 1px solid #ccc;
    padding: 10px;
    height: 300px;
    overflow-y: auto;
    white-space: pre-wrap;
    font-size: 12px;
  }
</style>

<script>
  function clamp(v, lo, hi) {
    return Math.min(hi, Math.max(lo, v));
  }

  function percentColor(pct) {
    if (pct >= 60) return '#4caf50';
    if (pct >= 30) return '#ff9800';
    return '#f44336';
  }

  function tempColor(tempC) {
    if (tempC < 0)  return '#777';
    if (tempC < 55) return '#4caf50';
    if (tempC < 70) return '#8bc34a';
    if (tempC < 85) return '#ff9800';
    return '#f44336';
  }

  function setBar(id, pct, color) {
    const bar = document.getElementById(id);
    if (!bar) return;
    bar.style.width = clamp(pct, 0, 100) + '%';
    bar.style.background = color;
  }

  function updateStatus(statusPath) {
    fetch(statusPath)
      .then(r => r.json())
      .then(d => {
        const heapTotal = Math.max(1, parseFloat(d.heapTotal));

        const heapPct = clamp(parseFloat(d.heap) / heapTotal * 100, 0, 100);
        const maxAllocPct = clamp(parseFloat(d.maxAlloc) / heapTotal * 100, 0, 100);

        const tempC = parseFloat(d.tempC);
        const tempPct = clamp((clamp(tempC, 20, 100) - 20) / 80 * 100, 0, 100);

        setBar('heapBar', heapPct, percentColor(heapPct));
        setBar('maxAllocBar', maxAllocPct, percentColor(maxAllocPct));
        setBar('tempBar', tempPct, tempColor(tempC));

        document.getElementById('heapVal').innerText =
          d.heap + ' k / ' + heapPct.toFixed(0) + '%';

        document.getElementById('maxAllocVal').innerText =
          d.maxAlloc + ' k / ' + maxAllocPct.toFixed(0) + '%';

        document.getElementById('tempVal').innerText =
          tempC.toFixed(1) + ' C';
      })
      .catch(e => {});
  }

  function updateLog(logPath) {
    fetch(logPath)
      .then(r => r.text())
      .then(txt => {
        const c = document.getElementById('logContainer');
        if (!c) return;
        c.innerHTML = txt;
        c.scrollTop = c.scrollHeight;
      })
      .catch(e => {});
  }

  function initStatus(sPath, lPath) {
    updateStatus(sPath);
    updateLog(lPath);
    setInterval(() => updateStatus(sPath), 5000);
    setInterval(() => updateLog(lPath), 5000);
  }
</script>

<div class="status-section">
  <div class="status-wrapper">
    <div class="status-row">
      <span>Free Heap</span>
      <span id="heapVal">0 k</span>
    </div>
    <div class="status-bar-container">
      <div id="heapBar" class="fill"></div>
    </div>
  </div>

  <div class="status-wrapper">
    <div class="status-row">
      <span>Max Alloc</span>
      <span id="maxAllocVal">0 k</span>
    </div>
    <div class="status-bar-container">
      <div id="maxAllocBar" class="fill"></div>
    </div>
  </div>

  <div class="status-wrapper">
    <div class="status-row">
      <span>Temperature</span>
      <span id="tempVal">0 C</span>
    </div>
    <div class="status-bar-container">
      <div id="tempBar" class="fill"></div>
    </div>
  </div>
</div>

<div id="logContainer"></div>

<script>
  initStatus('{{STATUS_PATH}}', '{{LOG_PATH}}');
</script>
)rawliteral";


String WebStatus::createSystemStatHtmlFragment(const char* statusPath, const char* logPath) {
  String frag;
  frag.reserve(sizeof(STATUS_FRAGMENT));
  frag = FPSTR(STATUS_FRAGMENT);
  frag.replace("{{STATUS_PATH}}", statusPath);
  frag.replace("{{LOG_PATH}}", logPath);
  return frag;
}