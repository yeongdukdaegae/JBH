/* 사용자 라이브러리 (JDH) 의 네트워크 — "network" (에디터의 JBX_BOOT 의 NET 과 같은 뜻)
 *   HTTP 요청 · WebSocket. 모두 기다리지 않음: 요청을 걸어 두고 다음 프레임 · 명령에서 상태를 물어봄.
 *   Windows: WinHTTP (http · https · ws · wss).  그 밖 (시험용 빌드): 소켓으로 http · ws 만.
 *   한도: 동시에 64개 · 응답 본문과 WebSocket 에 쌓인 메시지는 16MB 까지.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#endif

#define NET_MAX 64
#define NET_CAP (16 << 20)
enum { K_HTTP = 1, K_WS };
typedef struct Msg { unsigned char *b; int n; struct Msg *next; } Msg;
typedef struct {
    int kind, id, released, running;
    /* http */
    int status; unsigned char *body; int blen;
    char *method, *url, *headers; unsigned char *req; int reqn;
    /* ws */
    int wstate; Msg *head, *tail; int qbytes;
#ifdef _WIN32
    HINTERNET h, hc;
#else
    int fd;
#endif
} Conn;
static Conn CN[NET_MAX];
static pthread_mutex_t MU = PTHREAD_MUTEX_INITIALIZER;
static int GEN;

static char *sdup(const char *s) { size_t n = strlen(s ? s : ""); char *r = malloc(n + 1); memcpy(r, s ? s : "", n); r[n] = 0; return r; }
static Conn *slotNew(int kind) {   /* MU 를 잡은 채로 */
    for (int i = 0; i < NET_MAX; i++) if (!CN[i].kind) {
        Conn *c = &CN[i]; memset(c, 0, sizeof *c); c->kind = kind; c->id = i + 1 + (++GEN % 30000) * NET_MAX; c->status = -1;
#ifndef _WIN32
        c->fd = -1;
#endif
        return c;
    }
    return NULL;
}
static Conn *slotOf(int id, int kind) { if (id <= 0) return NULL; Conn *c = &CN[(id - 1) % NET_MAX]; return c->kind == kind && c->id == id && !c->released ? c : NULL; }
static void slotFree(Conn *c) {   /* MU 를 잡은 채로 */
    free(c->body); free(c->method); free(c->url); free(c->headers); free(c->req);
    for (Msg *m = c->head; m; ) { Msg *n = m->next; free(m->b); free(m); m = n; }
    memset(c, 0, sizeof *c);
}
static int grow(unsigned char **b, int *n, int *cap, const unsigned char *p, int k) {
    if (*n + k > NET_CAP) return 0;
    if (*n + k + 1 > *cap) { *cap = (*n + k + 1) * 2; *b = realloc(*b, *cap); }
    memcpy(*b + *n, p, k); *n += k; (*b)[*n] = 0; return 1;
}
static void wsPush(Conn *c, unsigned char *b, int n) {   /* b 는 넘겨받음 */
    pthread_mutex_lock(&MU);
    if (c->released || c->qbytes + n > NET_CAP) { free(b); pthread_mutex_unlock(&MU); return; }
    Msg *m = malloc(sizeof *m); m->b = b; m->n = n; m->next = NULL;
    if (c->tail) c->tail->next = m; else c->head = m;
    c->tail = m; c->qbytes += n;
    pthread_mutex_unlock(&MU);
}
static int startsWith(const char *s, const char *p) { return !strncmp(s, p, strlen(p)); }

