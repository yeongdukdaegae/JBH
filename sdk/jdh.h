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
 *   값의 형식:  int · bit → int      float → double      string → const char *
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

/* "draw" — 라이브러리마다 그리기 층 하나: 오브젝트 위 · UI 아래. 지울 때까지 남아 있음 (매 프레임 다시 그리려면 JDH_FRAME 에서 clear 후 그림) */
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
/* 엔진이 문자열 인자를 넣어 두는 자리 (명령마다 비움) — 건드리지 않아도 됩니다 */
JDH__EXPORT(jdh_scratch) __attribute__((weak)) char *jdh_scratch(int n) {
    static char buf[65536]; static int used;
    if (n < 0) { used = 0; return buf; }
    if (n > (int)sizeof buf - used) return 0;
    char *p = buf + used; used += n; return p;
}
#endif

#ifdef __cplusplus
}
#endif
#endif
