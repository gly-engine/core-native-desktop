#include <uv.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include "picohttpparser.h"
#include "gecnd.h"

#define PORT 3000
#define BUF_SIZE 8192
#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef struct {
    uv_tcp_t handle;
    int upgraded;
    size_t read_len;
    char buffer[BUF_SIZE];
} client_t;

static const char html_response[] =
"HTTP/1.1 200 OK\r\n"
"Content-Type: text/html\r\n"
"Connection: close\r\n"
"\r\n"
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
"<div class=\"remote\">"
"<div class=\"btn power\">Power</div>"
"<div class=\"btn green\">Green</div>"
"<div class=\"btn yellow\">Yellow</div>"
"<div class=\"btn blue\">Blue</div>"
"<div class=\"btn up\">↑</div>"
"<div class=\"btn left\">←</div>"
"<div class=\"btn red\">OK</div>"
"<div class=\"btn right\">→</div>"
"<div class=\"btn down\">↓</div>"
"<div class=\"btn volup\">Vol +</div>"
"<div class=\"btn voldown\">Vol −</div>"
"<div class=\"btn chup\">Ch +</div>"
"<div class=\"btn chdown\">Ch −</div>"
"</div>"
"<script>"
"const ip=location.hostname;"
"const port=3000;"
"const socket=new WebSocket('ws://'+ip+':'+port);"
"socket.onopen=()=>console.log('Conectado');"
"socket.onclose=()=>console.log('Desconectado');"
"socket.onerror=e=>console.log('Erro',e);"
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

static void sha1(const uint8_t *data,size_t len,uint8_t out[20]){
    uint32_t h0=0x67452301,h1=0xEFCDAB89,h2=0x98BADCFE,h3=0x10325476,h4=0xC3D2E1F0;
    uint64_t bits=len*8;
    size_t new_len=((len+8)/64+1)*64;
    uint8_t *msg=calloc(1,new_len);
    memcpy(msg,data,len);
    msg[len]=0x80;
    for(int i=0;i<8;i++) msg[new_len-1-i]=(bits>>(8*i))&0xff;
    for(size_t off=0;off<new_len;off+=64){
        uint32_t w[80];
        for(int i=0;i<16;i++)
            w[i]=(msg[off+4*i]<<24)|(msg[off+4*i+1]<<16)|(msg[off+4*i+2]<<8)|(msg[off+4*i+3]);
        for(int i=16;i<80;i++){
            uint32_t v=w[i-3]^w[i-8]^w[i-14]^w[i-16];
            w[i]=(v<<1)|(v>>31);
        }
        uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
        for(int i=0;i<80;i++){
            uint32_t f,k;
            if(i<20){f=(b&c)|((~b)&d);k=0x5A827999;}
            else if(i<40){f=b^c^d;k=0x6ED9EBA1;}
            else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8F1BBCDC;}
            else{f=b^c^d;k=0xCA62C1D6;}
            uint32_t temp=((a<<5)|(a>>27))+f+e+k+w[i];
            e=d;d=c;c=(b<<30)|(b>>2);b=a;a=temp;
        }
        h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;
    }
    free(msg);
    out[0]=h0>>24;out[1]=h0>>16;out[2]=h0>>8;out[3]=h0;
    out[4]=h1>>24;out[5]=h1>>16;out[6]=h1>>8;out[7]=h1;
    out[8]=h2>>24;out[9]=h2>>16;out[10]=h2>>8;out[11]=h2;
    out[12]=h3>>24;out[13]=h3>>16;out[14]=h3>>8;out[15]=h3;
    out[16]=h4>>24;out[17]=h4>>16;out[18]=h4>>8;out[19]=h4;
}

static void base64(const uint8_t *in,size_t len,char *out){
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t j=0;
    for(size_t i=0;i<len;i+=3){
        uint32_t v=in[i]<<16;
        if(i+1<len)v|=in[i+1]<<8;
        if(i+2<len)v|=in[i+2];
        out[j++]=t[(v>>18)&63];
        out[j++]=t[(v>>12)&63];
        out[j++]=(i+1<len)?t[(v>>6)&63]:'=';
        out[j++]=(i+2<len)?t[v&63]:'=';
    }
    out[j]=0;
}

static void after_write(uv_write_t* req,int status){
    if(req->data) free(req->data);
    free(req);
}

static void safe_close(uv_handle_t* h){
    client_t* c=h->data;
    free(c);
}

static void ws_send(client_t* c,const char*msg,size_t len){
    if(len>125) return;
    size_t sz=2+len;
    char*frame=malloc(sz);
    frame[0]=0x81;
    frame[1]=len;
    memcpy(frame+2,msg,len);
    uv_buf_t b=uv_buf_init(frame,sz);
    uv_write_t*wr=malloc(sizeof(*wr));
    wr->data=frame;
    uv_write(wr,(uv_stream_t*)&c->handle,&b,1,after_write);
}

