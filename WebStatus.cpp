#include "WebStatus.h"
#include <WebLog.h>
#include <TimeManager.h>
#include <ESP.h>        // ESP.getFreeHeap(), etc.

//----------------------------------------------------------------------------
// JSON status
//----------------------------------------------------------------------------
String WebStatus::getSystemStatus() {
  char buf[128];
  snprintf(buf, sizeof(buf),
    "{\"heap\":%u,\"maxAlloc\":%u,\"heapTotal\":%u,\"tempC\":%.1f}",
    ESP.getFreeHeap()/1024,
    ESP.getMaxAllocHeap()/1024,
    ESP.getHeapSize()/1024,
    temperatureRead()   // or however you read temp
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

  body{font-family:Arial,sans-serif;}

  /* add this: */
   .status-section {
     display: flex;            /* lay children in a row */
     justify-content: center;  /* center the group in the page */
     gap: 30px;                /* space between each wrapper */
     margin-bottom: 20px;      /* if you want some breathing room below */
   }

  .status-wrapper {
    display: flex;
    flex-direction: column;   /* stack children vertically */
    align-items: center;      /* center them horizontally */
    margin: 0 15px;
    font-size: 12px;
  }
    
  
  .status-bar-container {
    position: relative;      /* allow its child to be absolutely positioned */
    width: 20px;
    height: 100px;
    background: #ddd;
    border: 1px solid #999;
    margin-bottom: 10px;
    /* remove the flex rules here */
  }
  
  .status-bar-container .fill {
    position: absolute;  /* stick the fill to the container’s bottom edge */
    bottom: 0;
    left:   0;
    width: 100%;
    background: #3498db;
    transition: height 0.5s;
  }
  
  /* Temperature bar gets its own color */
  .status-bar-container.temp .fill {
    background: #e67e22;
  }
  
  /* Label and value */
  .status-label,
  .status-value {
    text-align: center;
  }

  /* Log window styling */
  #logContainer {
    background-color: #f9f9f9;
    border: 1px solid #ccc;
    padding: 10px;
    height: 300px;
    overflow-y: auto;
    white-space: pre-wrap;
  }
</style>
<script>
  function updateStatus(statusPath) {
    fetch(statusPath)
      .then(r=>r.json())
      .then(d=>{
        document.getElementById('heapBar').style.height      = (d.heap/d.heapTotal*100)+'%';
        document.getElementById('maxAllocBar').style.height = (d.maxAlloc/d.heapTotal*100)+'%';
        let t = ((Math.min(100,Math.max(20,d.tempC)) - 20)/80*100)+'%';
        document.getElementById('tempBar').style.height     = t;
        document.getElementById('heapVal').innerText      = d.heap;
        document.getElementById('maxAllocVal').innerText  = d.maxAlloc;
        document.getElementById('tempVal').innerText      = d.tempC.toFixed(1);
      });
  }
  function updateLog(logPath) {
    fetch(logPath)
      .then(r=>r.text())
      .then(txt=>{
        let c = document.getElementById('logContainer');
        c.innerHTML = txt;
        c.scrollTop = c.scrollHeight;
      });
  }
  function initStatus(sPath, lPath) {
    updateStatus(sPath);
    updateLog(lPath);
    setInterval(()=>updateStatus(sPath),5000);
    setInterval(()=>updateLog(lPath),5000);
  }
</script>

<!-- call it straight away -->
<script>
  initStatus('{{STATUS_PATH}}','{{LOG_PATH}}');
</script>

<div class="status-section">
  <div class="status-wrapper">
    <div class="status-bar-container">
      <div id="heapBar" class="fill"></div>
    </div>
    <div class="status-label">Free Heap [k]</div>
    <div id="heapVal" class="status-value">0</div>
  </div>

  <div class="status-wrapper">
    <div class="status-bar-container">
      <div id="maxAllocBar" class="fill"></div>
    </div>
    <div class="status-label">Max Alloc [k]</div>
    <div id="maxAllocVal" class="status-value">0</div>
  </div>

  <div class="status-wrapper">
    <div class="status-bar-container temp">
      <div id="tempBar" class="fill"></div>
    </div>
    <div class="status-label">Temp [°C]</div>
    <div id="tempVal" class="status-value">0</div>
  </div>
</div>
<br>

<div id="logContainer"></div>
)rawliteral";


String WebStatus::createSystemStatHtmlFragment(const char* statusPath, const char* logPath) {
  // Copy from PROGMEM, then patch in the real URLs:
  String frag;
  frag.reserve(sizeof(STATUS_FRAGMENT));
  frag = FPSTR(STATUS_FRAGMENT);
  frag.replace("{{STATUS_PATH}}", statusPath);
  frag.replace("{{LOG_PATH}}",    logPath);
  return frag;
}