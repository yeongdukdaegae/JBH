/* jdh.h — JawBreaker 사용자 라이브러리 (C 로 한 번만 작성)
 *
 *   같은 C 파일이 두 곳에서 쓰입니다.
 *     · 웹 (JawBreaker Dev. 실행 · HTML · APK): jdh lib build 가 WebAssembly 로 컴파일 — 격리된 공간에서 실행
 *     · EXE: 실행기와 함께 컴파일
 *
 *   명령어:  JDH_COMMAND(add, int n) { total += n; }            코드에서  별칭.add(3);
 *            JDH_COMMAND(reset, void) { total = 0; }             인자가 없으면 void
 *   값:      JDH_VALUE(int, total) { return total; }             코드에서  별칭.total
 *   선택:    JDH_INIT() { }   JDH_FRAME(dt) { }   JDH_STOP() { }
 *
 *   선언(jdl.json)의 형식 → C 의 형식
 *     int · bit → int      float → double      string · color → const char * (UTF-8 · 그 명령이 끝날 때까지만 유효)
 *     mat12 → double 두 개 (x, y)
 *     리스트 (항목 형식은 int · float · string · bit)
 *       "[int, string]"  (항목 수가 정해짐 · 8개까지) → 항목마다 하나씩:  int a, const char *b
 *       "[int, ...]"     (몇 개든 · 0개도)          → 배열과 개수:  const int *items, int n
 *                        float → const double *  · string → const char *const *  · bit → const int * (0 · 1)
 *     행렬 (숫자 · 행 순서: m[(행-1) * 열 수 + (열-1)])
 *       "matrix[2, 3]"   → const double *m
 *       크기 자리에 ... 를 쓰면 그 크기가 뒤에 붙음:  "matrix[..., 3]" → const double *m, int rows
 *                        "matrix[2, ...]" → const double *m, int cols    "matrix[..., ...]" → const double *m, int rows, int cols
 *     예)  "params": ["[float, ...]"]   →   JDH_COMMAND(avg, const double *items, int n) { … }
 *     배열 · 글자는 그 명령이 끝날 때까지만 유효합니다 (바꾸지 말고 읽기만 · 남겨 두려면 복사).
 *     정해진 항목 수와 다르거나 항목 형식이 틀리면 엔진이 그 줄에서 오류를 냅니다 (int 는 소수점 아래를 버림).
 *   값의 형식:  int · bit → int      float → double      string → const char *
 *               compound → const char * (JSON 글자 — 엔진 1.6 부터):  "[1, \"a\", [2, 3]]" → compound
 *                 jaw.loadJson 과 같은 규칙: [ ] · 숫자 · 문자열 · true/false (1/0) · null (0) · 맨 바깥이 배열이 아니면 [값] · 빈 글자 = []
 *                 { } (객체) 나 올바르지 않은 JSON 은 그 값을 읽는 줄에서 오류
 */
