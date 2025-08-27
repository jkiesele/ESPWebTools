
#include "WebButton.h"
#include "WebAuthPlugin.h" // for auth check
#include "LoggingBase.h"


String WebButton::createHtmlFragment() const
{
    String html;
    html.reserve(256);
    html += "<button id='";  html += id();  html += "_btn' "
            "style='padding:6px 12px;margin:4px'>"
            + jsonEscape(label_) + "</button>\n"
            "<script>(function(){\n"
            " const btn=document.getElementById('";  html += id();
    html += "_btn');\n"
            " let last=0;\n"
            " btn.addEventListener('click', async()=>{\n"
            "   const now=Date.now();\n";
    if (cooldownMs_)
        html += "   if(now-last<" + String(cooldownMs_) + ")return;\n";
    html += "   last=now; btn.disabled=true;\n"
            "   try{await fetch('";  html += handle();  html += "');}"
            " catch(e){}\n";
    if (cooldownMs_)
        html += "   setTimeout(()=>btn.disabled=false,"+String(cooldownMs_)+");\n";
    else
        html += "   btn.disabled=false;\n";
    html += " });})();</script>\n";
    return html;
}
/** GET handler – fires callback & returns a JSON ack */
String WebButton::routeText() const 
{
    const uint32_t now = millis();
    if (onClick_ && now - lastClick_ >= cooldownMs_) {
        if (WebAuthPlugin::instance().require()) {
            onClick_();                 // *****  user action  *****
            lastClick_ = now;
            ++clickCounter_;
        }
        else{
            gLogger->println("WebButton: " + id() + " click blocked: not authenticated");
        }
    }
    return "{\"id\":\"" + id() +
           "\",\"clicks\":" + String(clickCounter_) + "}";
}