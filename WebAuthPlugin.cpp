// WebAuthPlugin.cpp
#include "WebAuthPlugin.h"



WebAuthPlugin& WebAuthPlugin::instance(){ static WebAuthPlugin inst; return inst; }

bool WebAuthPlugin::isWhitelistedPath_(const String& path,
                                       std::initializer_list<const char*> extra) const {
  if (path.startsWith("/login") || path.startsWith("/logout") ||
      path.startsWith("/static/") || path == "/favicon.ico") return true;
  for (auto p: extra) if (path.startsWith(p)) return true;
  return false;
}

String WebAuthPlugin::parseCookieToken_(WebServer& srv, const char* cookieName){
  String cookie = srv.header("Cookie");
  int idx = cookie.indexOf(cookieName);
  if (idx < 0) return String();
  int eq = cookie.indexOf('=', idx); if (eq < 0) return String();
  int sc = cookie.indexOf(';', eq+1);
  String tok = cookie.substring(eq+1, sc>=0?sc:cookie.length());
  tok.trim();
  if (tok.startsWith("\"") && tok.endsWith("\"") && tok.length() >= 2) {
    tok = tok.substring(1, tok.length()-1);
  }
  return tok;
}
void WebAuthPlugin::sendRedirect_(WebServer& srv, const String& to){
  srv.sendHeader("Location", to); srv.send(302, "text/plain", "Redirecting...");
}
String WebAuthPlugin::htmlLogin_(){
  return "<!doctype html><meta name=viewport content='width=device-width, initial-scale=1'/>"
         "<style>body{font-family:sans-serif;max-width:420px;margin:4em auto;padding:0 1em}"
         "form{display:grid;gap:.6em}label{font-size:.9em}input{padding:.5em}"
         "button{padding:.6em 1em}</style>"
         "<h3>Login</h3>"
         "<form method='POST' action='/login'>"
         "<label>User<br/><input type='text' name='u' autofocus></label>"
         "<label>Password<br/><input type='password' name='p'></label>"
         "<input type='hidden' name='next' id='next'>"
         "<button type='submit'>Login</button></form>"
         "<script>const qp=new URLSearchParams(location.search);"
         "document.getElementById('next').value=qp.get('next')||'/'</script>";
}

void WebAuthPlugin::install(WebServer& srv){
  if (installed_) return;
  server_ = &srv;
  installed_ = true;

  static const char* headerKeys[] = { "Cookie" };
  server_->collectHeaders(headerKeys, 1);

  // GET /login
  server_->on("/login", HTTP_GET, [this]{
    String tok = parseCookieToken_(*server_, kCookieName_);
    if (auth_.validate(tok)) {
      String next = server_->hasArg("next") ? server_->arg("next") : "/";
      if (next.length() == 0 || next[0] != '/') next = "/";
      server_->sendHeader("Cache-Control", "no-store");
      // the next logic is not working yet, so redirect to root for now - maybe fix in future
      sendRedirect_(*server_, "/"); return;
    }
    server_->send(200, "text/html", htmlLogin_());
  });

  // POST /login
  server_->on("/login", HTTP_POST, [this]{
    const String u = server_->arg("u");
    const String p = server_->arg("p");

    String next = server_->hasArg("next") ? server_->arg("next") : "/";
    if (next.length() == 0 || next[0] != '/') next = "/";

    if (u == secret::webUser && p == secret::webPass) {
      String tok = auth_.createSession();
      server_->sendHeader("Set-Cookie",
                        String(kCookieName_)+"="+tok+"; HttpOnly; SameSite=Lax; Path=/");
      // the next logic is not working yet, so redirect to root for now - maybe fix in future
      sendRedirect_(*server_, "/");
    } else {
      delay(400);
      server_->sendHeader("Cache-Control", "no-store");
      server_->send(401, "text/html",
        "<p>Wrong user or password.</p><p><a href='/login'>Try again</a></p>");
    }
  });

  // GET /logout
  server_->on("/logout", HTTP_GET, [this]{
    auth_.clear();
    server_->sendHeader("Set-Cookie", String(kCookieName_) + "=; Max-Age=0; Path=/");
    sendRedirect_(*server_, "/login");
  });
}

bool WebAuthPlugin::require(std::initializer_list<const char*> extraWhitelist){

  if (!installed_ || !enabled_) return true;
  if(!server_) return true; // should not happen

  const String path = server_->uri();
  const bool isPost = (server_->method() == HTTP_POST);

  if (postOnlyLockdown_) {
    if (isWhitelistedPath_(path, extraWhitelist)) return true;
    if (!isPost) return true;  // only protect writes
  } else {
    if (isWhitelistedPath_(path, extraWhitelist)) return true;
  }

  String tok = parseCookieToken_(*server_, kCookieName_);
  if (auth_.validate(tok)) return true;

  sendRedirect_(*server_, "/login?next=" + path);
  return false;
}