#ifndef JDH_H
#define JDH_H
#ifdef __cplusplus
extern "C" {
#endif

#ifdef __wasm__
#define JDH__EXPORT(n) __attribute__((export_name(#n)))
#define JDH__IMPORT(n) __attribute__((import_module("jdh"), import_name(#n)))
#else
#define JDH__EXPORT(n)
#define JDH__IMPORT(n)
#endif

/* 엔진이 주는 것 */
JDH__IMPORT(log) void jdh_log(const char *text);       /* 디버그 로그에 한 줄 (print 와 같은 곳 · 내보낸 EXE 에서는 화면에 안 보임) */
JDH__IMPORT(error) void jdh_error(const char *text);   /* 오류로 실행을 멈춤 */

/* ---- 아래는 선언(jdl.json)의 "permissions" 에 적은 것만 쓸 수 있습니다 (안 적고 쓰면 jdh lib check 가 오류) ----
 *   좌표 · 크기는 오브젝트와 같은 화면 단위: (0, 0) = 화면 가운데 · 위쪽이 + · 카메라를 따름
 *   색은 "#rgb" · "#rrggbb" (오브젝트의 Color 와 같은 꼴)
 *   웹(Dev. · HTML · APK)에서는 엔진과 메시지로 주고받으므로 한 박자 늦을 수 있습니다:
 *     읽기 = 가장 최근에 받은 상태 · 쓰기 · 그리기 = 이 명령(또는 프레임)이 끝난 뒤 반영.  EXE 에서는 바로 반영됩니다.
 */

/* "draw" — 라이브러리마다 그리기 층 하나. 지울 때까지 남아 있음 (매 프레임 다시 그리려면 JDH_FRAME 에서 clear 후 그림)
 *   그린 것이 놓이는 곳은 jdh_draw_layer 로 고릅니다 (그 뒤에 그리는 것부터 · 고르지 않으면 JDH_LAYER_OWN · 엔진 1.4 부터):
 *     JDH_LAYER_OWN      오브젝트 위 · UI 아래 (n 은 쓰지 않음)
 *     JDH_LAYER_OBJECT   오브젝트 사이: n = dis.setObjLayer 와 같은 레이어 번호 — 번호가 n 이하인 오브젝트 위 · n 보다 큰 오브젝트 아래
 *     JDH_LAYER_UI       화면에 고정 (카메라를 따르지 않음 · 배율 1) · UI 오브젝트 위 (n 은 쓰지 않음)
 *   한 층 안에서는 그린 순서대로 (나중 것이 위). jdh_draw_clear 는 어느 곳에 놓였든 이 라이브러리가 그린 것을 모두 지웁니다 (고른 곳은 그대로). */
#define JDH_LAYER_OWN 0
#define JDH_LAYER_OBJECT 1
#define JDH_LAYER_UI 2
JDH__IMPORT(draw_layer) void jdh_draw_layer(int layer, int n);
JDH__IMPORT(draw_clear) void jdh_draw_clear(void);
JDH__IMPORT(draw_rect) void jdh_draw_rect(double x, double y, double w, double h, const char *color);      /* 가운데 (x, y) · 너비 · 높이 */
JDH__IMPORT(draw_circle) void jdh_draw_circle(double x, double y, double w, double h, const char *color);  /* 가운데 (x, y) · 타원 */
JDH__IMPORT(draw_line) void jdh_draw_line(double x1, double y1, double x2, double y2, double width, const char *color);
JDH__IMPORT(draw_text) void jdh_draw_text(double x, double y, double size, const char *text, const char *color);   /* 가운데 (x, y) · size = 글자 높이 (dis.setText 와 같음) */

/* "objects.read" — 있으면 1 (값을 out 에), 없으면 0 · 글자는 snprintf 처럼 (돌려주는 값 = 전체 길이, 없으면 -1) */
JDH__IMPORT(obj_exists) int jdh_obj_exists(const char *id);
JDH__IMPORT(obj_pos) int jdh_obj_pos(const char *id, double *x, double *y);
JDH__IMPORT(obj_scale) int jdh_obj_scale(const char *id, double *w, double *h);
JDH__IMPORT(obj_vel) int jdh_obj_vel(const char *id, double *vx, double *vy);
JDH__IMPORT(obj_color) int jdh_obj_color(const char *id, char *out, int n);
JDH__IMPORT(obj_text) int jdh_obj_text(const char *id, char *out, int n);   /* 글자 오브젝트만 */

/* "objects.write" — 없는 오브젝트면 오류로 실행을 멈춤 */
JDH__IMPORT(obj_set_pos) void jdh_obj_set_pos(const char *id, double x, double y);
JDH__IMPORT(obj_set_scale) void jdh_obj_set_scale(const char *id, double w, double h);
JDH__IMPORT(obj_set_vel) void jdh_obj_set_vel(const char *id, double vx, double vy);
JDH__IMPORT(obj_set_color) void jdh_obj_set_color(const char *id, const char *color);
JDH__IMPORT(obj_set_text) void jdh_obj_set_text(const char *id, const char *text);   /* 글자 오브젝트만 */

/* "files.read" — 이 프로젝트의 파일을 읽기만 (경로는 프로젝트 기준 · ext/ 는 못 읽음)
 *   웹에서는 사용자가 허락한 경우에만 열립니다 (허락하지 않았으면 없는 파일처럼 -1) */
JDH__IMPORT(file_size) int jdh_file_size(const char *path);                 /* 바이트 수 · 없으면 -1 */
JDH__IMPORT(file_read) int jdh_file_read(const char *path, void *buf, int n);   /* 앞에서부터 n 바이트까지 · 읽은 바이트 수 · 없으면 -1 */

/* "network" — HTTP 요청과 WebSocket (엔진 1.5 부터)
 *   모두 기다리지 않습니다: 걸어 두고, 다음 명령이나 JDH_FRAME 에서 상태를 물어봅니다 (같은 명령 안에서는 아직 진행 중).
 *   웹 (Dev. · HTML · APK) 에서는 사용자가 허락한 경우에만 됩니다 (허락하지 않았으면 0 을 돌려줌 = 실패).
 *   웹에서 HTTP 는 브라우저 규칙을 따릅니다: 서버가 다른 곳에서의 요청을 허용 (CORS · Access-Control-Allow-Origin) 해야 응답을 읽을 수 있고,
 *     https 로 연 페이지에서는 http · ws 주소에 접속할 수 없습니다.  EXE 에는 이 제한이 없습니다.
 *   한도: 동시에 64개 · 응답 본문과 WebSocket 에 쌓인 메시지는 16MB 까지.  다 쓴 것은 close 로 돌려줍니다. */
/* HTTP: 요청을 시작하고 번호를 받음 (0 = 시작하지 못함). headers 는 "이름: 값\n이름: 값" (없으면 NULL) · body 는 n 바이트 (없으면 NULL, 0) */
JDH__IMPORT(http_request) int jdh_http_request(const char *method, const char *url, const char *headers, const void *body, int n);
JDH__IMPORT(http_status) int jdh_http_status(int id);     /* -1 = 진행 중 · 0 = 실패 (연결하지 못함 · 막힘) · 그 밖 = HTTP 상태 (200 · 404 …) */
JDH__IMPORT(http_size) int jdh_http_size(int id);         /* 응답 본문의 바이트 수 (끝나기 전에는 0) */
JDH__IMPORT(http_read) int jdh_http_read(int id, int offset, void *buf, int n);   /* 본문의 offset 부터 n 바이트까지 복사 · 복사한 바이트 수 */
JDH__IMPORT(http_close) void jdh_http_close(int id);      /* 이 요청을 잊음 (진행 중이면 결과를 버림) */
/* WebSocket: 연결을 시작하고 번호를 받음 (0 = 시작하지 못함) */
JDH__IMPORT(ws_open) int jdh_ws_open(const char *url);    /* "ws://…" · "wss://…" */
JDH__IMPORT(ws_state) int jdh_ws_state(int id);           /* 0 = 연결 중 · 1 = 열림 · 2 = 닫힘 · -1 = 실패 */
JDH__IMPORT(ws_send) int jdh_ws_send(int id, const void *data, int n, int text);   /* text = 1: 글자 (UTF-8) · 0: 바이트 — 보냈으면 1 */
JDH__IMPORT(ws_peek) int jdh_ws_peek(int id);             /* 다음 메시지의 바이트 수 · 없으면 -1 */
JDH__IMPORT(ws_recv) int jdh_ws_recv(int id, void *buf, int n);   /* 다음 메시지를 꺼냄 (n 바이트까지 복사 · 넘는 부분은 버림) — 메시지의 바이트 수 · 없으면 -1 */
JDH__IMPORT(ws_close) void jdh_ws_close(int id);          /* 닫고 잊음 (닫힌 뒤에도 남은 메시지는 close 전까지 읽을 수 있음) */

/* "server" — 이 PC 의 포트를 열고 접속을 받는 서버 (엔진 1.6 부터 · EXE 에서만)
 *   웹 (Dev. · HTML · APK) 은 브라우저 안이라 포트를 열 수 없습니다: jdh_srv_listen 이 늘 0 (실패).
 *   한 포트에서 HTTP 요청과 WebSocket 접속을 같이 받습니다 (Upgrade: websocket 이면 WebSocket).
 *   https · wss 로 받으려면 jdh_srv_listen_tls (인증서 .pfx 가 필요 · 그 밖은 같음).
 *   모두 기다리지 않습니다: 받은 것은 쌓아 두고, 명령이나 JDH_FRAME 에서 jdh_srv_next · jdh_srv_ws 로 꺼냅니다.
 *   HTTP: 요청 하나 = 번호 하나. jdh_req_respond 로 답하면 그 번호는 끝 (30초 안에 답하지 않으면 엔진이 503 으로 답함 · 연결은 요청마다 닫힘).
 *   WebSocket: jdh_srv_ws 가 준 번호는 jdh_ws_state · send · peek · recv · close 로 씁니다 (클라이언트로 연 것과 같음).
 *   한도: 서버 8개 · 받은 접속 (요청 + WebSocket) 동시에 192개 · 요청 머리 64KB · 본문 16MB (Content-Length 가 있는 것만 · chunked 는 411). */
#define JDH_SRV_LOCAL 1      /* flags: 이 PC 안 (127.0.0.1) 에서 오는 접속만 받음 · 0 = 모든 주소 (방화벽이 물을 수 있음) */
JDH__IMPORT(srv_listen) int jdh_srv_listen(int port, int flags);   /* 포트를 열고 서버 번호 (0 = 실패: 포트를 쓰는 중 · 막힘 · 웹) · port 0 = 비어 있는 아무 포트 */
/* https · wss 로 받는 서버 (Windows 의 SChannel): pfx = 인증서와 개인 키가 든 .pfx 파일 · password = 그 비밀번호 (없으면 "")
 *   pfx 경로는 먼저 프로젝트 파일 (EXE 에 들어감 — 개인 키가 실행 파일 안에 들어가므로 배포할 때 주의) · 없으면 디스크 (절대 경로 · 지금 폴더 · 실행 파일 폴더 기준)
 *   0 = 실패 (파일 없음 · 비밀번호 틀림 · 개인 키 없음 · 포트를 쓰는 중 · 웹).  받은 요청 · WebSocket 은 jdh_srv_listen 과 같은 함수로 씁니다.
 *   개인 키는 여는 동안 이 사용자의 Windows 키 저장소에 두었다가 서버를 닫을 때 지웁니다 (강제로 끄면 남을 수 있음). */
JDH__IMPORT(srv_listen_tls) int jdh_srv_listen_tls(int port, int flags, const char *pfx, const char *password);
JDH__IMPORT(srv_port) int jdh_srv_port(int sid);           /* 실제로 연 포트 (port 0 으로 연 경우) · 없으면 0 */
JDH__IMPORT(srv_close) void jdh_srv_close(int sid);        /* 더는 받지 않음 (이미 받은 요청 · WebSocket 은 그대로 씀) */
JDH__IMPORT(srv_next) int jdh_srv_next(int sid);           /* 다음 HTTP 요청 번호 · 없으면 0 */
JDH__IMPORT(srv_ws) int jdh_srv_ws(int sid);               /* 새로 들어온 WebSocket 번호 · 없으면 0 (꺼내기 전에 온 메시지도 쌓여 있음) */
#define JDH_REQ_METHOD 0     /* "GET" · "POST" … */
#define JDH_REQ_PATH 1       /* "/a/b?x=1" (주소의 경로 · 물음표 뒤까지) */
#define JDH_REQ_HEADERS 2    /* "이름: 값\n이름: 값" */
#define JDH_REQ_ADDR 3       /* 보낸 쪽 "1.2.3.4:5678" */
/* 요청 (또는 서버가 받은 WebSocket) 의 정보를 글자로 — snprintf 처럼 (돌려주는 값 = 전체 길이 · 없는 번호면 -1) */
JDH__IMPORT(req_info) int jdh_req_info(int id, int what, char *out, int n);
JDH__IMPORT(req_size) int jdh_req_size(int rid);           /* 요청 본문의 바이트 수 */
JDH__IMPORT(req_read) int jdh_req_read(int rid, int offset, void *buf, int n);   /* 본문의 offset 부터 n 바이트까지 · 복사한 바이트 수 */
/* 답함: status (200 · 404 …) · headers 는 "이름: 값\n…" (없으면 NULL · Content-Type 이 없으면 text/plain; charset=utf-8) · body n 바이트 — 1 = 보냄 */
JDH__IMPORT(req_respond) int jdh_req_respond(int rid, int status, const char *headers, const void *body, int n);

#ifdef __wasm__
#define JDH_COMMAND(name, ...) JDH__EXPORT(jdh_cmd_##name) void jdh_cmd_##name(__VA_ARGS__)
#define JDH_VALUE(type, name) JDH__EXPORT(jdh_val_##name) type jdh_val_##name(void)
#define JDH_INIT() JDH__EXPORT(jdh_init) void jdh_init(void)
#define JDH_FRAME(dt) JDH__EXPORT(jdh_frame) void jdh_frame(double dt)
#define JDH_STOP() JDH__EXPORT(jdh_stop) void jdh_stop(void)
#else
/* EXE: 여러 라이브러리가 한 실행 파일에 들어가므로 이름 앞에 라이브러리 표시(JDH_LIB · jdh build exe 가 정함)를 붙임 */
#ifndef JDH_LIB
#define JDH_LIB lib
#endif
#define JDH__N3(l, k, n) jdh_##l##_##k##_##n
#define JDH__N2(l, k, n) JDH__N3(l, k, n)
#define JDH__H3(l, n) jdh_##l##_##n
#define JDH__H2(l, n) JDH__H3(l, n)
#define JDH_COMMAND(name, ...) void JDH__N2(JDH_LIB, cmd, name)(__VA_ARGS__)
#define JDH_VALUE(type, name) type JDH__N2(JDH_LIB, val, name)(void)
#define JDH_INIT() void JDH__H2(JDH_LIB, init)(void)
#define JDH_FRAME(dt) void JDH__H2(JDH_LIB, frame)(double dt)
#define JDH_STOP() void JDH__H2(JDH_LIB, stop)(void)
#endif

#ifdef __wasm__
#include <stdlib.h>
/* 엔진이 문자열 · 리스트 · 행렬 인자를 넣어 두는 자리 (명령마다 비움 · 8바이트 맞춤) — 건드리지 않아도 됩니다
 *   64KB 를 넘는 것은 malloc 으로 따로 잡았다가 다음 명령 때 돌려줌 */
JDH__EXPORT(jdh_scratch) __attribute__((weak)) char *jdh_scratch(int n) {
    static char buf[65536] __attribute__((aligned(8))); static int used; static void **big; static int nbig, capbig;
    if (n < 0) { used = 0; for (int i = 0; i < nbig; i++) free(big[i]); nbig = 0; return buf; }
    n = (n + 7) & ~7;
    if (n <= (int)sizeof buf - used) { char *p = buf + used; used += n; return p; }
    if (nbig == capbig) { void **nb = (void **)realloc(big, sizeof(void *) * (capbig ? capbig * 2 : 8)); if (!nb) return 0; big = nb; capbig = capbig ? capbig * 2 : 8; }
    char *p = (char *)malloc((size_t)n); if (!p) return 0;
    big[nbig++] = p; return p;
}
#endif

#ifdef __cplusplus
}
#endif
#endif
