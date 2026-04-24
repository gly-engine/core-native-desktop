#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

#include "gecnd.h"

static char s_iface[64];

static void sanitize_shell(char *dst, const char *src, size_t max) {
    size_t i = 0;
    for (; *src && i < max - 1; src++) {
        unsigned char c = (unsigned char)*src;
        if (c == '"' || c == '\\' || c == '$' || c == '`' ||
            c == '\'' || c == '\n' || c == '\r')
            continue;
        dst[i++] = *src;
    }
    dst[i] = '\0';
}

static void json_escape(char *dst, const char *src, size_t max) {
    size_t i = 0;
    for (; *src && i + 2 < max; src++) {
        switch ((unsigned char)*src) {
            case '"':  dst[i++] = '\\'; dst[i++] = '"';  break;
            case '\\': dst[i++] = '\\'; dst[i++] = '\\'; break;
            case '\n': dst[i++] = '\\'; dst[i++] = 'n';  break;
            case '\r': dst[i++] = '\\'; dst[i++] = 'r';  break;
            case '\t': dst[i++] = '\\'; dst[i++] = 't';  break;
            default:
                if ((unsigned char)*src >= 0x20) dst[i++] = *src;
        }
    }
    dst[i] = '\0';
}

static void wpa_cli_run(const char *args, char *out, size_t out_sz) {
    char cmd[512];
    if (s_iface[0])
        snprintf(cmd, sizeof(cmd), "wpa_cli -i %.32s %s 2>&1", s_iface, args);
    else
        snprintf(cmd, sizeof(cmd), "wpa_cli %s 2>&1", args);
    FILE *fp = popen(cmd, "r");
    if (!fp) { if (out) out[0] = '\0'; return; }
    size_t n = (out && out_sz) ? fread(out, 1, out_sz - 1, fp) : 0;
    pclose(fp);
    if (out) {
        out[n] = '\0';
        while (n > 0 && (out[n-1] == '\n' || out[n-1] == '\r' || out[n-1] == ' '))
            out[--n] = '\0';
    }
}

static bool wpa_out_ok(const char *out) {
    const char *p = out;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (strncmp(p, "OK",   2) == 0) return true;
        if (strncmp(p, "FAIL", 4) == 0) return false;
        p = strchr(p, '\n');
        if (!p) break;
        p++;
    }
    return false;
}

static bool wpa_cli_ok(const char *args, char *detail, size_t detail_sz) {
    char out[256];
    wpa_cli_run(args, out, sizeof(out));
    bool ok = wpa_out_ok(out);
    if (!ok && detail && detail_sz)
        json_escape(detail, out, detail_sz);
    return ok;
}

static void send_json(const gly_http_req_t *req, int status, const char *json) {
    gamely_daemon_webserver_http_send(req->id, status,
        "application/json", json, strlen(json));
}

static void send_error(const gly_http_req_t *req, int status,
                       const char *msg, const char *detail) {
    char json[512];
    if (detail && *detail)
        snprintf(json, sizeof(json),
            "{\"error\":\"%s\",\"detail\":\"%s\"}", msg, detail);
    else
        snprintf(json, sizeof(json), "{\"error\":\"%s\"}", msg);
    send_json(req, status, json);
}

static bool get_param(const char *path, const char *key, char *val, size_t val_sz) {
    const char *q = strchr(path, '?');
    if (!q) return false;
    q++;
    size_t klen = strlen(key);
    while (*q) {
        if (strncmp(q, key, klen) == 0 && q[klen] == '=') {
            q += klen + 1;
            size_t i = 0;
            while (*q && *q != '&' && i < val_sz - 1) {
                if (*q == '%' && isxdigit((unsigned char)q[1]) && isxdigit((unsigned char)q[2])) {
                    char hex[3] = { q[1], q[2], 0 };
                    val[i++] = (char)strtol(hex, NULL, 16);
                    q += 3;
                } else {
                    val[i++] = (*q == '+') ? ' ' : *q;
                    q++;
                }
            }
            val[i] = '\0';
            return true;
        }
        while (*q && *q != '&') q++;
        if (*q == '&') q++;
    }
    return false;
}

static bool do_connect(const char *ssid, const char *password,
                       char *detail, size_t detail_sz) {
    char st[1024] = {0};
    wpa_cli_run("status", st, sizeof(st));
    char ssid_line[192];
    snprintf(ssid_line, sizeof(ssid_line), "ssid=%s", ssid);
    if (strstr(st, "wpa_state=COMPLETED") && strstr(st, ssid_line))
        return true;

    char safe_ssid[128], safe_pass[128];
    sanitize_shell(safe_ssid, ssid, sizeof(safe_ssid));
    sanitize_shell(safe_pass, password ? password : "", sizeof(safe_pass));

    char out[256], args[512];

    wpa_cli_run("add_network", out, sizeof(out));
    if (strstr(out, "FAIL")) {
        if (detail && detail_sz) json_escape(detail, out, detail_sz);
        return false;
    }

    int net_id = -1;
    for (char *p = out; *p; ) {
        while (*p == ' ' || *p == '\t' || *p == '\r') p++;
        if (isdigit((unsigned char)*p)) { net_id = atoi(p); break; }
        p = strchr(p, '\n');
        if (!p) break;
        p++;
    }
    if (net_id < 0) {
        if (detail && detail_sz) json_escape(detail, out, detail_sz);
        return false;
    }

    snprintf(args, sizeof(args),
        "set_network %d ssid \"\\\"%.64s\\\"\"", net_id, safe_ssid);
    if (!wpa_cli_ok(args, detail, detail_sz)) goto fail;

    if (safe_pass[0]) {
        snprintf(args, sizeof(args),
            "set_network %d psk \"\\\"%.63s\\\"\"", net_id, safe_pass);
    } else {
        snprintf(args, sizeof(args), "set_network %d key_mgmt NONE", net_id);
    }
    if (!wpa_cli_ok(args, detail, detail_sz)) goto fail;

    snprintf(args, sizeof(args), "select_network %d", net_id);
    if (!wpa_cli_ok(args, detail, detail_sz)) goto fail;

    wpa_cli_ok("save_config", NULL, 0);
    return true;

fail:
    snprintf(args, sizeof(args), "remove_network %d", net_id);
    wpa_cli_ok(args, NULL, 0);
    return false;
}

