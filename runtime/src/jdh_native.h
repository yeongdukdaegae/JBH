/* 사용자 라이브러리 (JDH) — 실행기와 함께 컴파일된 C 구현을 엔진에 잇는 표
 *   표는 jdh build exe 가 프로젝트의 ext/*.jbx 선언을 보고 만든다 (라이브러리가 없으면 ext_none.c 의 빈 표).
 */
#ifndef JDH_NATIVE_H
#define JDH_NATIVE_H
enum { JDH_T_INT = 1, JDH_T_FLOAT, JDH_T_STRING, JDH_T_BIT, JDH_T_COLOR, JDH_T_MAT12 };
typedef union { int i; double d; const char *s; } JdhArg;   /* mat12 는 d 두 칸 (x, y) */
typedef struct { const char *name; int np; int p[8]; void (*call)(const JdhArg *a); } JdhCmd;
typedef struct { const char *name; int ty; void (*get)(JdhArg *out); } JdhVal;
typedef struct {
    const char *id;
    const JdhCmd *cmds; int ncmds;
    const JdhVal *vals; int nvals;
    void (*init)(void); void (*frame)(double dt); void (*stop)(void);
} JdhLib;
extern const JdhLib JDH_LIBS[];
extern const int JDH_NLIBS;
void jdh_log(const char *text);
void jdh_error(const char *text);
#endif
