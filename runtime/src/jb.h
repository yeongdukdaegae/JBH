/* Jawbreaker — C 판 (인터프리터)
 *   편집기(JawBreaker.html)와 같은 문법 · 같은 검사 · 같은 실행 규칙으로 .jbs · .jb · .jbl 프로젝트를 실행한다.
 *   DisPlayer · colliders · Canvas · Magellan · soundMaster · SeongBin 은 SDL2 로 (화면 · 입력 · 소리).
 */
#ifndef JB_H
#define JB_H

#include <stddef.h>
#include <stdint.h>

/* ---------- 오류 ---------- */
void jb_fail(const char *file, int line, const char *fmt, ...);
void *jb_alloc(size_t n);
void *jb_calloc(size_t n);
void *jb_realloc(void *p, size_t n);
char *jb_strdup(const char *s);
char *jb_strndup(const char *s, size_t n);
char *jb_fmt(const char *fmt, ...);        /* malloc 한 글자 */
const char *jb_intern(const char *s, size_t n);
const char *jb_in(const char *s);          /* jb_intern(s, strlen(s)) */

/* ---------- 토큰 ---------- */
enum { T_INT, T_FLOAT, T_STR, T_ID, T_KW, T_TYPE, T_OP, T_EOF };
typedef struct { int t, line; const char *v; double num; char raw[32]; } Tok;
Tok *jb_lex(const char *src, const char *file, int *count);

/* ---------- 값 (숫자는 JS 처럼 모두 double · 형식은 검사기가 정함) ---------- */
enum { V_NUM, V_STR, V_MAT, V_LIST, V_DATA };
typedef struct List List;
typedef struct Mat Mat;
typedef struct Data Data;
typedef struct { int k; union { double n; char *s; Mat *m; List *l; Data *dt; } u; } Value;
struct Data { unsigned char *b; size_t n; int dev, bind, rc; };   /* dev: 0 = 바이트 · 1 = 카메라 · 2 = 마이크 (귀속 · 읽을 때마다 지금 바이트) · rc: 같이 쓰는 값의 수 (복사 없이 나눠 씀 · 고칠 때 rc > 1 이면 새로 만듦) */
struct Mat { int r, c; double *d; };
struct List { int n, cap; Value *items; int comp; };

Value v_num(double n);
Value v_str(const char *s);
Value v_strown(char *s);
Value v_list(int comp);
Value v_mat(int r, int c);
Value v_copy(Value v);          /* 안쪽까지 복사 (JS deepCopy) */
Value v_own(Value v);           /* 리스트에 넣을 때 (객체면 복사) */
void v_free(Value *v);
int v_eq(Value a, Value b);     /* JS: a===b || deepEq */
void list_push(List *l, Value v);
char *fmt_float(double x);
char *fmt_any(Value v);
char *fmt_val(Value v, int ty);
char *json_of(Value v);
int json_parse(const char *s, Value *out);   /* 0 = JSON 아님 · -1 = { } 객체 */
char *kind_txt(Value v);
Value v_data(const unsigned char *b, size_t n);
Value v_data_own(unsigned char *b, size_t n);
Value to_data(Value v);                                       /* 무엇이든 → data */
int from_data(Value d, int ty, const int *dims, Value *out);   /* data → 형식 · 안 맞으면 0 */
unsigned char *data_bytes(Value v, size_t *n, int *own);

/* ---------- 형식 ---------- */
enum { TY_NONE, TY_INT, TY_FLOAT, TY_STRING, TY_BIT, TY_MATRIX, TY_LIST, TY_COMPOUND, TY_ANY,
       TY_MAT12, TY_COLOR, TY_JSON, TY_DATA };   /* 뒤의 셋은 명령 매개변수 전용 */
const char *ty_name(int ty);
static inline int isL(int t) { return t == TY_LIST || t == TY_COMPOUND; }

/* ---------- AST ---------- */
enum {
    N_NUM, N_STR, N_BOOL, N_ID, N_MEMBER, N_CALL, N_IDX, N_BIN, N_UN, N_MAT,
    N_OBJPROP, N_OBJKEEP, N_OBJVAR, N_CANOBJ,
    N_VAR, N_ASSIGN, N_EASSIGN, N_OASSIGN, N_IF, N_LOOP, N_RUN, N_FUNC, N_USE, N_LIB,
    N_DPART, N_DSET   /* data.이름.getPart(시작, 끝) · data.이름.setPart(시작, 끝, 값); */
};
/* 연산자: '+','-','*','/','%','<','>','l'(<=),'g'(>=),'e'(==),'n'(!=),'&'(&&),'|'(||),'!' */
/* 오브젝트 속성 키 */
enum { K_POS = 1, K_SCALE, K_COLOR, K_TEXT, K_VEL, K_ISTRIG, K_TRIGID, K_ISCOL, K_COLID, K_ANIFR };
/* 변수 정보 (검사기) */
enum { VK_PUBLIC = 1, VK_PVAR, VK_GLOBAL, VK_TVAR, VK_RVAR, VK_LOCAL };
typedef struct Info {
    const char *name; int ty; int dims[2]; int hasDims;
    int kind; const char *mod; int runK; int param;
    const char *file, *clash; int line; int used;
} Info;
/* 명령의 대상 */
enum { M_NONE, M_JAW, M_LIST, M_BELONG, M_DIS, M_COL, M_MAG, M_CAN, M_SND, M_CAM, M_USER, M_EXT, M_DEM };