static void handle_ws(client_t* c){
    if(c->read_len<6) return;
    uint8_t* p=(uint8_t*)c->buffer;
    size_t l=p[1]&127;
    if(c->read_len < 6+l) return;
    uint8_t* mask=p+2;
    uint8_t* pl=p+6;
    for(size_t i=0;i<l;i++) pl[i]^=mask[i%4];

    if (l > 1 && (pl[0] == '+' || pl[0] == '-')) {
        char keyname[128];
        size_t name_len = l - 1;
        if (name_len < sizeof(keyname)) {
            memcpy(keyname, pl + 1, name_len);
            keyname[name_len] = '\0';
            bool pressed = (pl[0] == '+');

            const char* final_key = keyname;
            if (strcmp(keyname, "red") == 0) final_key = "a";
            else if (strcmp(keyname, "green") == 0) final_key = "b";
            else if (strcmp(keyname, "yellow") == 0) final_key = "c";
            else if (strcmp(keyname, "blue") == 0) final_key = "d";

            gecnd_set_btn_state(gecnd_get_root(), final_key, pressed);
        }
    }

    ws_send(c,(char*)pl,l);
    c->read_len=0;
}

static void alloc_cb(uv_handle_t* h,size_t s,uv_buf_t* b){
    client_t* c=h->data;
    b->base=c->buffer + c->read_len;
    b->len=BUF_SIZE - c->read_len;
}

static void on_read(uv_stream_t* s,ssize_t n,const uv_buf_t* b){
    client_t* c=s->data;
    if(n<=0){
        uv_read_stop(s);
        uv_close((uv_handle_t*)s,safe_close);
        return;
    }

    c->read_len += n;

    if(!c->upgraded){
        const char*method,*path;int minor;
        struct phr_header headers[32];
        size_t mlen,plen,nh=32;
        int r=phr_parse_request(c->buffer,c->read_len,&method,&mlen,&path,&plen,&minor,headers,&nh,0);
        if(r>0){
            int ws=0;
            char key[128]={0};
            for(size_t i=0;i<nh;i++){
                if(strncasecmp(headers[i].name,"Upgrade",headers[i].name_len)==0) ws=1;
                if(strncasecmp(headers[i].name,"Sec-WebSocket-Key",headers[i].name_len)==0){
                    memcpy(key,headers[i].value,headers[i].value_len);
                    key[headers[i].value_len]=0;
                }
            }
            if(ws){
                char src[256];
                snprintf(src,sizeof(src),"%s%s",key,WS_GUID);
                uint8_t hash[20];
                sha1((uint8_t*)src,strlen(src),hash);
                char b64[32];
                base64(hash,20,b64);
                char resp[256];
                int len=snprintf(resp,sizeof(resp),
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\n"
                    "Connection: Upgrade\r\n"
                    "Sec-WebSocket-Accept: %s\r\n\r\n",b64);
                char*out=malloc(len);
                memcpy(out,resp,len);
                uv_buf_t wb=uv_buf_init(out,len);
                uv_write_t*wr=malloc(sizeof(*wr));
                wr->data=out;
                uv_write(wr,s,&wb,1,after_write);
                c->upgraded=1;
                c->read_len=0;
                printf("[RC] New WebSocket client connected\n");
            }else{
                size_t len=strlen(html_response);
                char*out=malloc(len);
                memcpy(out,html_response,len);
                uv_buf_t wb=uv_buf_init(out,len);
                uv_write_t*wr=malloc(sizeof(*wr));
                wr->data=out;
                uv_write(wr,s,&wb,1,after_write);
                uv_read_stop(s);
                uv_close((uv_handle_t*)s,safe_close);
            }
        }
    }else{
        handle_ws(c);
    }
}

static void on_conn(uv_stream_t* srv,int status){
    if(status<0) return;
    client_t* c=calloc(1,sizeof(*c));
    uv_tcp_init(srv->loop,&c->handle);
    c->handle.data=c;
    if(uv_accept(srv,(uv_stream_t*)&c->handle)==0)
        uv_read_start((uv_stream_t*)&c->handle,alloc_cb,on_read);
    else{
        uv_close((uv_handle_t*)&c->handle,safe_close);
    }
}

void main_rc(uv_loop_t* loop) {
    static uv_tcp_t server;
    uv_tcp_init(loop, &server);

    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", PORT, &addr);
    uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);

    uv_listen((uv_stream_t*)&server, 128, on_conn);
}