#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <sys/wait.h>

#include "gecnd.h"

static char s_iface[64];

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

static void detail_set(char *detail, size_t detail_sz, const char *msg) {
    if (detail && detail_sz)
        json_escape(detail, msg ? msg : "", detail_sz);
}

static bool wpa_quote_value(char *dst, const char *src, size_t max) {
    if (!dst || !src || max < 3) return false;

    size_t i = 0;
    dst[i++] = '"';
    for (; *src; src++) {
        unsigned char c = (unsigned char)*src;
        if (c < 0x20 || c == 0x7f) return false;
        if (c == '"' || c == '\\') {
            if (i + 2 >= max) return false;
            dst[i++] = '\\';
            dst[i++] = (char)c;
        } else {
            if (i + 1 >= max) return false;
            dst[i++] = (char)c;
        }
    }
    if (i + 1 >= max) return false;
    dst[i++] = '"';
    dst[i] = '\0';
    return true;
}

static bool is_hex_psk(const char *s) {
    if (!s || strlen(s) != 64) return false;
    for (; *s; s++)
        if (!isxdigit((unsigned char)*s)) return false;
    return true;
}

static void trim_output(char *out) {
    if (!out) return;
    size_t n = strlen(out);
    while (n > 0 && (out[n - 1] == '\n' || out[n - 1] == '\r' || out[n - 1] == ' '))
        out[--n] = '\0';
}

static void run_argv(char *const argv[], char *out, size_t out_sz) {
    if (out && out_sz) out[0] = '\0';

    int fd[2];
    if (pipe(fd) != 0) return;

    pid_t pid = fork();
    if (pid == 0) {
        close(fd[0]);
        dup2(fd[1], STDOUT_FILENO);
        dup2(fd[1], STDERR_FILENO);
        close(fd[1]);
        execvp(argv[0], argv);
        _exit(127);
    }

    close(fd[1]);
    if (pid < 0) {
        close(fd[0]);
        return;
    }

    size_t n = 0;
    for (;;) {
        char buf[256];
        ssize_t r = read(fd[0], buf, sizeof(buf));
        if (r > 0) {
            if (out && out_sz && n < out_sz - 1) {
                size_t room = out_sz - 1 - n;
                size_t take = (size_t)r < room ? (size_t)r : room;
                memcpy(out + n, buf, take);
                n += take;
            }
            continue;
        }
        if (r < 0 && errno == EINTR) continue;
        break;
    }
    close(fd[0]);

    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}

    if (out && out_sz) {
        out[n] = '\0';
        trim_output(out);
    }
}

static void wpa_cli_run_argv(const char *const *args, size_t argc,
                             char *out, size_t out_sz) {
    char *argv[20];
    size_t n = 0;

    argv[n++] = "wpa_cli";
    if (s_iface[0]) {
        argv[n++] = "-i";
        argv[n++] = s_iface;
    }
    for (size_t i = 0; i < argc && n < (sizeof(argv) / sizeof(argv[0])) - 1; i++)
        argv[n++] = (char *)args[i];
    argv[n] = NULL;

    run_argv(argv, out, out_sz);
}

static void wpa_cli_run(const char *cmd, char *out, size_t out_sz) {
    const char *args[] = { cmd };
    wpa_cli_run_argv(args, 1, out, out_sz);
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
    const char *cmd[] = { args };
    char out[256];
    wpa_cli_run_argv(cmd, 1, out, sizeof(out));
    bool ok = wpa_out_ok(out);
    if (!ok) detail_set(detail, detail_sz, out);
    return ok;
}

