
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "gecnd.h"
#include "gdweb.h"

static const char s_html[] =
"<!DOCTYPE html>"
"<html lang=\"en\">"
"<head>"
"<meta charset=\"UTF-8\"/>"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"/>"
"<title>Remote Control</title>"
"<style>"
"*{box-sizing:border-box;}"
"body{margin:0;background:#111;font-family:sans-serif;display:flex;justify-content:center;padding:20px;}"
".remote{display:grid;grid-template-columns:repeat(5,80px);gap:10px;background:#222;padding:20px;border-radius:12px;max-width:480px;width:100%;}"
".btn{background:#444;color:white;text-align:center;padding:16px;border-radius:8px;font-size:16px;user-select:none;transition:transform .1s,background .2s;display:flex;align-items:center;justify-content:center;}"
".btn:active{transform:scale(.95);background:#666;}"
".power{background:crimson;grid-column:5/5;}"
".green{background:green;grid-column:2/2;}"
".yellow{background:yellow;color:black;}"
".blue{background:dodgerblue;}"
".up{grid-column:3;grid-row:3;}"
".left{grid-column:2;grid-row:4;}"
".red{background:red;grid-column:3;grid-row:4;}"
".right{grid-column:4;grid-row:4;}"
".down{grid-column:3;grid-row:5;}"
".volup{grid-column:1/3;grid-row:6;}"
".voldown{grid-column:1/3;grid-row:7;}"
".chup{grid-column:4/6;grid-row:6;}"
".chdown{grid-column:4/6;grid-row:7;}"
"@media(max-width:480px){body{padding:0;}.remote{grid-template-columns:repeat(5,20%);margin:0;padding:0;}}"
"</style>"
"</head>"
"<body>"
"<div id=\"status\" style=\"position:fixed;top:8px;right:8px;padding:4px 10px;border-radius:12px;"
     "background:#f00;color:#fff;font-size:12px;font-family:sans-serif\">WS: desconectado</div>"
"<div class=\"remote\">"
"<div class=\"btn power\">Power</div>"
"<div class=\"btn green\">Green</div>"
"<div class=\"btn yellow\">Yellow</div>"
"<div class=\"btn blue\">Blue</div>"
"<div class=\"btn up\">\xe2\x86\x91</div>"
"<div class=\"btn left\">\xe2\x86\x90</div>"
"<div class=\"btn red\">OK</div>"
"<div class=\"btn right\">\xe2\x86\x92</div>"
"<div class=\"btn down\">\xe2\x86\x93</div>"
"<div class=\"btn volup\">Vol +</div>"
"<div class=\"btn voldown\">Vol \xe2\x88\x92</div>"
"<div class=\"btn chup\">Ch +</div>"
"<div class=\"btn chdown\">Ch \xe2\x88\x92</div>"
"</div>"
"<script>"
"const st=document.getElementById('status');"
"const socket=new WebSocket('ws://'+location.host+'/rc', 'ws');"
"socket.onopen=()=>{st.textContent='WS: conectado';st.style.background='#0a0';console.log('Conectado');};"
"socket.onclose=()=>{st.textContent='WS: desconectado';st.style.background='#f00';console.log('Desconectado');};"
"socket.onerror=e=>{st.textContent='WS: erro';st.style.background='#f80';console.log('Erro',e);};"
"const buttons=document.querySelectorAll('.btn');"
"const send=m=>{if(socket.readyState===1){socket.send(m);}};"
"buttons.forEach(btn=>{"
"const cls=Array.from(btn.classList).filter(c=>c!=='btn');"
"const name=cls[0]||'unknown';"
"let pressed=false;"
"btn.addEventListener('mousedown',()=>{if(!pressed){pressed=true;send('+'+name);}});"
"btn.addEventListener('touchstart',e=>{e.preventDefault();if(!pressed){pressed=true;send('+'+name);}},{passive:false});"
"const release=()=>{if(pressed){pressed=false;send('-'+name);}};"
"btn.addEventListener('mouseup',release);"
"btn.addEventListener('mouseleave',release);"
"btn.addEventListener('touchend',release);"
"btn.addEventListener('touchcancel',release);"
"});"
"</script>"
"</body>"
"</html>";

static void http_rc(const gdweb_http_req_t *req)
{
    gdweb_value_t ct = { .str = "text/html; charset=utf-8" };
    gdweb_control_server()->http(req->id, GDWEB_HTTP_CONTENT_TYPE, &ct);
    gdweb_control_server()->send(req->id, s_html, sizeof(s_html) - 1);
}

static void ws_rc(const gdweb_ws_req_t *req)
{
    if (req->event == GDWEB_WS_OPEN)  { *req->usr = 0; return; }
    if (req->event == GDWEB_WS_CLOSE) { gamely_daemon_input_reset_port((int)(intptr_t)*req->usr); return; }
    if (req->event != GDWEB_WS_MESSAGE || req->len < 1) return;

    const char *data = req->data;
    size_t      len  = req->len;

    char tmp[16] = {0};
    memcpy(tmp, data, len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1);

    int port;
    if (sscanf(tmp, "%d", &port) == 1) {
        int old = (int)(intptr_t)*req->usr;
        if (port != old) gamely_daemon_input_reset_port(old);
        *req->usr = (void *)(intptr_t)port;
        return;
    }

    if (len < 2 || (data[0] != '+' && data[0] != '-')) return;

    bool pressed = (data[0] == '+');
    char keyname[128];
    size_t name_len = len - 1;
    if (name_len >= sizeof(keyname)) return;
    memcpy(keyname, data + 1, name_len);
    keyname[name_len] = '\0';

    const char *key = keyname;
    if      (strcmp(keyname, "red")    == 0) key = "a";
    else if (strcmp(keyname, "green")  == 0) key = "b";
    else if (strcmp(keyname, "yellow") == 0) key = "c";
    else if (strcmp(keyname, "blue")   == 0) key = "d";

    gamely_daemon_input_push_name(key, pressed, (int)(intptr_t)*req->usr, 0);
}

__attribute__((constructor))
static void register_rc_routes(void)
{
    gecnd_registry("set", "web_http_route:rc", (void *)http_rc, NULL);
    gecnd_registry("set", "web_ws_route:rc",   (void *)ws_rc,   NULL);
}