/* ================= 전송 ================= */
#ifdef _WIN32
static HINTERNET SES;
static HINTERNET ses(void) { if (!SES) { SES = WinHttpOpen(L"jdh-game", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0); if (SES) WinHttpSetTimeouts(SES, 15000, 15000, 30000, 30000); } return SES; }
/* 주소 → 연결 · 요청 (ws · wss 는 http · https 로 바꿔 엶) */
static HINTERNET openReq(const char *url, const char *method, HINTERNET *hc) {
    char fixed[4096];
    if (startsWith(url, "wss://")) snprintf(fixed, sizeof fixed, "https://%s", url + 6);
    else if (startsWith(url, "ws://")) snprintf(fixed, sizeof fixed, "http://%s", url + 5);
    else snprintf(fixed, sizeof fixed, "%s", url);
    static const int W = 4096; wchar_t *wurl = malloc(sizeof(wchar_t) * W), *host = malloc(sizeof(wchar_t) * 512), *path = malloc(sizeof(wchar_t) * W * 2), *extra = malloc(sizeof(wchar_t) * W), wm[16];
    HINTERNET r = NULL; *hc = NULL;
    if (MultiByteToWideChar(CP_UTF8, 0, fixed, -1, wurl, W) && MultiByteToWideChar(CP_UTF8, 0, method, -1, wm, 16)) {
        URL_COMPONENTS uc; memset(&uc, 0, sizeof uc); uc.dwStructSize = sizeof uc;
        uc.lpszHostName = host; uc.dwHostNameLength = 512; uc.lpszUrlPath = path; uc.dwUrlPathLength = W; uc.lpszExtraInfo = extra; uc.dwExtraInfoLength = W;
        if (WinHttpCrackUrl(wurl, 0, 0, &uc) && ses()) {
            wcsncat(path, extra, W - 1);
            *hc = WinHttpConnect(ses(), host, uc.nPort, 0);
            if (*hc) r = WinHttpOpenRequest(*hc, wm, *path ? path : L"/", NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
            if (!r && *hc) { WinHttpCloseHandle(*hc); *hc = NULL; }
        }
    }
    free(wurl); free(host); free(path); free(extra);
    return r;
}
static wchar_t *wideHeaders(const char *h) {   /* "이름: 값\n…" → "이름: 값\r\n…" */
    if (!h || !*h) return NULL;
    size_t n = strlen(h); char *t = malloc(n * 2 + 3), *w = t;
    for (const char *p = h; *p; p++) { if (*p == '\r') continue; if (*p == '\n') { *w++ = '\r'; *w++ = '\n'; } else *w++ = *p; }
    if (w > t && w[-1] != '\n') { *w++ = '\r'; *w++ = '\n'; }
    *w = 0;
    int k = MultiByteToWideChar(CP_UTF8, 0, t, -1, NULL, 0); wchar_t *o = malloc(sizeof(wchar_t) * (k + 1));
    MultiByteToWideChar(CP_UTF8, 0, t, -1, o, k); free(t); return o;
}
static void httpRun(Conn *c, int *status, unsigned char **body, int *blen) {
    HINTERNET hc, r = openReq(c->url, c->method, &hc); if (!r) return;
    wchar_t *wh = wideHeaders(c->headers);
    if (WinHttpSendRequest(r, wh ? wh : WINHTTP_NO_ADDITIONAL_HEADERS, wh ? (DWORD)-1L : 0, c->reqn ? c->req : WINHTTP_NO_REQUEST_DATA, (DWORD)c->reqn, (DWORD)c->reqn, 0) && WinHttpReceiveResponse(r, NULL)) {
        DWORD st = 0, sz = sizeof st; int cap = 0, ok = 1;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &st, &sz, WINHTTP_NO_HEADER_INDEX);
        unsigned char *buf = malloc(65536);
        for (;;) { DWORD got = 0; if (!WinHttpReadData(r, buf, 65536, &got)) { ok = 0; break; } if (!got) break; if (!grow(body, blen, &cap, buf, (int)got)) { ok = 0; break; } }
        free(buf);
        if (ok) *status = (int)st; else { free(*body); *body = NULL; *blen = 0; }
    }
    free(wh); WinHttpCloseHandle(r); WinHttpCloseHandle(hc);
}
static int wsConnect(Conn *c) {
    HINTERNET hc, r = openReq(c->url, "GET", &hc); if (!r) return 0;
    HINTERNET ws = NULL;
    if (WinHttpSetOption(r, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, NULL, 0) && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, NULL))
        ws = WinHttpWebSocketCompleteUpgrade(r, 0);
    WinHttpCloseHandle(r);
    if (!ws) { WinHttpCloseHandle(hc); return 0; }
    { DWORD inf = 0xFFFFFFFF; WinHttpSetOption(ws, WINHTTP_OPTION_RECEIVE_TIMEOUT, &inf, sizeof inf); }   /* 메시지는 언제 올지 모름 */
    pthread_mutex_lock(&MU); c->h = ws; c->hc = hc; pthread_mutex_unlock(&MU);
    return 1;
}
static void wsLoop(Conn *c) {
    unsigned char *buf = malloc(65536), *acc = NULL; int an = 0, cap = 0;
    for (;;) {
        DWORD got = 0; WINHTTP_WEB_SOCKET_BUFFER_TYPE ty;
        if (WinHttpWebSocketReceive(c->h, buf, 65536, &got, &ty) != NO_ERROR || ty == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;
        if (!grow(&acc, &an, &cap, buf, (int)got)) break;
        if (ty == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE || ty == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE) { wsPush(c, acc ? acc : calloc(1, 1), an); acc = NULL; an = 0; cap = 0; }
    }
    free(buf); free(acc);
    pthread_mutex_lock(&MU); HINTERNET h = c->h, hc = c->hc; c->h = NULL; c->hc = NULL; pthread_mutex_unlock(&MU);
    if (h) WinHttpCloseHandle(h);
    if (hc) WinHttpCloseHandle(hc);
}
static int wsSendRaw(Conn *c, const void *d, int n, int text) {   /* MU 를 잡은 채로 */
    return c->h && WinHttpWebSocketSend(c->h, text ? WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE : WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE, (PVOID)d, (DWORD)n) == NO_ERROR;
}
static void wsShut(Conn *c) { if (c->h) WinHttpWebSocketShutdown(c->h, 1000, NULL, 0); }   /* MU 를 잡은 채로 — 받는 쪽이 닫힘을 받고 끝남 */
#else
/* ---- 소켓 (http · ws 만) ---- */
static int dial(const char *url, const char *scheme, char *host, size_t hn, char *path, size_t pn) {
    if (!startsWith(url, scheme)) return -1;
    const char *p = url + strlen(scheme), *sl = strchr(p, '/'); size_t L = sl ? (size_t)(sl - p) : strlen(p);
    if (L >= hn) return -1;
    memcpy(host, p, L); host[L] = 0; snprintf(path, pn, "%s", sl ? sl : "/");
    char name[256]; snprintf(name, sizeof name, "%s", host); char *co = strrchr(name, ':'); const char *port = "80"; if (co) { *co = 0; port = co + 1; }
    struct addrinfo hints, *ai = NULL; memset(&hints, 0, sizeof hints); hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(name, port, &hints, &ai) || !ai) return -1;
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd >= 0 && connect(fd, ai->ai_addr, ai->ai_addrlen)) { close(fd); fd = -1; }
    freeaddrinfo(ai); return fd;
}
static int sendAll(int fd, const void *b, size_t n) { const char *p = b; while (n) { ssize_t k = send(fd, p, n, MSG_NOSIGNAL); if (k <= 0) return 0; p += k; n -= (size_t)k; } return 1; }
static int recvN(int fd, void *b, size_t n) { char *p = b; while (n) { ssize_t k = recv(fd, p, n, 0); if (k <= 0) return 0; p += k; n -= (size_t)k; } return 1; }
static void httpRun(Conn *c, int *status, unsigned char **body, int *blen) {
    char host[256], path[2048]; int fd = dial(c->url, "http://", host, sizeof host, path, sizeof path); if (fd < 0) return;
    struct timeval tv = {30, 0}; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    size_t hl = strlen(c->headers ? c->headers : ""); char *head = malloc(strlen(path) + hl * 2 + 512), *w = head;
    w += sprintf(w, "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nContent-Length: %d\r\n", c->method, path, host, c->reqn);
    for (const char *p = c->headers ? c->headers : ""; *p; p++) { if (*p == '\r') continue; if (*p == '\n') { *w++ = '\r'; *w++ = '\n'; } else *w++ = *p; }
    if (hl && w[-1] != '\n') { *w++ = '\r'; *w++ = '\n'; }
    *w++ = '\r'; *w++ = '\n';
    unsigned char *all = NULL; int an = 0, cap = 0, ok = sendAll(fd, head, (size_t)(w - head)) && (!c->reqn || sendAll(fd, c->req, (size_t)c->reqn));
    free(head);
    unsigned char buf[65536];
    while (ok) { ssize_t k = recv(fd, buf, sizeof buf, 0); if (k < 0) ok = 0; if (k <= 0) break; if (!grow(&all, &an, &cap, buf, (int)k)) ok = 0; }
    close(fd);
    char *sep = ok && all ? strstr((char *)all, "\r\n\r\n") : NULL; int st = 0;
    if (sep && sscanf((char *)all, "HTTP/%*s %d", &st) == 1 && st > 0) {
        int off = (int)(sep + 4 - (char *)all); *blen = an - off; *body = malloc(*blen + 1); memcpy(*body, all + off, *blen); (*body)[*blen] = 0; *status = st;
    }
    free(all);
}
static int wsConnect(Conn *c) {
    char host[256], path[2048]; int fd = dial(c->url, "ws://", host, sizeof host, path, sizeof path); if (fd < 0) return 0;
    char req[3000]; int n = snprintf(req, sizeof req, "GET %s HTTP/1.1\r\nHost: %s\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", path, host);
    char resp[4096]; int rn = 0, ok = sendAll(fd, req, (size_t)n);
    while (ok && rn < (int)sizeof resp - 1) { if (!recvN(fd, resp + rn, 1)) { ok = 0; break; } rn++; resp[rn] = 0; if (rn >= 4 && !strcmp(resp + rn - 4, "\r\n\r\n")) break; }
    if (!ok || !strstr(resp, " 101 ")) { close(fd); return 0; }
    pthread_mutex_lock(&MU); c->fd = fd; pthread_mutex_unlock(&MU);
    return 1;
}
static int wsFrame(int fd, int op, const void *d, int n) {   /* 보내는 쪽은 가림 (mask) */
    unsigned char h[14]; int k = 0; h[k++] = (unsigned char)(0x80 | op);
    if (n < 126) h[k++] = (unsigned char)(0x80 | n); else if (n < 65536) { h[k++] = 0x80 | 126; h[k++] = (unsigned char)(n >> 8); h[k++] = (unsigned char)n; }
    else { h[k++] = 0x80 | 127; for (int i = 7; i >= 0; i--) h[k++] = (unsigned char)(((unsigned long long)n >> (8 * i)) & 255); }
    unsigned char mk[4] = {0x12, 0x34, 0x56, 0x78}; memcpy(h + k, mk, 4); k += 4;
    unsigned char *m = malloc(n ? n : 1); for (int i = 0; i < n; i++) m[i] = ((const unsigned char *)d)[i] ^ mk[i & 3];
    int ok = sendAll(fd, h, (size_t)k) && (!n || sendAll(fd, m, (size_t)n)); free(m); return ok;
}
static void wsLoop(Conn *c) {
    int fd = c->fd; unsigned char *acc = NULL; int an = 0, cap = 0;
    for (;;) {
        unsigned char h[2]; if (!recvN(fd, h, 2)) break;
        int op = h[0] & 15, fin = h[0] & 0x80, masked = h[1] & 0x80; unsigned long long len = h[1] & 127;
        if (len == 126) { unsigned char e[2]; if (!recvN(fd, e, 2)) break; len = (unsigned long long)e[0] << 8 | e[1]; }
        else if (len == 127) { unsigned char e[8]; if (!recvN(fd, e, 8)) break; len = 0; for (int i = 0; i < 8; i++) len = len << 8 | e[i]; }
        unsigned char mk[4] = {0, 0, 0, 0}; if (masked && !recvN(fd, mk, 4)) break;
        if (len > NET_CAP) break;
        unsigned char *p = malloc(len ? (size_t)len : 1); if (len && !recvN(fd, p, (size_t)len)) { free(p); break; }
        if (masked) for (unsigned long long i = 0; i < len; i++) p[i] ^= mk[i & 3];
        if (op == 8) { free(p); break; }
        if (op == 9) { pthread_mutex_lock(&MU); if (c->fd >= 0) wsFrame(fd, 10, p, (int)len); pthread_mutex_unlock(&MU); free(p); continue; }
        if (op == 10) { free(p); continue; }
        int okg = grow(&acc, &an, &cap, p, (int)len); free(p); if (!okg) break;
        if (fin) { wsPush(c, acc ? acc : calloc(1, 1), an); acc = NULL; an = 0; cap = 0; }
    }
    free(acc);
    pthread_mutex_lock(&MU); c->fd = -1; pthread_mutex_unlock(&MU);
    close(fd);
}
static int wsSendRaw(Conn *c, const void *d, int n, int text) { return c->fd >= 0 && wsFrame(c->fd, text ? 1 : 2, d, n); }
static void wsShut(Conn *c) { if (c->fd >= 0) { wsFrame(c->fd, 8, "\x03\xe8", 2); shutdown(c->fd, SHUT_RDWR); } }
#endif

