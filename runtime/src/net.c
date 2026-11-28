/* 사용자 라이브러리 (JDH) 의 네트워크 — "network" (에디터의 JBX_BOOT 의 NET 과 같은 뜻) · "server" (EXE 에서만)
 *   HTTP 요청 · WebSocket. 모두 기다리지 않음: 요청을 걸어 두고 다음 프레임 · 명령에서 상태를 물어봄.
 *   Windows: WinHTTP (http · https · ws · wss).  그 밖 (시험용 빌드): 소켓으로 http · ws 만.
 *   한도: 동시에 64개 · 응답 본문과 WebSocket 에 쌓인 메시지는 16MB 까지.
 *   서버 (jdh_srv_*): 소켓 (Windows 는 Winsock) 으로 한 포트에서 HTTP 와 WebSocket 을 받음 · 접속마다 스레드 하나 · TLS 없음.
 *     서버 8개 · 받은 접속 192개 · 요청은 답하면 연결을 닫음 (Connection: close) · 30초 안에 답하지 않으면 503.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#else
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#endif

#define NET_CLI 64    /* 클라이언트 (http_request · ws_open) */
#define NET_ACC 192   /* 서버가 받은 접속 (요청 · WebSocket) */
#define NET_SRV 8
#define NET_MAX (NET_CLI + NET_ACC + NET_SRV)
#define NET_CAP (16 << 20)
enum { K_HTTP = 1, K_WS, K_SRV, K_REQ };
typedef struct Msg { unsigned char *b; int n; struct Msg *next; } Msg;
typedef struct {
    int kind, id, released, running;
    /* http */
    int status; unsigned char *body; int blen;
    char *method, *url, *headers; unsigned char *req; int reqn;
    /* ws */
    int wstate; Msg *head, *tail; int qbytes;
    /* 서버: srv = 받은 서버 번호 (0 = 클라이언트) · sk = 소켓 · ready = 꺼낼 수 있음 · taken = 꺼냄 · seq = 받은 차례
     *   K_SRV: port · K_REQ: method · url (경로) · headers · req (본문) · addr · 답 (done · rst · rhead · rbody) */
    int srv, ready, taken, done, port, rst, rn; unsigned seq; long long sk; char addr[64]; char *rhead; unsigned char *rbody;
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
static int slotCount(int acc) {   /* MU 를 잡은 채로 — acc: 0 클라이언트 · 1 받은 접속 · 2 서버 */
    int n = 0;
    for (int i = 0; i < NET_MAX; i++) if (CN[i].kind) { int a = CN[i].kind == K_SRV ? 2 : CN[i].kind == K_REQ || CN[i].srv ? 1 : 0; n += a == acc; }
    return n;
}
static Conn *slotNew(int kind) {   /* MU 를 잡은 채로 */
    for (int i = 0; i < NET_MAX; i++) if (!CN[i].kind) {
        Conn *c = &CN[i]; memset(c, 0, sizeof *c); c->kind = kind; c->id = i + 1 + (++GEN % 30000) * NET_MAX; c->status = -1; c->sk = -1;
#ifndef _WIN32
        c->fd = -1;
#endif
        return c;
    }
    return NULL;
}
static Conn *slotOf(int id, int kind) { if (id <= 0) return NULL; Conn *c = &CN[(id - 1) % NET_MAX]; return c->kind == kind && c->id == id && !c->released ? c : NULL; }
static void slotFree(Conn *c) {   /* MU 를 잡은 채로 */
    free(c->body); free(c->method); free(c->url); free(c->headers); free(c->req); free(c->rhead); free(c->rbody);
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

/* ================= 서버 (jdh_srv_* · 소켓 — Windows 와 그 밖이 같은 코드) ================= */
#ifdef _WIN32
typedef SOCKET sk_t;
#define SK_BAD INVALID_SOCKET
#define skClose closesocket
#define SK_NOSIG 0
typedef int sklen_t;
static int skInit(void) { static int ok; if (!ok) { WSADATA w; ok = WSAStartup(MAKEWORD(2, 2), &w) == 0; } return ok; }
static void skTimeout(sk_t s, int ms) { DWORD t = (DWORD)ms; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char *)&t, sizeof t); }
static void skSleep(int ms) { Sleep((DWORD)ms); }
#else
typedef int sk_t;
#define SK_BAD (-1)
#define skClose close
#define SK_NOSIG MSG_NOSIGNAL
typedef socklen_t sklen_t;
static int skInit(void) { return 1; }
static void skTimeout(sk_t s, int ms) { struct timeval t = {ms / 1000, (ms % 1000) * 1000}; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t); }
static void skSleep(int ms) { struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000L}; nanosleep(&t, NULL); }
#endif
static pthread_cond_t CV = PTHREAD_COND_INITIALIZER;
static unsigned SEQ;
static int skSend(sk_t s, const void *b, size_t n) { const char *p = b; while (n) { int k = send(s, p, (int)(n > (1u << 30) ? (1u << 30) : n), SK_NOSIG); if (k <= 0) return 0; p += k; n -= (size_t)k; } return 1; }
static int skRecvN(sk_t s, void *b, size_t n) { char *p = b; while (n) { int k = recv(s, p, (int)(n > (1u << 30) ? (1u << 30) : n), 0); if (k <= 0) return 0; p += k; n -= (size_t)k; } return 1; }
static int lowEq(const char *a, const char *b, size_t n) { for (size_t i = 0; i < n; i++) { char x = a[i], y = b[i]; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return 0; } return 1; }
/* "이름: 값\n…" 에서 이름의 값 (대소문자 무시 · 앞뒤 빈칸 뺌) → out */
static int hdrGet(const char *h, const char *name, char *out, size_t n) {
    size_t L = strlen(name);
    for (const char *p = h; p && *p; ) {
        const char *e = strchr(p, '\n'); size_t ln = e ? (size_t)(e - p) : strlen(p);
        if (ln > L && p[L] == ':' && lowEq(p, name, L)) {
            const char *v = p + L + 1; while (*v == ' ' || *v == '\t') v++;
            size_t vl = (size_t)(p + ln - v); while (vl && (v[vl - 1] == ' ' || v[vl - 1] == '\t')) vl--;
            if (vl >= n) vl = n - 1;
            memcpy(out, v, vl); out[vl] = 0; return 1;
        }
        p = e ? e + 1 : NULL;
    }
    return 0;
}
/* SHA-1 · base64 (WebSocket 악수의 Sec-WebSocket-Accept) */
static void sha1(const unsigned char *m, size_t n, unsigned char out[20]) {
    unsigned h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    size_t tot = ((n + 8) / 64 + 1) * 64; unsigned char *b = calloc(1, tot);
    memcpy(b, m, n); b[n] = 0x80; unsigned long long bits = (unsigned long long)n * 8; for (int i = 0; i < 8; i++) b[tot - 1 - i] = (unsigned char)(bits >> (8 * i));
    for (size_t o = 0; o < tot; o += 64) {
        unsigned w[80];
        for (int i = 0; i < 16; i++) w[i] = (unsigned)b[o + 4 * i] << 24 | (unsigned)b[o + 4 * i + 1] << 16 | (unsigned)b[o + 4 * i + 2] << 8 | b[o + 4 * i + 3];
        for (int i = 16; i < 80; i++) { unsigned x = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16]; w[i] = x << 1 | x >> 31; }
        unsigned a = h[0], bb = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            unsigned f, k;
            if (i < 20) { f = (bb & c) | (~bb & d); k = 0x5A827999; } else if (i < 40) { f = bb ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (bb & c) | (bb & d) | (c & d); k = 0x8F1BBCDC; } else { f = bb ^ c ^ d; k = 0xCA62C1D6; }
            unsigned t = (a << 5 | a >> 27) + f + e + k + w[i]; e = d; d = c; c = bb << 30 | bb >> 2; bb = a; a = t;
        }
        h[0] += a; h[1] += bb; h[2] += c; h[3] += d; h[4] += e;
    }
    free(b);
    for (int i = 0; i < 5; i++) { out[4 * i] = (unsigned char)(h[i] >> 24); out[4 * i + 1] = (unsigned char)(h[i] >> 16); out[4 * i + 2] = (unsigned char)(h[i] >> 8); out[4 * i + 3] = (unsigned char)h[i]; }
}
static void b64(const unsigned char *s, int n, char *o) {
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int k = 0;
    for (int i = 0; i < n; i += 3) {
        unsigned v = (unsigned)s[i] << 16 | (i + 1 < n ? (unsigned)s[i + 1] << 8 : 0) | (i + 2 < n ? s[i + 2] : 0);
        o[k++] = T[v >> 18 & 63]; o[k++] = T[v >> 12 & 63]; o[k++] = i + 1 < n ? T[v >> 6 & 63] : '='; o[k++] = i + 2 < n ? T[v & 63] : '=';
    }
    o[k] = 0;
}
static const char *reason(int st) {
    switch (st) {
    case 200: return "OK"; case 201: return "Created"; case 202: return "Accepted"; case 204: return "No Content";
    case 301: return "Moved Permanently"; case 302: return "Found"; case 304: return "Not Modified";
    case 400: return "Bad Request"; case 401: return "Unauthorized"; case 403: return "Forbidden"; case 404: return "Not Found";
    case 405: return "Method Not Allowed"; case 411: return "Length Required"; case 413: return "Payload Too Large";
    case 500: return "Internal Server Error"; case 501: return "Not Implemented"; case 503: return "Service Unavailable";
    default: return st < 300 ? "OK" : st < 400 ? "Redirect" : st < 500 ? "Client Error" : "Server Error";
    }
}
/* 답 보내기: 사용자가 준 머리에서 Content-Length · Connection 은 빼고 엔진이 붙임 */
static void srvReply(sk_t s, int st, const char *uh, const unsigned char *body, int n) {
    size_t hl = strlen(uh ? uh : ""); char *h = malloc(hl * 2 + 256), *w = h; char ct[8];
    w += sprintf(w, "HTTP/1.1 %d %s\r\nContent-Length: %d\r\nConnection: close\r\n", st, reason(st), n);
    if (!hdrGet(uh ? uh : "", "content-type", ct, sizeof ct)) w += sprintf(w, "Content-Type: text/plain; charset=utf-8\r\n");
    for (const char *p = uh ? uh : ""; *p; ) {
        const char *e = strchr(p, '\n'); size_t ln = e ? (size_t)(e - p) : strlen(p);
        while (ln && p[ln - 1] == '\r') ln--;
        int skip = !ln || !memchr(p, ':', ln) || (ln >= 15 && lowEq(p, "content-length:", 15)) || (ln >= 11 && lowEq(p, "connection:", 11));
        if (!skip) { memcpy(w, p, ln); w += ln; *w++ = '\r'; *w++ = '\n'; }
        p = e ? e + 1 : p + strlen(p);
    }
    *w++ = '\r'; *w++ = '\n';
    if (skSend(s, h, (size_t)(w - h)) && n) skSend(s, body, (size_t)n);
    free(h);
}
static void srvBye(sk_t s, int st, const char *msg) { srvReply(s, st, NULL, (const unsigned char *)msg, (int)strlen(msg)); shutdown(s, 2); skClose(s); }
/* 서버 쪽 WebSocket 틀 (가리지 않음) */
static int srvFrame(sk_t s, int op, const void *d, int n) {
    unsigned char h[10]; int k = 0; h[k++] = (unsigned char)(0x80 | op);
    if (n < 126) h[k++] = (unsigned char)n; else if (n < 65536) { h[k++] = 126; h[k++] = (unsigned char)(n >> 8); h[k++] = (unsigned char)n; }
    else { h[k++] = 127; for (int i = 7; i >= 0; i--) h[k++] = (unsigned char)(((unsigned long long)n >> (8 * i)) & 255); }
    return skSend(s, h, (size_t)k) && (!n || skSend(s, d, (size_t)n));
}
static void srvWsLoop(Conn *c, sk_t s) {
    unsigned char *acc = NULL; int an = 0, cap = 0;
    for (;;) {
        unsigned char h[2]; if (!skRecvN(s, h, 2)) break;
        int op = h[0] & 15, fin = h[0] & 0x80, masked = h[1] & 0x80; unsigned long long len = h[1] & 127;
        if (len == 126) { unsigned char e[2]; if (!skRecvN(s, e, 2)) break; len = (unsigned long long)e[0] << 8 | e[1]; }
        else if (len == 127) { unsigned char e[8]; if (!skRecvN(s, e, 8)) break; len = 0; for (int i = 0; i < 8; i++) len = len << 8 | e[i]; }
        unsigned char mk[4] = {0, 0, 0, 0}; if (masked && !skRecvN(s, mk, 4)) break;
        if (len > NET_CAP) break;
        unsigned char *p = malloc(len ? (size_t)len : 1); if (len && !skRecvN(s, p, (size_t)len)) { free(p); break; }
        if (masked) for (unsigned long long i = 0; i < len; i++) p[i] ^= mk[i & 3];
        if (op == 8) { pthread_mutex_lock(&MU); if (c->sk != -1) srvFrame(s, 8, p, len >= 2 ? 2 : 0); pthread_mutex_unlock(&MU); free(p); break; }
        if (op == 9) { pthread_mutex_lock(&MU); if (c->sk != -1) srvFrame(s, 10, p, (int)len); pthread_mutex_unlock(&MU); free(p); continue; }
        if (op == 10) { free(p); continue; }
        int okg = grow(&acc, &an, &cap, p, (int)len); free(p); if (!okg) break;
        if (fin) { wsPush(c, acc ? acc : calloc(1, 1), an); acc = NULL; an = 0; cap = 0; }
    }
    free(acc);
}
typedef struct { sk_t s; int sid; char addr[64]; } Acc;
static void *connThread(void *a) {
    Acc x = *(Acc *)a; free(a); sk_t s = x.s;
    skTimeout(s, 30000);
    /* 머리 (64KB 까지) */
    int cap = 65536 + 1, n = 0; char *buf = malloc((size_t)cap), *end = NULL;
    while (!end && n < cap - 1) {
        int k = recv(s, buf + n, cap - 1 - n, 0); if (k <= 0) break;
        n += k; buf[n] = 0; end = strstr(buf, "\r\n\r\n");
    }
    if (!end) { if (n >= cap - 1) srvBye(s, 431, "request header too large"); else skClose(s); free(buf); return NULL; }
    *end = 0; char *bodyAt = end + 4; int have = n - (int)(bodyAt - buf);
    char method[16], target[4096];
    char *nl = strstr(buf, "\r\n"); if (nl) *nl = 0;
    if (sscanf(buf, "%15s %4095s HTTP/", method, target) != 2) { srvBye(s, 400, "bad request"); free(buf); return NULL; }
    /* 머리 → "이름: 값\n…" */
    char *hd = malloc(strlen(nl ? nl + 2 : "") + 2), *hw = hd;
    for (const char *p = nl ? nl + 2 : ""; *p; p++) { if (*p == '\r') continue; *hw++ = *p; }
    *hw = 0;
    char up[32] = "", key[128] = "", cl[32] = "", te[64] = "";
    hdrGet(hd, "upgrade", up, sizeof up); hdrGet(hd, "sec-websocket-key", key, sizeof key); hdrGet(hd, "content-length", cl, sizeof cl); hdrGet(hd, "transfer-encoding", te, sizeof te);
    if (*up && lowEq(up, "websocket", 10) && *key) {
        /* ---- WebSocket ---- */
        pthread_mutex_lock(&MU);
        Conn *sv = slotOf(x.sid, K_SRV); Conn *c = sv && slotCount(1) < NET_ACC ? slotNew(K_WS) : NULL;
        if (c) { c->srv = x.sid; c->sk = (long long)s; c->running = 1; c->method = sdup("GET"); c->url = sdup(target); c->headers = hd; hd = NULL; snprintf(c->addr, sizeof c->addr, "%s", x.addr); c->seq = ++SEQ; }
        pthread_mutex_unlock(&MU);
        free(buf);
        if (!c) { free(hd); srvBye(s, 503, "server busy"); return NULL; }
        char cat[200], acc[64]; unsigned char dg[20];
        snprintf(cat, sizeof cat, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key); sha1((const unsigned char *)cat, strlen(cat), dg); b64(dg, 20, acc);
        char resp[256]; int rl = snprintf(resp, sizeof resp, "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n", acc);
        int ok = skSend(s, resp, (size_t)rl);
        skTimeout(s, 0);   /* 메시지는 언제 올지 모름 */
        pthread_mutex_lock(&MU); c->wstate = ok ? 1 : 2; c->ready = 1; pthread_mutex_unlock(&MU);
        if (ok) srvWsLoop(c, s);
        pthread_mutex_lock(&MU);
        c->sk = -1; c->running = 0; if (c->wstate == 1) c->wstate = 2;
        if (c->released) slotFree(c);
        pthread_mutex_unlock(&MU);
        shutdown(s, 2); skClose(s);
        return NULL;
    }
    /* ---- HTTP ---- */
    if (*te) { free(hd); free(buf); srvBye(s, 411, "Content-Length required (chunked is not supported)"); return NULL; }
    long long want = *cl ? atoll(cl) : 0;
    if (want < 0 || want > NET_CAP) { free(hd); free(buf); srvBye(s, 413, "body too large"); return NULL; }
    unsigned char *body = malloc((size_t)want + 1); int got = have < want ? have : (int)want;
    memcpy(body, bodyAt, (size_t)got);
    if (got < want && !skRecvN(s, body + got, (size_t)(want - got))) { free(body); free(hd); free(buf); skClose(s); return NULL; }
    body[want] = 0; free(buf);
    pthread_mutex_lock(&MU);
    Conn *sv = slotOf(x.sid, K_SRV); Conn *c = sv && slotCount(1) < NET_ACC ? slotNew(K_REQ) : NULL;
    if (c) {
        c->srv = x.sid; c->method = sdup(method); c->url = sdup(target); c->headers = hd; hd = NULL; c->req = body; body = NULL; c->reqn = (int)want;
        snprintf(c->addr, sizeof c->addr, "%s", x.addr); c->seq = ++SEQ; c->ready = 1; c->running = 1;
        struct timespec ts; ts.tv_sec = time(NULL) + 30; ts.tv_nsec = 0;
        while (!c->done && time(NULL) < ts.tv_sec) pthread_cond_timedwait(&CV, &MU, &ts);
    }
    int had = c != NULL, done = c && c->done, st = done ? c->rst : 503, rn = done ? c->rn : 0;
    char *rh = done ? c->rhead : NULL; unsigned char *rb = done ? c->rbody : NULL;
    if (c) { c->rhead = NULL; c->rbody = NULL; c->running = 0; slotFree(c); }
    pthread_mutex_unlock(&MU);
    free(hd); free(body);
    if (done) srvReply(s, st, rh, rb, rn);
    else { const char *m = had ? "no response" : "server busy"; srvReply(s, 503, NULL, (const unsigned char *)m, (int)strlen(m)); }
    free(rh); free(rb);
    shutdown(s, 2); skClose(s);
    return NULL;
}
static void *srvThread(void *a) {
    Conn *sv = a; sk_t L = (sk_t)sv->sk; int sid = sv->id;
    for (;;) {
        struct sockaddr_in ad; sklen_t al = sizeof ad;
        sk_t k = accept(L, (struct sockaddr *)&ad, &al);
        pthread_mutex_lock(&MU); int stop = sv->released; pthread_mutex_unlock(&MU);
        if (k == SK_BAD) { if (stop) break; skSleep(20); continue; }
        if (stop) { skClose(k); break; }
        Acc *x = malloc(sizeof *x); x->s = k; x->sid = sid;
        const unsigned char *ip = (const unsigned char *)&ad.sin_addr; snprintf(x->addr, sizeof x->addr, "%u.%u.%u.%u:%u", ip[0], ip[1], ip[2], ip[3], (unsigned)ntohs(ad.sin_port));
        pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
        if (pthread_create(&t, &at, connThread, x)) { free(x); skClose(k); }
        pthread_attr_destroy(&at);
    }
#ifndef _WIN32
    skClose(L);
#endif
    pthread_mutex_lock(&MU); sv->running = 0; slotFree(sv); pthread_mutex_unlock(&MU);
    return NULL;
}
int jdh_srv_listen(int port, int flags) {
    if (port < 0 || port > 65535 || !skInit()) return 0;
    sk_t L = socket(AF_INET, SOCK_STREAM, 0); if (L == SK_BAD) return 0;
#ifndef _WIN32
    { int one = 1; setsockopt(L, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one); }   /* 닫은 접속 (TIME_WAIT) 이 남아도 다시 열 수 있게 · Windows 는 기본이 그럼 */
#endif
    struct sockaddr_in ad; memset(&ad, 0, sizeof ad); ad.sin_family = AF_INET; ad.sin_port = htons((unsigned short)port);
    ad.sin_addr.s_addr = htonl((flags & 1) ? INADDR_LOOPBACK : INADDR_ANY);
    if (bind(L, (struct sockaddr *)&ad, sizeof ad) || listen(L, 64)) { skClose(L); return 0; }
    sklen_t al = sizeof ad; getsockname(L, (struct sockaddr *)&ad, &al);
    pthread_mutex_lock(&MU);
    Conn *c = slotCount(2) < NET_SRV ? slotNew(K_SRV) : NULL; int id = 0;
    if (c) { c->sk = (long long)L; c->port = ntohs(ad.sin_port); id = c->id; if (!spawn(srvThread, c)) { slotFree(c); id = 0; } }
    pthread_mutex_unlock(&MU);
    if (!id) skClose(L);
    return id;
}
int jdh_srv_port(int sid) { pthread_mutex_lock(&MU); Conn *c = slotOf(sid, K_SRV); int r = c ? c->port : 0; pthread_mutex_unlock(&MU); return r; }
void jdh_srv_close(int sid) {
    pthread_mutex_lock(&MU); Conn *c = slotOf(sid, K_SRV);
    if (c) {
        c->released = 1;
#ifdef _WIN32
        closesocket((sk_t)c->sk);   /* accept 가 깨어남 */
#else
        shutdown((sk_t)c->sk, SHUT_RDWR);
#endif
    }
    pthread_mutex_unlock(&MU);
}
static int srvTake(int sid, int kind) {
    pthread_mutex_lock(&MU); Conn *best = NULL;
    if (slotOf(sid, K_SRV)) for (int i = 0; i < NET_MAX; i++) { Conn *c = &CN[i]; if (c->kind == kind && c->srv == sid && c->ready && !c->taken && !c->released && (!best || c->seq < best->seq)) best = c; }
    int r = 0; if (best) { best->taken = 1; r = best->id; }
    pthread_mutex_unlock(&MU); return r;
}
int jdh_srv_next(int sid) { return srvTake(sid, K_REQ); }
int jdh_srv_ws(int sid) { return srvTake(sid, K_WS); }
static Conn *accOf(int id) {   /* MU 를 잡은 채로 — 꺼낸 요청 · 서버가 받은 WebSocket */
    Conn *c = slotOf(id, K_REQ); if (c && c->taken && !c->done) return c;
    c = slotOf(id, K_WS); return c && c->srv && c->taken ? c : NULL;
}
int jdh_req_info(int id, int what, char *out, int n) {
    pthread_mutex_lock(&MU); Conn *c = accOf(id); int r = -1;
    if (c) {
        const char *v = what == 0 ? c->method : what == 1 ? c->url : what == 2 ? c->headers : what == 3 ? c->addr : NULL;
        if (v) { r = (int)strlen(v); if (out && n > 0) snprintf(out, (size_t)n, "%s", v); }
    }
    pthread_mutex_unlock(&MU); return r;
}
int jdh_req_size(int rid) { pthread_mutex_lock(&MU); Conn *c = accOf(rid); int r = c && c->kind == K_REQ ? c->reqn : 0; pthread_mutex_unlock(&MU); return r; }
int jdh_req_read(int rid, int offset, void *buf, int n) {
    pthread_mutex_lock(&MU); Conn *c = accOf(rid); int k = 0;
    if (c && c->kind == K_REQ && offset >= 0 && offset < c->reqn && n > 0 && buf) { k = c->reqn - offset < n ? c->reqn - offset : n; memcpy(buf, c->req + offset, (size_t)k); }
    pthread_mutex_unlock(&MU); return k;
}
int jdh_req_respond(int rid, int status, const char *headers, const void *body, int n) {
    if (status < 100 || status > 599 || n < 0 || n > NET_CAP || (n && !body)) return 0;
    pthread_mutex_lock(&MU); Conn *c = accOf(rid); int r = 0;
    if (c && c->kind == K_REQ) {
        c->rst = status; c->rhead = headers ? sdup(headers) : NULL; c->rn = n;
        if (n) { c->rbody = malloc((size_t)n); memcpy(c->rbody, body, (size_t)n); }
        c->done = 1; r = 1; pthread_cond_broadcast(&CV);
    }
    pthread_mutex_unlock(&MU); return r;
}

