// Server — 서버 (접속 받기) 시험: EXE 에서 HTTP · WebSocket 을 받고 답함 · 웹에서는 listen 이 늘 0
//   compound 값 시험도 같이 (info · one · empty · bad) — 웹과 EXE 가 같은 줄
#include "jdh.h"
#include <stdio.h>
#include <string.h>

static int sid, port, nreq, nws, ws[8];
static char last[512], wsmsgs[1024], wspath[128], paths[512] = "", info[1024];

JDH_COMMAND(listen, int p, int local) { sid = jdh_srv_listen(p, local ? JDH_SRV_LOCAL : 0); port = sid ? jdh_srv_port(sid) : 0; }
JDH_COMMAND(stop, void) { if (sid) jdh_srv_close(sid); sid = 0; }

static void hdr(int rid, const char *name, char *out, int n) {   // 요청 머리에서 이름의 값
    char h[4096]; out[0] = 0; if (jdh_req_info(rid, JDH_REQ_HEADERS, h, sizeof h) < 0) return;
    size_t L = strlen(name);
    for (char *p = h; p && *p; ) {
        char *e = strchr(p, '\n'); if (e) *e = 0;
        if (!strncmp(p, name, L) && p[L] == ':') { const char *v = p + L + 1; while (*v == ' ') v++; snprintf(out, (size_t)n, "%s", v); return; }
        p = e ? e + 1 : NULL;
    }
}
JDH_FRAME(dt) {
    (void)dt;
    if (!sid) goto wsonly;
    for (int rid; (rid = jdh_srv_next(sid)); ) {
        char m[16], path[256], body[512], tag[64], out[1024];
        jdh_req_info(rid, JDH_REQ_METHOD, m, sizeof m); jdh_req_info(rid, JDH_REQ_PATH, path, sizeof path);
        int k = jdh_req_read(rid, 0, body, sizeof body - 1); body[k] = 0;
        nreq++; size_t pl = strlen(paths); snprintf(paths + pl, sizeof paths - pl, "%s\"%s\"", pl ? ", " : "", path);
        snprintf(last, sizeof last, "%s %s %d", m, path, jdh_req_size(rid));
        if (!strcmp(path, "/hello")) { snprintf(out, sizeof out, "hi %s", m); jdh_req_respond(rid, 200, NULL, out, (int)strlen(out)); }
        else if (!strcmp(path, "/echo")) { hdr(rid, "X-Tag", tag, sizeof tag); snprintf(out, sizeof out, "%s:%s", tag, body); jdh_req_respond(rid, 200, "Content-Type: text/plain; charset=utf-8\nX-Server: jdh", out, (int)strlen(out)); }
        else jdh_req_respond(rid, 404, NULL, "nope", 4);
    }
    for (int w; (w = jdh_srv_ws(sid)); ) {
        if (nws < 8) ws[nws++] = w;
        jdh_req_info(w, JDH_REQ_PATH, wspath, sizeof wspath);
        jdh_ws_send(w, "welcome", 7, 1);
    }
wsonly:
    for (int i = 0; i < nws; i++) {
        if (!ws[i]) continue;
        for (;;) {
            char m[128]; int g = jdh_ws_recv(ws[i], m, sizeof m - 1); if (g < 0) break;
            m[g < (int)sizeof m - 1 ? g : (int)sizeof m - 1] = 0;
            size_t L = strlen(wsmsgs); snprintf(wsmsgs + L, sizeof wsmsgs - L, "%s%s", L ? "|" : "", m);
            if (!strcmp(m, "bye")) { jdh_ws_close(ws[i]); ws[i] = 0; break; }
            char e[160]; snprintf(e, sizeof e, "echo:%s", m); jdh_ws_send(ws[i], e, (int)strlen(e), 1);
        }
    }
}
JDH_VALUE(int, port) { return port; }
JDH_VALUE(int, nreq) { return nreq; }
JDH_VALUE(const char *, last) { return last; }
JDH_VALUE(int, nws) { return nws; }
JDH_VALUE(const char *, wsmsgs) { return wsmsgs; }
JDH_VALUE(const char *, wspath) { return wspath; }
JDH_VALUE(const char *, info) { snprintf(info, sizeof info, "[%d, %d, [%s], true, null, \"한글\"]", port > 0, nreq, paths); return info; }
JDH_VALUE(const char *, one) { return "5"; }
JDH_VALUE(const char *, empty) { return ""; }
JDH_VALUE(const char *, bad) { return "{\"a\": 1}"; }
