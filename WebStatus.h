#pragma once
#include <Arduino.h>

namespace WebStatus {
  // JSON + Log text
  String getSystemStatus();
  String createLogText();

  // HTML fragment: status bars + live-updating log
  //    statusPath, logPath: which URLs to fetch for JSON and log
  String createSystemStatHtmlFragment(const char* statusPath = "/status",
                                      const char* logPath    = "/log");
}