static bool parse_bss_line(char *line, char *json_out, size_t json_sz) {
    char *t1 = strchr(line, '\t');
    if (!t1 || (t1 - line) != 17) return false;

    char *freq  = t1 + 1;
    char *t2 = strchr(freq,  '\t'); if (!t2) return false;
    char *sig   = t2 + 1;
    char *t3 = strchr(sig,   '\t'); if (!t3) return false;
    char *flags = t3 + 1;
    char *t4 = strchr(flags, '\t'); if (!t4) return false;
    char *ssid  = t4 + 1;

    *t1 = *t2 = *t3 = *t4 = '\0';

    char essid[256], eflags[256];
    json_escape(essid,  ssid,  sizeof(essid));
    json_escape(eflags, flags, sizeof(eflags));

    snprintf(json_out, json_sz,
        "{\"bssid\":\"%s\",\"freq\":%d,\"signal\":%d,\"flags\":\"%s\",\"ssid\":\"%s\"}",
        line, atoi(freq), atoi(sig), eflags, essid);
    return true;
}

static void http_wifi_status(const gly_http_req_t *req) {
    char raw[2048] = {0};
    wpa_cli_run("status", raw, sizeof(raw));

    char json[4096];
    int  jlen = snprintf(json, sizeof(json), "{");
    bool first = true;
    char *p = raw;

    while (*p) {
        char *nl = strchr(p, '\n');
        if (nl) *nl = '\0';
        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char ekey[128], eval[512];
            json_escape(ekey, p,    sizeof(ekey));
            json_escape(eval, eq+1, sizeof(eval));
            if (jlen < (int)sizeof(json) - 16)
                jlen += snprintf(json + jlen, sizeof(json) - jlen,
                    "%s\"%s\":\"%s\"", first ? "" : ",", ekey, eval);
            first = false;
            *eq = '=';
        }
        if (!nl) break;
        p = nl + 1;
    }
    snprintf(json + jlen, sizeof(json) - jlen, "}");
    send_json(req, 200, json);
}

static void http_wifi_scan(const gly_http_req_t *req) {
    char out[256] = {0};
    wpa_cli_run("scan", out, sizeof(out));
    if (wpa_out_ok(out) || strstr(out, "FAIL-BUSY")) {
        send_json(req, 200, "{\"ok\":true}");
        return;
    }
    char detail[256];
    json_escape(detail, out, sizeof(detail));
    send_error(req, 500, "scan failed", detail);
}

static void http_wifi_scan_results(const gly_http_req_t *req) {
    char raw[8192] = {0};
    wpa_cli_run("scan_results", raw, sizeof(raw));

    char json[8192];
    int  jlen = snprintf(json, sizeof(json), "[");
    bool first = true;

    char *line = raw;
    while (*line) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';

        char entry[512];
        if (parse_bss_line(line, entry, sizeof(entry))) {
            if (jlen < (int)sizeof(json) - (int)sizeof(entry))
                jlen += snprintf(json + jlen, sizeof(json) - jlen,
                    "%s%s", first ? "" : ",", entry);
            first = false;
        }

        if (!nl) break;
        line = nl + 1;
    }
    snprintf(json + jlen, sizeof(json) - jlen, "]");
    send_json(req, 200, json);
}

static void http_wifi_connect(const gly_http_req_t *req) {
    char ssid[128] = {0}, pass[128] = {0};
    if (!get_param(req->path, "ssid", ssid, sizeof(ssid))) {
        send_json(req, 400, "{\"error\":\"missing ssid\"}");
        return;
    }
    get_param(req->path, "password", pass, sizeof(pass));
    char detail[256] = {0};
    bool ok = do_connect(ssid, pass, detail, sizeof(detail));
    if (ok) send_json(req, 200, "{\"ok\":true}");
    else    send_error(req, 500, "connect failed", detail);
}

static void http_wifi_disconnect(const gly_http_req_t *req) {
    char detail[256] = {0};
    bool ok = wpa_cli_ok("disconnect", detail, sizeof(detail));
    if (ok) send_json(req, 200, "{\"ok\":true}");
    else    send_error(req, 500, "disconnect failed", detail);
}

void coreopen_wpa_gecnd(void) {
    const char *iface = getenv("wifi_iface");
    if (iface) snprintf(s_iface, sizeof(s_iface), "%s", iface);
    else s_iface[0] = '\0';

    const char *supl = getenv("wifi_supplicant");
    if (supl) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "wpa_supplicant %.500s 2>/dev/null", supl);
        (void)system(cmd);
    }

    const char *ssid = getenv("wifi_ssid");
    if (ssid) do_connect(ssid, getenv("wifi_password"), NULL, 0);

    gamely_daemon_webloop_route_http("/wifi/scan",         http_wifi_scan);
    gamely_daemon_webloop_route_http("/wifi/scan-results", http_wifi_scan_results);
    gamely_daemon_webloop_route_http("/wifi/connect",      http_wifi_connect);
    gamely_daemon_webloop_route_http("/wifi/disconnect",   http_wifi_disconnect);
    gamely_daemon_webloop_route_http("/wifi/status",       http_wifi_status);
}
