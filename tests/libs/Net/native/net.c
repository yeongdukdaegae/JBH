// Net — 네트워크 (HTTP · WebSocket) 시험: 웹과 EXE 가 같은 줄을 내야 함 (tests/net_server.py 에 접속)
#include "jdh.h"
#include <stdio.h>
#include <string.h>

static int req, status = -2, size, sum, started;
static char body[512];
static int ws, wstate = -9, nmsg;
static char msgs[1024];

static void start(int id) { if (req) jdh_http_close(req); req = id; started = id != 0; status = id ? -1 : 0; size = sum = 0; body[0] = 0; }
JDH_COMMAND(get, const char *url) { start(jdh_http_request("GET", url, NULL, NULL, 0)); }
JDH_COMMAND(post, const char *url, const char *tag, const char *text) {
    char h[256]; snprintf(h, sizeof h, "X-Tag: %s\nContent-Type: text/plain", tag);
    start(jdh_http_request("POST", url, h, text, (int)strlen(text)));
}
JDH_COMMAND(big, const char *url) { start(jdh_http_request("GET", url, NULL, NULL, 0)); }
JDH_COMMAND(wsopen, const char *url) { if (ws) jdh_ws_close(ws); ws = jdh_ws_open(url); started = ws != 0; wstate = ws ? 0 : -1; nmsg = 0; msgs[0] = 0; }
JDH_COMMAND(wssend, const char *text) { if (ws) jdh_ws_send(ws, text, (int)strlen(text), 1); }
JDH_COMMAND(wsclose, void) { if (ws) { jdh_ws_close(ws); ws = 0; wstate = 2; } }

JDH_FRAME(dt) {
    (void)dt;
    if (req && status == -1) {
        int st = jdh_http_status(req);
        if (st != -1) {
            size = jdh_http_size(req);
            int k = jdh_http_read(req, 0, body, (int)sizeof body - 1); body[k] = 0;
            unsigned char buf[4096]; sum = 0;   // 본문을 나눠 읽어 합을 냄 (큰 응답 확인)
            for (int off = 0; off < size; ) { int g = jdh_http_read(req, off, buf, (int)sizeof buf); if (g <= 0) break; for (int i = 0; i < g; i++) sum = (sum + buf[i]) % 1000003; off += g; }
            status = st;
        }
    }
    if (ws) {
        wstate = jdh_ws_state(ws);
        for (;;) {
            int n = jdh_ws_peek(ws); if (n < 0) break;
            char m[128]; int g = jdh_ws_recv(ws, m, (int)sizeof m - 1); if (g < 0) break;
            m[g < (int)sizeof m - 1 ? g : (int)sizeof m - 1] = 0;
            size_t L = strlen(msgs); snprintf(msgs + L, sizeof msgs - L, "%s%s", nmsg ? "|" : "", m); nmsg++;
        }
    }
}
JDH_VALUE(int, status) { return status; }
JDH_VALUE(const char *, body) { return body; }
JDH_VALUE(int, size) { return size; }
JDH_VALUE(int, sum) { return sum; }
JDH_VALUE(int, ws) { return wstate; }
JDH_VALUE(const char *, msgs) { return msgs; }
JDH_VALUE(int, nmsg) { return nmsg; }
JDH_VALUE(int, started) { return started; }