/* ================= 스레드 ================= */
static void *httpThread(void *a) {
    Conn *c = a; int st = 0, blen = 0; unsigned char *body = NULL;
    httpRun(c, &st, &body, &blen);
    pthread_mutex_lock(&MU);
    c->running = 0;
    if (c->released) { free(body); slotFree(c); } else { c->body = body; c->blen = blen; c->status = st; }
    pthread_mutex_unlock(&MU);
    return NULL;
}
static void *wsThread(void *a) {
    Conn *c = a; int ok = wsConnect(c);
    pthread_mutex_lock(&MU); c->wstate = ok ? 1 : -1; int rel = c->released; pthread_mutex_unlock(&MU);
    if (ok) {
        if (rel) { pthread_mutex_lock(&MU); wsShut(c); pthread_mutex_unlock(&MU); }
        wsLoop(c);
    }
    pthread_mutex_lock(&MU);
    c->running = 0; if (c->wstate == 1) c->wstate = 2;
    if (c->released) slotFree(c);
    pthread_mutex_unlock(&MU);
    return NULL;
}
static int spawn(void *(*fn)(void *), Conn *c) {
    pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    c->running = 1; int r = pthread_create(&t, &at, fn, c); pthread_attr_destroy(&at); return !r;
}

/* ================= jdh.h 의 함수 ================= */
int jdh_http_request(const char *method, const char *url, const char *headers, const void *body, int n) {
    if (!url || !(startsWith(url, "http://") || startsWith(url, "https://")) || strlen(url) > 3900 || n < 0 || n > NET_CAP) return 0;
    if (!method || !*method || strlen(method) > 12) return 0;
    pthread_mutex_lock(&MU);
    Conn *c = slotNew(K_HTTP); int id = 0;
    if (c) {
        c->method = sdup(method); c->url = sdup(url); c->headers = sdup(headers); c->reqn = body ? n : 0;
        if (c->reqn) { c->req = malloc(c->reqn); memcpy(c->req, body, c->reqn); }
        id = c->id;
        if (!spawn(httpThread, c)) { slotFree(c); id = 0; }
    }
    pthread_mutex_unlock(&MU);
    return id;
}
int jdh_http_status(int id) { pthread_mutex_lock(&MU); Conn *c = slotOf(id, K_HTTP); int r = c ? c->status : 0; pthread_mutex_unlock(&MU); return r; }
int jdh_http_size(int id) { pthread_mutex_lock(&MU); Conn *c = slotOf(id, K_HTTP); int r = c && c->status > 0 ? c->blen : 0; pthread_mutex_unlock(&MU); return r; }
int jdh_http_read(int id, int offset, void *buf, int n) {
    pthread_mutex_lock(&MU); Conn *c = slotOf(id, K_HTTP); int k = 0;
    if (c && c->status > 0 && offset >= 0 && offset < c->blen && n > 0 && buf) { k = c->blen - offset < n ? c->blen - offset : n; memcpy(buf, c->body + offset, k); }
    pthread_mutex_unlock(&MU); return k;
}
void jdh_http_close(int id) {
    pthread_mutex_lock(&MU); Conn *c = slotOf(id, K_HTTP);
    if (c) { if (c->running) c->released = 1; else slotFree(c); }
    pthread_mutex_unlock(&MU);
}
int jdh_ws_open(const char *url) {
    if (!url || !(startsWith(url, "ws://") || startsWith(url, "wss://")) || strlen(url) > 3900) return 0;
    pthread_mutex_lock(&MU);
    Conn *c = slotNew(K_WS); int id = 0;
    if (c) { c->url = sdup(url); id = c->id; if (!spawn(wsThread, c)) { slotFree(c); id = 0; } }
    pthread_mutex_unlock(&MU);
    return id;
}
int jdh_ws_state(int id) { pthread_mutex_lock(&MU); Conn *c = slotOf(id, K_WS); int r = c ? c->wstate : -1; pthread_mutex_unlock(&MU); return r; }
int jdh_ws_send(int id, const void *data, int n, int text) {
    if (n < 0 || n > NET_CAP || (n && !data)) return 0;
    pthread_mutex_lock(&MU); Conn *c = slotOf(id, K_WS); int r = c && c->wstate == 1 && wsSendRaw(c, data, n, text); pthread_mutex_unlock(&MU); return r;
}
int jdh_ws_peek(int id) { pthread_mutex_lock(&MU); Conn *c = slotOf(id, K_WS); int r = c && c->head ? c->head->n : -1; pthread_mutex_unlock(&MU); return r; }
int jdh_ws_recv(int id, void *buf, int n) {
    pthread_mutex_lock(&MU); Conn *c = slotOf(id, K_WS); int r = -1;
    if (c && c->head) {
        Msg *m = c->head; r = m->n;
        if (buf && n > 0) memcpy(buf, m->b, m->n < n ? m->n : n);
        c->head = m->next; if (!c->head) c->tail = NULL; c->qbytes -= m->n; free(m->b); free(m);
    }
    pthread_mutex_unlock(&MU); return r;
}
void jdh_ws_close(int id) {
    pthread_mutex_lock(&MU); Conn *c = slotOf(id, K_WS);
    if (c) { if (c->running) { c->released = 1; wsShut(c); } else slotFree(c); }
    pthread_mutex_unlock(&MU);
}