static bool wpa_cli_ok_argv(const char *const *args, size_t argc,
                            char *detail, size_t detail_sz) {
    char out[256];
    wpa_cli_run_argv(args, argc, out, sizeof(out));
    bool ok = wpa_out_ok(out);
    if (!ok) detail_set(detail, detail_sz, out);
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

    char quoted_ssid[256], quoted_pass[256];
    if (!wpa_quote_value(quoted_ssid, ssid, sizeof(quoted_ssid))) {
        detail_set(detail, detail_sz, "invalid ssid");
        return false;
    }

    char out[256];
    wpa_cli_run("add_network", out, sizeof(out));
    if (strstr(out, "FAIL")) {
        detail_set(detail, detail_sz, out);
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
        detail_set(detail, detail_sz, out);
        return false;
    }

    char net_id_str[16];
    snprintf(net_id_str, sizeof(net_id_str), "%d", net_id);

    const char *set_ssid[] = { "set_network", net_id_str, "ssid", quoted_ssid };
    if (!wpa_cli_ok_argv(set_ssid, 4, detail, detail_sz)) goto fail;

    if (password && password[0]) {
        const char *psk_value = password;
        if (!is_hex_psk(password)) {
            size_t pass_len = strlen(password);
            if (pass_len < 8 || pass_len > 63) {
                detail_set(detail, detail_sz, "invalid password length");
                goto fail;
            }
            if (!wpa_quote_value(quoted_pass, password, sizeof(quoted_pass))) {
                detail_set(detail, detail_sz, "invalid password");
                goto fail;
            }
            psk_value = quoted_pass;
        }
        const char *set_psk[] = { "set_network", net_id_str, "psk", psk_value };
        if (!wpa_cli_ok_argv(set_psk, 4, detail, detail_sz)) goto fail;
    } else {
        const char *set_open[] = { "set_network", net_id_str, "key_mgmt", "NONE" };
        if (!wpa_cli_ok_argv(set_open, 4, detail, detail_sz)) goto fail;
    }

    const char *select_network[] = { "select_network", net_id_str };
    if (!wpa_cli_ok_argv(select_network, 2, detail, detail_sz)) goto fail;

    wpa_cli_ok("save_config", NULL, 0);
    return true;

fail:
    {
        const char *remove_network[] = { "remove_network", net_id_str };
        wpa_cli_ok_argv(remove_network, 2, NULL, 0);
    }
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
    size_t jlen = 0;
    json[jlen++] = '{';
    json[jlen] = '\0';
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
            char item[768];
            int ilen = snprintf(item, sizeof(item),
                "%s\"%s\":\"%s\"", first ? "" : ",", ekey, eval);
            if (ilen > 0 && (size_t)ilen < sizeof(item) &&
                jlen + (size_t)ilen + 2 <= sizeof(json)) {
                memcpy(json + jlen, item, (size_t)ilen);
                jlen += (size_t)ilen;
                json[jlen] = '\0';
                first = false;
            }
            *eq = '=';
        }
        if (!nl) break;
        p = nl + 1;
    }
    if (jlen + 2 <= sizeof(json)) {
        json[jlen++] = '}';
        json[jlen] = '\0';
    } else {
        json[sizeof(json) - 2] = '}';
        json[sizeof(json) - 1] = '\0';
    }
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
        char buf[512];
        char *argv[32];
        snprintf(buf, sizeof(buf), "%s", supl);
        argv[0] = "wpa_supplicant";
        int argc = 1;
        char *p = buf;
        while (*p && argc < (int)(sizeof(argv) / sizeof(argv[0])) - 1) {
            while (isspace((unsigned char)*p)) p++;
            if (!*p) break;
            argv[argc++] = p;
            while (*p && !isspace((unsigned char)*p)) p++;
            if (*p) *p++ = '\0';
        }
        argv[argc] = NULL;
        run_argv(argv, NULL, 0);
    }

    const char *ssid = getenv("wifi_ssid");
    if (ssid) do_connect(ssid, getenv("wifi_password"), NULL, 0);

    gamely_daemon_webloop_route_http("/wifi/scan",         http_wifi_scan);
    gamely_daemon_webloop_route_http("/wifi/scan-results", http_wifi_scan_results);
    gamely_daemon_webloop_route_http("/wifi/connect",      http_wifi_connect);
    gamely_daemon_webloop_route_http("/wifi/disconnect",   http_wifi_disconnect);
    gamely_daemon_webloop_route_http("/wifi/status",       http_wifi_status);
}