typedef struct Module Module;
typedef struct Node Node;
typedef struct { Node **v; int n, cap; } NodeList;
typedef struct { int ty; int dims[2]; const char *name; Info *info; } Param;

struct Node {
    int k, line;
    const char *file;
    /* 검사 결과 */
    int ty; int dims[2]; int hasDims;
    int chk; int chkDims[2]; int hasChkDims;   /* 리스트 항목(any) 실행 확인 */
    int anyInt, anyBase, anyMat, isList;
    Info *info;
    /* 식 */
    double num; int isFloat; char raw[32];
    char *str; int lit;
    const char *name, *lib;           /* id · call · member */
    const char *id;                   /* objprop · objvar · canobj : 오브젝트 id (NULL = 자기 자신) */
    const char *prop; int key;        /* objprop / canobj 속성 */
    int keepK, keepI;                 /* objkeep : 속성(K_POS·K_SCALE·K_VEL) · 성분 0/1 */
    const char *colLib; int btn;      /* objprop colLib · canobj btn (0 · 'c' click · 'p' push · 'r' rel) */
    int op;
    Node *a, *b, *c;                  /* bin l r · un e · idx base i j · eassign target i j */
    NodeList args;                    /* call · mat 성분 (한 줄로) */
    int nrows, *rowLen;               /* mat */
    int empty, asList, comp;          /* mat */
    /* call 부가 */
    const char *sub; int canObj, objId;
    Node *relChild, *relParent, *saveAs, *toExpr, *forExpr, *easeExpr;
    const char *asName; int asLine; Info *asInfo;
    int target; Module *tmod; Node *tdef;   /* 대상 */
    const void *ext; const char *extLib;   /* M_EXT: 사용자 라이브러리 (JDH) 의 명령어 · 값 */
    int lfind, canKey, fileEx, isMath, isStrf, splitOK;
    int permit;   /* jaw.hasPermit */
    int readF;    /* jaw.readFile */
    Node *lnode;                      /* 리스트 명령 · .length · .find 의 리스트 변수 */
    int ret, retTy, retDims[2];       /* return(값) */
    Node *jump; int jumpIsBreak;      /* break() · return() 대상 */
    int jumpTarget;
    const char *taskPath, *imgPath, *fontPath, *tlPath;
    const char **folderNames, **folderPaths; int nfolder;
    Info *jinfo; const char *jname;
    int belongOk; const char *gotoLabel;
    /* 문장 */
    Node *expr;                       /* var init · assign · eassign · oassign 값 · if cond · loop cond */
    Node *ms;
    NodeList body, then, els; int hasEls;
    int elseIf;
    int pub;
    Param *params; int nparams;
    const char *alias, *libId, *parent;
};

typedef struct { const char *alias; const char *lib; } UseEnt;
struct Module {
    const char *name;       /* 라이브러리 id · 코드 파일은 "@경로" */
    const char *file;
    int isLib;
    NodeList ast;
    UseEnt uses[64]; int nuses;
    const char *parentId;
    /* 검사기 */
    struct InfoMap *globals;
    Node **funcs; int nfuncs;
    int bound, sceneBound;
    struct { int scene; const char *id; } belong[16]; int nbelong;
    struct { const char *name; int line; } labels[512]; int nlabels; int labelsBuilt;
    int *stmtLines; int nstmt; int stmtBuilt;
};
const char *mod_use(Module *m, const char *alias);   /* 별칭 → 라이브러리 id (없으면 NULL) */
int mod_uses_lib(Module *m, const char *lib);

NodeList jb_parse(Tok *toks, int ntok, const char *file, int isLib);
void nl_push(NodeList *l, Node *n);
Node *node_new(int k, int line, const char *file);

/* ---------- 프로젝트 ---------- */
typedef struct { char *path; char *text; size_t len; } SrcFile;
typedef struct { const char *id; const char **files; int n; } Belong;
typedef struct {
    char *root;
    char *name, *main;
    SrcFile *files; int nfiles;
    Module **libs; int nlibs;
    Module **tasks; int ntasks;
    Info **publics; int npublics;
    Belong *belongs; int nbelongs;            /* 오브젝트 id → 귀속 파일 */
    Belong *sceneBelongs; int nsceneBelongs;  /* 장면 → 파일 */
    struct { const char *id; Info **v; int n; } *objVars; int nobjVars;
    Info **parentVars; int nparentVars;
} Project;
extern Project P;
SrcFile *proj_file(const char *path);
const char *proj_text(const char *path);
char *path_resolve(const char *raw, const char *fromFile);
Module *lib_by_id(const char *id);
Module *task_by_path(const char *path);
void jb_build(void);                      /* 불러오기 · 검사 */
void jb_check(Module *m);

/* ---------- 실행 ---------- */
void jb_run(void);
double jb_now(void);
extern const char *JB_EXE_DIR;

/* 수학 · 문자열 함수 */
int math_n(const char *name);             /* 매개변수 개수 (-1 = 없음) */
const char *math_r(const char *name);     /* "float" "int" "same" "num" */
double math_call(const char *name, double *x);
int strf_n(const char *name, const char **sig, int *rty);
Value strf_call(const char *name, Value *a);

/* 키 이름 */
const char *key_canon(const char *n, char *buf, size_t bn);

#endif