/* ================= jdh.h 의 함수 ================= */
int jdh_http_request(const char *method, const char *url, const char *headers, const void *body, int n) {
    if (!url || !(startsWith(url, "http://") || startsWith(url, "https://")) || strlen(url) > 3900 || n < 0 || n > NET_CAP) return 0;
    if (!method || !*method || strlen(method) > 12) return 0;
    pthread_mutex_lock(&MU);
    Conn *c = slotCount(0) < NET_CLI ? slotNew(K_HTTP) : NULL; int id = 0;
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
    Conn *c = slotCount(0) < NET_CLI ? slotNew(K_WS) : NULL; int id = 0;
    if (c) { c->url = sdup(url); id = c->id; if (!spawn(wsThread, c)) { slotFree(c); id = 0; } }
    pthread_mutex_unlock(&MU);
    return id;
}
int jdh_ws_state(int id) { pthread_mutex_lock(&MU); Conn *c = slotOf(id, K_WS); int r = c ? c->wstate : -1; pthread_mutex_unlock(&MU); return r; }
int jdh_ws_send(int id, const void *data, int n, int text) {
    if (n < 0 || n > NET_CAP || (n && !data)) return 0;
    pthread_mutex_lock(&MU); Conn *c = slotOf(id, K_WS); int r = c && c->wstate == 1 && (c->srv ? (c->sk != -1 && srvFrame((sk_t)c->sk, text ? 1 : 2, data, n)) : wsSendRaw(c, data, n, text)); pthread_mutex_unlock(&MU); return r;
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
    if (c && c->srv) { if (c->running) { c->released = 1; if (c->sk != -1) { srvFrame((sk_t)c->sk, 8, "\x03\xe8", 2); shutdown((sk_t)c->sk, 2); } } else slotFree(c); }
    else if (c) { if (c->running) { c->released = 1; wsShut(c); } else slotFree(c); }
    pthread_mutex_unlock(&MU);
}
