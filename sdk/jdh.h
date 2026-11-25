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
JDH__IMPORT(log) void jdh_log(const char *text);       /* 디버그 로그에 한 줄 */
JDH__IMPORT(error) void jdh_error(const char *text);   /* 오류로 실행을 멈춤 */

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
