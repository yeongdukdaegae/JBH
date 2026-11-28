/* 사용자 라이브러리 (JDH) — 실행기와 함께 컴파일된 C 구현을 엔진에 잇는 표
 *   표는 jdh build exe 가 프로젝트 ext 폴더의 .jbx 선언을 보고 만든다 (라이브러리가 없으면 ext_none.c 의 빈 표).
 */
#ifndef JDH_NATIVE_H
#define JDH_NATIVE_H
enum { JDH_T_INT = 1, JDH_T_FLOAT, JDH_T_STRING, JDH_T_BIT, JDH_T_COLOR, JDH_T_MAT12, JDH_T_LIST, JDH_T_MAT, JDH_T_COMPOUND };   /* COMPOUND = 값 전용 (C 는 JSON 글자를 돌려줌) */
/* 인자 한 칸 — mat12 는 d 두 칸 (x, y) · 리스트 [a, b] 는 항목마다 한 칸 · [형식, ...] 는 p (배열) · i (개수) · 행렬은 p (행 순서로 행 × 열) 뒤에 크기를 ... 로 쓴 것만 i (행 · 열) */
typedef union { int i; double d; const char *s; const void *p; } JdhArg;
/* 인자 형식 — LIST: el[0 .. n-1] (var = 1 이면 [el[0], ...]) · MAT: r · c (0 = ... 아무 크기) */
typedef struct { int t, n, var, el[8], r, c; } JdhPT;
#define JDH_MAXSLOT 64
typedef struct { const char *name; int np; JdhPT p[8]; void (*call)(const JdhArg *a); } JdhCmd;
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
