/* 프로그램 만들기 · 실행 전 검사 — 편집기의 buildProgram() · check() 를 옮김 */
#include "jb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

/* ---------- 이름 → Info ---------- */
typedef struct InfoMap { Info **v; int n, cap; } InfoMap;
static InfoMap *im_new(void) { return jb_calloc(sizeof(InfoMap)); }
static Info *im_get(InfoMap *m, const char *n) { for (int i = 0; i < m->n; i++) if (m->v[i]->name == n) return m->v[i]; return NULL; }
static void im_set(InfoMap *m, Info *i) {
    for (int q = 0; q < m->n; q++) if (m->v[q]->name == i->name) { m->v[q] = i; return; }
    if (m->n == m->cap) { m->cap = m->cap ? m->cap * 2 : 8; m->v = jb_realloc(m->v, sizeof(Info*) * m->cap); }
    m->v[m->n++] = i;
}
static Info *info_new(const char *name, int ty, const int *dims) {
    Info *i = jb_calloc(sizeof(Info)); i->name = name; i->ty = ty; i->runK = -1;
    if (dims) { i->dims[0] = dims[0]; i->dims[1] = dims[1]; i->hasDims = 1; }
    return i;
}

/* ---------- 내장 라이브러리 명령 ---------- */
#include "jdh_native.h"
/* 사용자 라이브러리 (JDH): 실행기와 함께 컴파일된 것 */
static const JdhLib *ext_find(const char *id) { for (int i = 0; i < JDH_NLIBS; i++) if (!strcmp(JDH_LIBS[i].id, id)) return &JDH_LIBS[i]; return NULL; }
static int ext_ty(int t) { return t == JDH_T_INT ? TY_INT : t == JDH_T_FLOAT ? TY_FLOAT : t == JDH_T_BIT ? TY_BIT : t == JDH_T_COLOR ? TY_COLOR : t == JDH_T_MAT12 ? TY_MAT12 : t == JDH_T_LIST ? TY_LIST : t == JDH_T_MAT ? TY_MATRIX : TY_STRING; }
#define DIS_ID "DisPlayer"
#define COL_ID "colliders"
#define CAN_ID "Canvas"
#define MAG_ID "Magellan"
#define SND_ID "soundMaster"
#define CAM_ID "SeongBin"
#define DEM_ID "deviceManager"
typedef struct { const char *name; int n; int p[6]; } Spec;
enum { I_ = TY_INT, F_ = TY_FLOAT, S_ = TY_STRING, B_ = TY_BIT, M12 = TY_MAT12, C_ = TY_COLOR, L_ = TY_LIST, A_ = TY_ANY, J_ = TY_JSON };
static const Spec DIS_FUNCS[] = {
    {"waitF",1,{I_}},{"setDisplay",0},{"setDisplayColor",1,{S_}},{"setFont",1,{S_}},{"setupObject",0},
    {"setObject",4,{M12,S_,M12,C_}},{"setObjScale",2,{S_,M12}},{"setObjPos",2,{S_,M12}},
    {"getVar",2,{S_,S_}},{"moveObj",1,{S_}},
    {"setText",6,{M12,S_,S_,F_,B_,C_}},{"nudgeObj",4,{S_,M12,F_,F_}},{"setTextNudge",2,{S_,S_}},{"setObjNudge",2,{S_,S_}},
    {"enableScreenScale",0},{"disableScreenScale",0},{"setParent",0},{"setLink",2,{S_,S_}},{"removeObj",1,{S_}},
    {"escParent",1,{S_}},{"splitParent",1,{S_}},{"setObjLayer",2,{S_,I_}},{"ascendObjLayer",2,{S_,I_}},{"setTxtContent",2,{S_,S_}},{"tagObj",2,{S_,S_}},
    {"setObjCyl",4,{M12,S_,F_,C_}},{"setObjPlg",6,{M12,M12,M12,M12,S_,C_}},{"setObjAlpha",2,{S_,I_}},
    {"disableObj",1,{S_}},{"enableObj",1,{S_}},{"setObjColor",2,{S_,C_}},{"setObjOverlay",3,{S_,C_,I_}},{"setGlow",3,{S_,C_,F_}},
    {"rotateObj",3,{S_,F_,I_}},{"setRenderLimit",1,{I_}},{"setTilemap",2,{M12,S_}},{NULL}};
static const Spec CAN_FUNCS[] = {{"setUi",1,{S_}},{"setasButton",1,{S_}},{"bindInput",1,{S_}},{"setUpScene",0},{"nameScene",1,{S_}},{"setScene",1,{S_}},
    {"sendtoScene",1,{S_}},{"popupScene",1,{S_}},{"closeScene",0},{"obj.setExistableScene",2,{S_,S_}},{NULL}};
static const Spec MAG_FUNCS[] = {{"importFile",1,{S_}},{"importFolder",1,{S_}},{"setObjTexture",4,{S_,S_,M12,M12}},
    {"setasTexture",3,{S_,M12,M12}},{"wearTexture",2,{S_,S_}},{"setasAnime",3,{L_,I_,S_}},
    {"obj.applyAnime",2,{S_,S_}},{"obj.runAnime",3,{S_,S_,F_}},{"obj.breakAnime",1,{S_}},{NULL}};
static const Spec SND_FUNCS[] = {{"setSpeaker",1,{S_}},{"playSfx",3,{S_,S_,I_}},{"setVol",2,{S_,I_}},
    {"repeatingPlay",3,{S_,S_,I_}},{"repeatPart",4,{S_,S_,I_,I_}},{"repatPart",4,{S_,S_,I_,I_}},{"introPart",4,{S_,S_,I_,I_}},{"break",1,{S_}},{"setFadein",2,{S_,I_}},{"setFadeout",2,{S_,I_}},
    {"spk.belongto",2,{S_,S_}},{"spk.belongtoScene",2,{S_,S_}},{NULL}};
static const Spec CAM_FUNCS[] = {{"setUpCam",0},{"setCam",1,{S_}},{"setMainCamas",1,{S_}},
    {"cam.setZoom",2,{S_,F_}},{"cam.moveCamto",2,{S_,M12}},{"cam.follow",2,{S_,S_}},{"cam.followOut",1,{S_}},{NULL}};
static const Spec COL_FUNCS[] = {{"setVelocity",2,{S_,M12}},{"setVelocityX",2,{S_,F_}},{"setVelocityY",2,{S_,F_}},{"setAnchor",2,{S_,B_}},
    {"setCollider",5,{M12,M12,S_,F_,B_}},{"setColider",5,{M12,M12,S_,F_,B_}},
    {"setColliderVisible",1,{B_}},{"setGravity",1,{F_}},{"setTrigger",4,{M12,M12,S_,B_}},{"setLink",2,{S_,S_}},{NULL}};
static const Spec BUILTINS[] = {{"log",1,{A_}},{"print",1,{A_}},{"wait",1,{I_}},{"terminate",0},{"sendto",1,{S_}},
    {"break",0},{"return",0},{"goTo",1,{I_}},{"label",1,{S_}},{"setup",1,{S_}},
    {"keyClick",1,{S_}},{"keyPush",1,{S_}},{"keyRlse",1,{S_}},
    {"setJson",2,{S_,J_}},{"genJson",2,{S_,S_}},{"loadJson",2,{S_,S_}},{"exportFile",2,{S_,S_}},{"openFile",1,{S_}},{"getPermit",1,{S_}},{"copyFileData",1,{S_}},{"overWriteFile",2,{A_,S_}},{NULL}};
static const Spec *spec(const Spec *t, const char *n) { for (; t->name; t++) if (!strcmp(t->name, n)) return t; return NULL; }
static char *specList(const Spec *t, const char *pre, int objStyle) {
    size_t cap = 1024, L = 0; char *s = jb_alloc(cap); s[0] = 0;
    for (int i = 0; t[i].name; i++) {
        char b[256];
        const char *n = t[i].name;
        if (objStyle == 1 && !strncmp(n, "obj.", 4)) snprintf(b, sizeof b, "%s%sobj.id.%s", i ? ", " : "", pre, n + 4);
        else if (objStyle == 2 && !strncmp(n, "spk.", 4)) snprintf(b, sizeof b, "%s%sspk.id.%s", i ? ", " : "", pre, n + 4);
        else if (objStyle == 3 && !strncmp(n, "cam.", 4)) snprintf(b, sizeof b, "%s%scam.id.%s", i ? ", " : "", pre, n + 4);
        else snprintf(b, sizeof b, "%s%s%s", i ? ", " : "", pre, n);
        size_t bl = strlen(b); if (L + bl + 1 > cap) { cap = (L + bl + 1) * 2; s = jb_realloc(s, cap); }
        memcpy(s + L, b, bl + 1); L += bl;
    }
    return s;
}
int is_builtin_lib(const char *l) {
    return !strcmp(l, "jawbreaker") || !strcmp(l, DIS_ID) || !strcmp(l, COL_ID) || !strcmp(l, CAN_ID) || !strcmp(l, MAG_ID) || !strcmp(l, SND_ID) || !strcmp(l, CAM_ID) || !strcmp(l, DEM_ID);
}
const char *mod_use(Module *m, const char *alias) { for (int i = 0; i < m->nuses; i++) if (m->uses[i].alias == alias || !strcmp(m->uses[i].alias, alias)) return m->uses[i].lib; return NULL; }
int mod_uses_lib(Module *m, const char *lib) { for (int i = 0; i < m->nuses; i++) if (!strcmp(m->uses[i].lib, lib)) return 1; return 0; }

/* ---------- 공통 ---------- */
static int HEX_ok(const char *v) {
    if (*v == '#') v++;
    size_t L = strlen(v); if (L != 3 && L != 6) return 0;
    for (size_t i = 0; i < L; i++) if (!isxdigit((unsigned char)v[i])) return 0;
    return 1;
}
int jb_hex_ok(const char *v) { return HEX_ok(v); }
static int assignable(int to, int from) {
    if (to == TY_DATA || from == TY_DATA) return to == from;
    if (to == TY_MATRIX || from == TY_MATRIX || isL(to) || isL(from)) return to == from;
    if (to == TY_STRING) return from == TY_STRING;
    if (from == TY_STRING) return 0;
    if (to == TY_INT) return from == TY_INT || from == TY_BIT;
    if (to == TY_BIT) return from == TY_BIT || from == TY_INT;
    return 1;
}
int jb_assignable(int to, int from) { return assignable(to, from); }
static int isNum(int t) { return t == TY_INT || t == TY_FLOAT || t == TY_BIT; }
Module *lib_by_id(const char *id) { for (int i = 0; i < P.nlibs; i++) if (!strcmp(P.libs[i]->name, id)) return P.libs[i]; return NULL; }
Module *task_by_path(const char *path) { for (int i = 0; i < P.ntasks; i++) if (!strcmp(P.tasks[i]->file, path)) return P.tasks[i]; return NULL; }
static Node *mod_func(Module *m, const char *n) { for (int i = 0; i < m->nfuncs; i++) if (m->funcs[i]->name == n || !strcmp(m->funcs[i]->name, n)) return m->funcs[i]; return NULL; }
static Module *resolveFunc(const char *id, const char *n, Node **def) {
    for (Module *m = lib_by_id(id); m; m = m->parentId ? lib_by_id(m->parentId) : NULL) { Node *f = mod_func(m, n); if (f) { *def = f; return m; } }
    return NULL;
}
static Info *resolveVar(const char *id, const char *n) {
    for (Module *m = lib_by_id(id); m; m = m->parentId ? lib_by_id(m->parentId) : NULL) { if (m->globals) { Info *v = im_get(m->globals, n); if (v) return v; } }
    return NULL;
}
static Info *publicOf(const char *n) { for (int i = 0; i < P.npublics; i++) if (P.publics[i]->name == n) return P.publics[i]; return NULL; }
static Info *parentVarOf(const char *n) { for (int i = 0; i < P.nparentVars; i++) if (P.parentVars[i]->name == n) return P.parentVars[i]; return NULL; }
static Info **objVarsOf(const char *id, int *n) { for (int i = 0; i < P.nobjVars; i++) if (!strcmp(P.objVars[i].id, id)) { *n = P.objVars[i].n; return P.objVars[i].v; } *n = 0; return NULL; }
int jb_parse_nudge(const char *v, int *nx, int *ny);
int jb_ease_ok(const char *v);
int obj_prop_key(const char *p);
static int strieq(const char *a, const char *b) { for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; return !*a && !*b; }
/* getVar 이름: "coord" / "scale[1, 2]" */
int jb_parse_objvar(const char *v, int *key, int *idx) {
    const char *p = v; while (*p == ' ' || *p == '\t') p++;
    char w[64]; int n = 0; while (isalpha((unsigned char)*p) && n < 63) w[n++] = *p++; w[n] = 0;
    if (!n) return 0;
    while (*p == ' ' || *p == '\t') p++;
    idx[0] = idx[1] = 0;
    if (*p == '[') {
        p++; while (*p == ' ') p++;
        if (!isdigit((unsigned char)*p)) return 0; idx[0] = (int)strtol(p, (char**)&p, 10);
        while (*p == ' ') p++; if (*p != ',') return 0; p++; while (*p == ' ') p++;
        if (!isdigit((unsigned char)*p)) return 0; idx[1] = (int)strtol(p, (char**)&p, 10);
        while (*p == ' ') p++; if (*p != ']') return 0; p++;
        while (*p == ' ' || *p == '\t') p++;
    }
    if (*p) return 0;
    int k = obj_prop_key(w); if (!k || k == K_ISCOL || k == K_COLID) return 0;
    *key = k; return 1;
}
static int STR_KEY(int k) { return k == K_COLOR || k == K_TEXT || k == K_TRIGID || k == K_COLID; }

/* ---------- 검사기 ---------- */
typedef struct { int kind; Node *node; } CS;   /* 0 if · 1 loop */
static __thread Module *M;
static __thread int curLine, objCtx, runK, runN;
static __thread Node *curFn;
#define E(n, ...) jb_fail(M->file, (n)->line, __VA_ARGS__)
#define EL(l, ...) jb_fail(M->file, (l), __VA_ARGS__)

static Info *look(const char *n, InfoMap **sc, int ns) {
    for (int i = ns - 1; i >= 0; i--) { Info *v = im_get(sc[i], n); if (v) return v; }
    Info *v = M->isLib && M->parentId ? resolveVar(M->parentId, n) : NULL;
    Info *pv = v ? v : publicOf(n); if (pv) return pv;
    if (M->bound) {
        Info *i = parentVarOf(n);
        if (i && i->clash) jb_fail(M->file, curLine, "부모 변수 '%s' 의 형식이 파일마다 달라 정할 수 없습니다 (%s · %s)", n, i->file, i->clash);
        if (i) { i->used = 1; return i; }
    }
    return NULL;
}
static int ex(Node *e, InfoMap **sc, int ns);
static const char *tyTxt(int t, Node *e) {
    static __thread char b[4][64]; static __thread int k;
    if (t == TY_MATRIX && e && e->hasDims) { k = (k + 1) & 3; snprintf(b[k], 64, "matrix[%d, %d]", e->dims[0], e->dims[1]); return b[k]; }
    return ty_name(t);
}
static int isNumA(Node *e, int t, int w) { if (t == TY_ANY) { e->chk = w ? w : TY_FLOAT; return 1; } return isNum(t); }
static int fit(Node *e, int t, int to) {
    if (t == TY_ANY) { if (to == TY_MATRIX) return 0; e->chk = to; return 1; }
    if (to == TY_INT && t == TY_FLOAT && e && e->anyInt) return 1;
    return assignable(to, t);
}
static Info *listOf(const char *n, InfoMap **sc, int ns) { if (mod_use(M, n)) return NULL; Info *i = look(n, sc, ns); return i && isL(i->ty) ? i : NULL; }
static Node *lnode(const char *name, Info *info, int line) { Node *n = node_new(N_ID, line, M->file); n->name = name; n->info = info; n->ty = info->ty; return n; }
static Module *userFn(Node *c, Node **def) {
    if (!c->lib) return M->isLib ? resolveFunc(M->name, c->name, def) : NULL;
    const char *al = mod_use(M, c->lib);
    if (!al || is_builtin_lib(al)) return NULL;
    return resolveFunc(al, c->name, def);
}
static void needMat(Node *e, int t, const int *dims, const char *what) {
    if (t == TY_ANY) { e->chk = TY_MATRIX; e->chkDims[0] = dims[0]; e->chkDims[1] = dims[1]; e->hasChkDims = 1; e->dims[0] = dims[0]; e->dims[1] = dims[1]; e->hasDims = 1; return; }
    if (t != TY_MATRIX || !e->hasDims || e->dims[0] != dims[0] || e->dims[1] != dims[1]) E(e, "%s 에는 matrix[%d, %d] 행렬이 필요합니다 (받은 값: %s)", what, dims[0], dims[1], tyTxt(t, e));
}
static void fnArgs(Node *c, Node *def, InfoMap **sc, int ns) {
    if (c->args.n != def->nparams) E(c, "'%s' 은(는) 값 %d개가 필요합니다 (받은 값: %d개)", c->name, def->nparams, c->args.n);
    for (int i = 0; i < c->args.n; i++) {
        Node *a = c->args.v[i]; Param *pp = &def->params[i]; int pt = pp->ty;
        if (isL(pt) && a->k == N_MAT) { a->asList = 1; a->comp = pt == TY_COMPOUND; }
        int t = ex(a, sc, ns);
        if (pt == TY_DATA || t == TY_DATA) continue;   /* 함수의 data 매개변수 ↔ 무엇이든 */
        if (pt == TY_MATRIX) { char w[128]; snprintf(w, sizeof w, "'%s' 의 %d번째 값", c->name, i + 1); needMat(a, t, pp->dims, w); continue; }
        if (!fit(a, t, pt)) E(a, "'%s' 의 %d번째 값은 %s 형식이어야 합니다 (받은 형식: %s)", c->name, i + 1, ty_name(pt), tyTxt(t, a));
    }
}
static int wantComp(Node *x, InfoMap **sc, int ns);
static int strish(Node *y, InfoMap **sc, int ns) {
    if (y->k == N_STR) return 1;
    if (y->k == N_BIN && y->op == '+') return strish(y->a, sc, ns) || strish(y->b, sc, ns);
    if (y->k == N_MAT) return wantComp(y, sc, ns);
    if (y->k == N_ID) { Info *i = look(y->name, sc, ns); return i && (i->ty == TY_STRING || isL(i->ty) || i->ty == TY_MATRIX); }
    if (y->k == N_CALL) { const char *sig; int r; return strf_n(y->name, &sig, &r) >= 0 && r == TY_STRING; }
    return 0;
}
static int wantComp(Node *x, InfoMap **sc, int ns) {
    if (x->k != N_MAT) return 0;
    if (x->empty) return 1;
    if (x->nrows != 1) return 0;
    for (int i = 0; i < x->args.n; i++) if (strish(x->args.v[i], sc, ns)) return 1;
    return 0;
}
static void compMark(Node *x, InfoMap **sc, int ns) { if (wantComp(x, sc, ns)) { x->asList = 1; x->comp = 1; } }
/* data.이름.… 의 이름: 선언된 data 변수 · 번호 인자: int */
static void dataVar(Node *e, InfoMap **sc, int ns) {
    Info *i = look(e->name, sc, ns);
    if (!i) E(e, "변수 '%s' 이(가) 선언되지 않았습니다 (var data call %s; 로 먼저 만드세요)", e->name, e->name);
    if (i->ty != TY_DATA) E(e, "getPart · setPart 는 data 변수에만 쓸 수 있습니다 ('%s' 은(는) %s)", e->name, ty_name(i->ty));
    e->info = i;
}
static void idxInt(Node *a, int t, const char *nm, int i) {
    if (t == TY_ANY) { a->chk = TY_INT; return; }
    if (t == TY_INT || t == TY_BIT || (t == TY_FLOAT && a->anyInt)) return;
    E(a, "'%s' 의 %d번째 값은 바이트 번호 (int) 입니다 (받은 형식: %s)", nm, i + 1, ty_name(t));
}
static void itemOk(Node *a, int t, int comp) {
    if (t == TY_DATA) E(a, "data 는 리스트 · compound 에 넣을 수 없습니다 — 다른 형식으로 바꿔서 넣으세요");
    if (comp) return;
    if (t == TY_ANY) { a->chk = -1; return; }   /* -1 = scalar */
    if (t == TY_MATRIX || isL(t)) E(a, "리스트 항목에는 숫자나 문자열만 넣을 수 있습니다 (받은 형식: %s) — 행렬 · 리스트까지 넣으려면 compound 를 쓰세요", tyTxt(t, a));
}
static void idxCheck(Node *e, InfoMap **sc, int ns, const int *dims) {
    Node *xs[2] = {e->b, e->c}; const char *w[2] = {"행", "열"};
    for (int q = 0; q < 2; q++) {
        Node *x = xs[q]; int n = dims ? dims[q] : 0;
        int t = ex(x, sc, ns);
        if (t == TY_ANY) { x->chk = TY_INT; continue; }
        if (t == TY_FLOAT && x->anyInt) continue;
        if (t != TY_INT && t != TY_BIT) E(x, "행렬의 %s 번호는 int 여야 합니다 (받은 형식: %s)", w[q], ty_name(t));
        if (n && x->k == N_NUM && (x->num < 1 || x->num > n)) E(x, "행렬의 %s 번호 %s 이(가) 범위(1~%d)를 벗어났습니다", w[q], x->raw, n);
    }
}
static void colorCheck(Node *a, int t, const char *what) {
    if (t == TY_ANY) { a->chk = TY_STRING; return; }
    if (t != TY_STRING && t != TY_INT && t != TY_BIT) E(a, "%s 은(는) 헥스코드여야 합니다 (예: \"#ff8800\" 또는 114514)", what);
    if (a->k == N_STR && !HEX_ok(a->str)) E(a, "색은 헥스코드여야 합니다 (예: \"#1e90ff\", \"#fff\") — 받은 값: \"%s\"", a->str);
    if (a->k == N_NUM) {
        const char *r = a->raw; size_t L = strlen(r); int dig = 1; for (size_t i = 0; i < L; i++) if (!isdigit((unsigned char)r[i])) dig = 0;
        if (!(dig && (L == 3 || L == 6))) E(a, "숫자로 쓴 색은 3자리나 6자리여야 합니다 (예: 114514 → #114514) — 받은 값: %s", r);
    }
}
/* 리스트 · 행렬 인자 (검사 때 알 수 있는 것만 · 나머지는 실행 때 run.c 의 ext_call) — 에디터와 같은 문구 */
static void ext_arg(Node *c, int i, Node *a, int t, const JdhPT *pt) {
    if (pt->t == JDH_T_LIST) {
        if (t != TY_ANY && !isL(t)) E(a, "'%s' 의 %d번째 값은 리스트여야 합니다 (받은 형식: %s)", c->name, i + 1, tyTxt(t, a));
        if (!pt->var && a->k == N_MAT && a->asList && a->args.n != pt->n) E(a, "'%s' 의 %d번째 값은 항목이 %d개인 리스트여야 합니다 (받은 것: %d개)", c->name, i + 1, pt->n, a->args.n);
        return;
    }
    if (t != TY_ANY && t != TY_MATRIX) E(a, "'%s' 의 %d번째 값은 행렬이어야 합니다 (받은 형식: %s)", c->name, i + 1, tyTxt(t, a));
    if (t == TY_MATRIX && a->hasDims && ((pt->r && a->dims[0] != pt->r) || (pt->c && a->dims[1] != pt->c))) {
        char want[64]; snprintf(want, sizeof want, "matrix[%s%.0d, %s%.0d]", pt->r ? "" : "...", pt->r, pt->c ? "" : "...", pt->c);
        E(a, "'%s' 의 %d번째 값은 %s 행렬이어야 합니다 (받은 값: matrix[%d, %d])", c->name, i + 1, want, a->dims[0], a->dims[1]);
    }
}
/* jaw.getPermit · hasPermit 의 "cam" · "mic" · "file" (따옴표 안이면 검사 때 확인) */
static void permitLit(Node *a) { if (a->k == N_STR && strcmp(a->str, "cam") && strcmp(a->str, "mic") && strcmp(a->str, "file")) E(a, "권한은 \"cam\" · \"mic\" · \"file\" 중 하나입니다 (받은 값: \"%s\")", a->str); }
static void aliasErr(const char *name, Node *n) { E(n, "'%s' 은(는) 라이브러리 별칭이 아닙니다 (파일 위쪽에 'Use 라이브러리id call %s' 이 필요합니다)", name, name); }
static void needDis(Node *e, const char *what) { if (!mod_uses_lib(M, DIS_ID)) E(e, "%s", what); }

static int ex(Node *e, InfoMap **sc, int ns) {
    switch (e->k) {
    case N_NUM:
        if (!e->isFloat && e->num > 2147483647) E(e, "int 범위(최대 2147483647)를 넘는 숫자입니다");
        return e->ty = e->isFloat ? TY_FLOAT : TY_INT;
    case N_STR: return e->ty = TY_STRING;
    case N_BOOL: return e->ty = TY_BIT;
    case N_DPART: {   /* data.이름.getPart(시작, 끝) → data (0 부터 · 끝은 빠짐) */
        dataVar(e, sc, ns);
        if (e->args.n != 2) E(e, "'getPart' 은(는) 값 2개가 필요합니다 (받은 값: %d개)", e->args.n);
        for (int i = 0; i < 2; i++) idxInt(e->args.v[i], ex(e->args.v[i], sc, ns), "getPart", i);
        return e->ty = TY_DATA;
    }
    case N_ID: {
        curLine = e->line;
        Info *i = look(e->name, sc, ns);
        if (!i) {
            if (mod_use(M, e->name)) E(e, "'%s' 은(는) 라이브러리 별칭입니다 — '%s.명령어' 형태로 쓰세요", e->name, e->name);
            E(e, "변수 '%s' 이(가) 선언되지 않았습니다 (var 형식 call %s; 으로 먼저 만드세요)", e->name, e->name);
        }
        e->info = i; if (i->hasDims) { e->dims[0] = i->dims[0]; e->dims[1] = i->dims[1]; e->hasDims = 1; }
        return e->ty = i->ty;
    }
    case N_MAT: {
        if (e->asList) {
            if (e->nrows != 1) E(e, "리스트는 [a, b, c] 처럼 한 줄로 씁니다 ('|' 는 쓸 수 없음)");
            for (int q = 0; q < e->args.n; q++) { Node *x = e->args.v[q]; if (e->comp) compMark(x, sc, ns); itemOk(x, ex(x, sc, ns), e->comp); }
            return e->ty = e->comp ? TY_COMPOUND : TY_LIST;
        }
        if (e->empty) E(e, "빈 행렬은 쓸 수 없습니다");
        for (int q = 0; q < e->args.n; q++) {
            Node *x = e->args.v[q]; int t = ex(x, sc, ns);
            if (!isNumA(x, t, 0)) E(x, "행렬에는 숫자(float)만 넣을 수 있습니다 (받은 형식: %s)", tyTxt(t, x));
        }
        e->dims[0] = e->nrows; e->dims[1] = e->rowLen[0]; e->hasDims = 1;
        return e->ty = TY_MATRIX;
    }
    case N_IDX: {
        int t = ex(e->a, sc, ns);
        if (t == TY_ANY && e->c) { idxCheck(e, sc, ns, NULL); e->anyBase = 1; return e->ty = TY_FLOAT; }
        if (isL(t) || t == TY_ANY) {
            if (e->c) E(e, "리스트 항목은 [번호] 하나로 씁니다 (예: list[2])");
            if (t == TY_ANY) e->anyBase = 1;
            int it = ex(e->b, sc, ns);
            if (it == TY_ANY) e->b->chk = TY_INT;
            else if (it != TY_INT && it != TY_BIT && !(it == TY_FLOAT && e->b->anyInt)) E(e->b, "리스트 번호는 int 여야 합니다 (받은 형식: %s)", tyTxt(it, e->b));
            return e->ty = TY_ANY;
        }
        if (t != TY_MATRIX) E(e, "[행, 열] 은 행렬에만 쓸 수 있습니다 (받은 형식: %s)", ty_name(t));
        if (!e->c) E(e, "행렬 성분은 [행, 열] 두 번호로 씁니다 (예: m[1, 2])");
        idxCheck(e, sc, ns, e->a->hasDims ? e->a->dims : NULL); return e->ty = TY_FLOAT;
    }
    case N_OBJPROP:
        if (e->colLib) {
            const char *al = mod_use(M, e->colLib); if (!al) aliasErr(e->colLib, e);
            if (e->key == K_ANIFR) { if (strcmp(al, MAG_ID)) E(e, "'%s.obj.….%s' 는 Magellan 라이브러리 값입니다 ('%s' 은(는) %s)", e->colLib, e->prop, e->colLib, al); }
            else if (strcmp(al, COL_ID)) E(e, "'%s.obj.….%s' 는 colliders 라이브러리 값입니다 ('%s' 은(는) %s)", e->colLib, e->prop, e->colLib, al);
        }
        if (!e->id && !M->bound) E(e, "obj.%s 처럼 id 없이 쓰려면 이 파일이 dis.BelongObj 로 오브젝트에 귀속되어 있어야 합니다 (아니면 obj.id.%s)", e->prop, e->prop);
        if (STR_KEY(e->key)) return e->ty = TY_STRING;
        if (e->key == K_ISTRIG || e->key == K_ISCOL) return e->ty = TY_BIT;
        if (e->key == K_ANIFR) return e->ty = TY_INT;
        e->dims[0] = 1; e->dims[1] = 2; e->hasDims = 1; return e->ty = TY_MATRIX;
    case N_OBJKEEP:
        if (!objCtx && !M->bound) E(e, "obj.%s 은(는) dis.setObjPos / setObjScale / setObject / col.setVelocity 의 값 안이나, dis.BelongObj 로 오브젝트에 귀속된 파일에서만 쓸 수 있습니다", e->name);
        return e->ty = TY_FLOAT;
    case N_OBJVAR: {
        int nv; Info **vm = objVarsOf(e->id, &nv); Info *i = NULL;
        for (int q = 0; q < nv; q++) if (vm[q]->name == e->name) i = vm[q];
        if (!i) {
            if (vm) {
                char b[1024] = ""; for (int q = 0; q < nv; q++) { if (q) strncat(b, ", ", sizeof b - strlen(b) - 1); strncat(b, vm[q]->name, sizeof b - strlen(b) - 1); }
                E(e, "오브젝트 '%s' 에 귀속된 파일에 변수 '%s' 이(가) 없습니다 (있는 변수: %s)", e->id, e->name, nv ? b : "없음");
            }
            E(e, "'%s' 은(는) 오브젝트 속성이 아닙니다 (Position, Scale, Velocity, Color, Text, Xpos, Ypos, Xscale, Yscale, Xvel, Yvel, IsTrigged, TriggedId) — 귀속된 파일의 변수라면 dis.BelongObj 로 '%s' 에 파일을 귀속시키세요", e->name, e->id);
        }
        e->info = i; if (i->hasDims) { e->dims[0] = i->dims[0]; e->dims[1] = i->dims[1]; e->hasDims = 1; }
        return e->ty = i->ty;
    }
    case N_CANOBJ: {
        const char *al = mod_use(M, e->lib); if (!al) aliasErr(e->lib, e);
        if (strcmp(al, CAN_ID)) E(e, "'%s.obj.….isMouseTouched' 는 Canvas 라이브러리 값입니다 ('%s' 은(는) %s)", e->lib, e->lib, al);
        needDis(e, "Canvas 는 DisPlayer 의 하위 라이브러리라 이 파일에 'Use DisPlayer call 별칭;' 도 필요합니다");
        if (!e->id && !M->bound) E(e, "%s.obj.isMouseTouched 처럼 id 없이 쓰려면 이 파일이 dis.BelongObj 로 오브젝트에 귀속되어 있어야 합니다 (아니면 %s.obj.id.isMouseTouched)", e->lib, e->lib);
        return e->ty = TY_BIT;
    }
    case N_MEMBER: {
        Info *li = listOf(e->lib, sc, ns);
        if (li) {
            if (strcmp(e->name, "length")) E(e, "리스트 '%s' 에서 읽을 수 있는 값은 %s.length (항목 개수) 와 %s.find(값) 입니다", e->lib, e->lib, e->lib);
            e->lnode = lnode(e->lib, li, e->line); return e->ty = TY_INT;
        }
        const char *al = mod_use(M, e->lib); if (!al) aliasErr(e->lib, e);
        if (!strcmp(al, "jawbreaker")) E(e, "jawbreaker 라이브러리에는 변수가 없습니다 (명령어는 %s.%s(...) 로 호출)", e->lib, e->name);
        if (!strcmp(al, COL_ID)) E(e, "colliders 라이브러리에는 변수가 없습니다 (명령어는 %s.%s(...) 로 호출)", e->lib, e->name);
        if (!strcmp(al, MAG_ID)) E(e, "Magellan 라이브러리에는 변수가 없습니다 (명령어는 %s.%s(...) 로 호출)", e->lib, e->name);
        if (!strcmp(al, SND_ID) || !strcmp(al, CAM_ID) || !strcmp(al, DEM_ID)) E(e, "라이브러리 '%s' (부모 포함) 에 변수 '%s' 이(가) 없습니다", al, e->name);
        if (!strcmp(al, CAN_ID)) {
            needDis(e, "Canvas 는 DisPlayer 의 하위 라이브러리라 이 파일에 'Use DisPlayer call 별칭;' 도 필요합니다");
            int t = !strcmp(e->name, "ScreenClicked") ? TY_BIT : !strcmp(e->name, "MouseX") || !strcmp(e->name, "MouseY") ? TY_FLOAT : !strcmp(e->name, "SceneName") ? TY_STRING : 0;
            if (!t) E(e, "Canvas 에 변수 '%s' 이(가) 없습니다 (사용 가능: ScreenClicked, MouseX, MouseY, SceneName)", e->name);
            e->target = M_CAN; return e->ty = t;
        }
        if (!strcmp(al, DIS_ID)) {
            if (strcmp(e->name, "displayX") && strcmp(e->name, "displayY") && strcmp(e->name, "idWidthmode")) E(e, "DisPlayer 에 변수 '%s' 이(가) 없습니다 (사용 가능: displayX, displayY, idWidthmode)", e->name);
            e->target = M_DIS; return e->ty = !strcmp(e->name, "idWidthmode") ? TY_BIT : TY_INT;
        }
        { const JdhLib *xl = ext_find(al);
          if (xl) {
            for (int q = 0; q < xl->nvals; q++) if (!strcmp(xl->vals[q].name, e->name)) { e->target = M_EXT; e->ext = &xl->vals[q]; e->extLib = xl->id; return e->ty = ext_ty(xl->vals[q].ty); }
            E(e, "라이브러리 '%s' 에 값 '%s' 이(가) 없습니다", xl->id, e->name);
          } }
        Info *i = resolveVar(al, e->name);
        if (!i) E(e, "라이브러리 '%s' (부모 포함) 에 변수 '%s' 이(가) 없습니다", al, e->name);
        e->target = M_USER; e->info = i; if (i->hasDims) { e->dims[0] = i->dims[0]; e->dims[1] = i->dims[1]; e->hasDims = 1; }
        return e->ty = i->ty;
    }
    case N_CALL: {
        int mn = math_n(e->name);
        Info *li = e->lib ? listOf(e->lib, sc, ns) : NULL;
        if (li) {
            if (strcmp(e->name, "find")) E(e, "'%s.%s(...)' 은(는) 값을 돌려주지 않습니다 — 식 안에서는 %s.find(값) 과 %s.length 를 씁니다", e->lib, e->name, e->lib, e->lib);
            if (e->args.n != 1) E(e, "'find' 은(는) 값 1개가 필요합니다 (받은 값: %d개)", e->args.n);
            int ft = ex(e->args.v[0], sc, ns); if (ft != TY_ANY) itemOk(e->args.v[0], ft, li->ty == TY_COMPOUND);
            e->lnode = lnode(e->lib, li, e->line); e->lfind = 1; return e->ty = TY_INT;
        }
        if (e->lib && mod_use(M, e->lib) && !strcmp(mod_use(M, e->lib), CAN_ID) && !strcmp(e->name, "isKeyInput"))
            E(e, "isKeyInput 은 jawbreaker 로 옮겼습니다 — jaw.isKeyInput(\"키\") 로 쓰세요 (Use jawbreaker call jaw;)");
        Node *ufd = NULL; Module *uf = userFn(e, &ufd);
        int jaw = e->lib ? (mod_use(M, e->lib) && !strcmp(mod_use(M, e->lib), "jawbreaker")) : !uf;
        if (!strcmp(e->name, "isKeyInput") && jaw) {
            if (e->args.n != 1) E(e, "'isKeyInput' 은(는) 값 1개가 필요합니다 (받은 값: %d개)", e->args.n);
            int kt = ex(e->args.v[0], sc, ns);
            if (!fit(e->args.v[0], kt, TY_STRING)) E(e->args.v[0], "isKeyInput 에는 키 이름(문자열)을 넣습니다 (예: \"a\", \"space\", \"up\") — 받은 형식: %s", tyTxt(kt, e->args.v[0]));
            e->canKey = 1; return e->ty = TY_BIT;
        }
        if (!strcmp(e->name, "fileExists") && jaw) {
            if (e->args.n != 1) E(e, "'fileExists' 은(는) 값 1개가 필요합니다 (받은 값: %d개)", e->args.n);
            int pt = ex(e->args.v[0], sc, ns);
            if (!fit(e->args.v[0], pt, TY_STRING)) E(e->args.v[0], "fileExists 에는 경로(문자열)를 넣습니다 (예: \"save/s.json\") — 받은 형식: %s", tyTxt(pt, e->args.v[0]));
            e->fileEx = 1; return e->ty = TY_BIT;
        }
        if (!strcmp(e->name, "hasPermit") && jaw) {   /* jaw.hasPermit("cam") */
            if (e->args.n != 1) E(e, "'%s' 은(는) 값 %d개가 필요합니다 (받은 값: %d개)", e->name, 1, e->args.n);
            int t = ex(e->args.v[0], sc, ns); if (!fit(e->args.v[0], t, TY_STRING)) E(e->args.v[0], "hasPermit 에는 \"cam\" · \"mic\" · \"file\" 중 하나를 넣습니다 — 받은 형식: %s", tyTxt(t, e->args.v[0])); permitLit(e->args.v[0]);
            e->permit = 1; return e->ty = TY_BIT;
        }
        const char *sig; int sr; int sn = strf_n(e->name, &sig, &sr);
        if (sn >= 0 && jaw) {
            if (e->args.n != sn) E(e, "'%s' 은(는) 값 %d개가 필요합니다 (받은 값: %d개)", e->name, sn, e->args.n);
            for (int i = 0; i < e->args.n; i++) {
                Node *a = e->args.v[i]; int t = ex(a, sc, ns);
                if (sig[i] == 's') { if (!fit(a, t, TY_STRING)) E(a, "'%s' 의 %d번째 값은 문자열이어야 합니다 (받은 형식: %s)", e->name, i + 1, tyTxt(t, a)); }
                else if (!isNumA(a, t, 0)) E(a, "'%s' 의 %d번째 값은 숫자(번호)여야 합니다 (받은 형식: %s)", e->name, i + 1, tyTxt(t, a));
            }
            if (!strcmp(e->name, "splitStr") && !e->splitOK) E(e, "splitStr 은 리스트 변수에 바로 넣을 때만 씁니다 (예: var list call a = splitStr(s, \",\"); 또는 a = splitStr(s, \",\");)");
            e->isStrf = 1; return e->ty = sr;
        }
        if (mn >= 0 && (!e->lib || (mod_use(M, e->lib) && !strcmp(mod_use(M, e->lib), "jawbreaker"))) && !(!e->lib && uf)) {
            if (e->args.n != mn) E(e, "'%s' 은(는) 값 %d개가 필요합니다 (받은 값: %d개)", e->name, mn, e->args.n);
            int anyF = 0, t0 = TY_INT;
            for (int i = 0; i < e->args.n; i++) {
                Node *a = e->args.v[i]; int t = ex(a, sc, ns);
                if (!isNumA(a, t, 0)) E(a, "'%s' 에는 숫자만 넣을 수 있습니다 (받은 형식: %s)", e->name, tyTxt(t, a));
                if (t == TY_ANY) t = TY_FLOAT;
                if (t == TY_FLOAT) anyF = 1;
                if (!i) t0 = t;
            }
            e->isMath = 1;
            const char *r = math_r(e->name);
            if (!strcmp(r, "same")) return e->ty = t0 == TY_FLOAT ? TY_FLOAT : TY_INT;
            if (!strcmp(r, "num")) return e->ty = anyF ? TY_FLOAT : TY_INT;
            return e->ty = !strcmp(r, "int") ? TY_INT : TY_FLOAT;
        }
        if (uf) {
            if (!ufd->retTy) E(e, "함수 '%s' 은(는) 값을 돌려주지 않아서 식 안에서 쓸 수 없습니다 — 'func 형식 %s(...)' 로 만들고 return(값); 으로 돌려주세요", e->name, e->name);
            fnArgs(e, ufd, sc, ns);
            e->target = M_USER; e->tmod = uf; e->tdef = ufd;
            if (ufd->retTy == TY_MATRIX) { e->dims[0] = ufd->retDims[0]; e->dims[1] = ufd->retDims[1]; e->hasDims = 1; }
            return e->ty = ufd->retTy;
        }
        if (e->lib && !mod_use(M, e->lib)) aliasErr(e->lib, e);
        if (e->lib && ext_find(mod_use(M, e->lib))) E(e, "'%s.%s(...)' 은(는) 식 안에서 쓸 수 없습니다 — 격리된 라이브러리의 값은 %s.값이름 으로 읽습니다", e->lib, e->name, e->lib);
        E(e, "'%s' 은(는) 값을 돌려주지 않아서 식 안에서 쓸 수 없습니다 (식 안에서는 수학 함수와 'func 형식 이름(...)' 로 만든 함수만 쓸 수 있음)", e->name);
        return 0;
    }
    case N_UN: {
        int t = ex(e->a, sc, ns);
        if (t == TY_ANY || e->a->anyInt) e->anyInt = 1;
        if (t == TY_ANY) { e->a->chk = TY_FLOAT; t = TY_FLOAT; }
        const char *o = e->op == '!' ? "!" : "-";
        if (isL(t)) E(e, "리스트에는 '%s' 를 쓸 수 없습니다", o);
        if (t == TY_STRING) E(e, "문자열에는 '%s' 를 쓸 수 없습니다", o);
        if (t == TY_MATRIX) E(e, "행렬에는 '%s' 를 쓸 수 없습니다", o);
        if (t == TY_DATA) E(e, "data 에는 '%s' 를 쓸 수 없습니다 — 다른 형식의 변수에 옮겨서 쓰세요 (문자열과 + 로 이어 붙이면 16진수)", o);
        return e->ty = e->op == '!' ? TY_BIT : (t == TY_FLOAT ? TY_FLOAT : TY_INT);
    }
    case N_BIN: {
        int a = ex(e->a, sc, ns), b = ex(e->b, sc, ns), op = e->op;
        const char *os = op == 'l' ? "<=" : op == 'g' ? ">=" : op == 'e' ? "==" : op == 'n' ? "!=" : op == '&' ? "&&" : op == '|' ? "||" : NULL;
        char ob[3] = {(char)op, 0, 0}; if (!os) os = ob;
        if (a == TY_DATA || b == TY_DATA) {   /* data 는 계산하지 않음 — 문자열과 + 로 이으면 16진수 글자 */
            if (op == '+' && (a == TY_STRING || b == TY_STRING)) return e->ty = TY_STRING;
            E(e, "data 에는 '%s' 를 쓸 수 없습니다 — 다른 형식의 변수에 옮겨서 쓰세요 (문자열과 + 로 이어 붙이면 16진수)", os);
        }
        if ((isL(a) || isL(b)) && !(op == '+' && (a == TY_STRING || b == TY_STRING)) && !((op == 'e' || op == 'n') && (a == TY_ANY || b == TY_ANY)))
            E(e, "리스트에는 '%s' 를 쓸 수 없습니다 (항목 list[번호] 는 계산할 수 있고, 문자열과 + 로 이어 붙일 수는 있음)", os);
        if (a == TY_ANY || b == TY_ANY) {
            if (op == '+') {
                if (a == TY_STRING || b == TY_STRING) return e->ty = TY_STRING;
                if (a == TY_MATRIX || b == TY_MATRIX) E(e, "행렬에는 '+' 를 쓸 수 없습니다");
                return e->ty = TY_ANY;
            }
            if (op == 'e' || op == 'n') return e->ty = TY_BIT;
            int w = op == '%' ? TY_INT : TY_FLOAT;
            if ((a == TY_ANY || a == TY_INT || a == TY_BIT) && (b == TY_ANY || b == TY_INT || b == TY_BIT)) e->anyInt = 1;
            if (a == TY_ANY) { e->a->chk = w; a = w; }
            if (b == TY_ANY) { e->b->chk = w; b = w; }
        }
        if ((a == TY_MATRIX || b == TY_MATRIX) && !(op == '+' && (a == TY_STRING || b == TY_STRING)))
            E(e, "행렬에는 '%s' 를 쓸 수 없습니다 (성분 m[행, 열] 은 계산할 수 있고, 문자열과 + 로 이어 붙일 수는 있음)", os);
#define INTISH(x, t) ((t) != TY_FLOAT || (x)->anyInt)
        if (op == '+') {
            if (a == TY_STRING || b == TY_STRING) return e->ty = TY_STRING;
            if (INTISH(e->a, a) && INTISH(e->b, b)) e->anyInt = 1;
            return e->ty = (a == TY_FLOAT || b == TY_FLOAT) ? TY_FLOAT : TY_INT;
        }
        if (op == '-' || op == '*' || op == '/') {
            if (a == TY_STRING || b == TY_STRING) E(e, "문자열에는 '%s' 를 쓸 수 없습니다 (문자열 연결은 + 만 가능)", os);
            if (INTISH(e->a, a) && INTISH(e->b, b)) e->anyInt = 1;
            return e->ty = (a == TY_FLOAT || b == TY_FLOAT) ? TY_FLOAT : TY_INT;
        }
        if (op == '%') {
            if (a == TY_STRING || b == TY_STRING) E(e, "문자열에는 '%%' 를 쓸 수 없습니다");
            if (INTISH(e->a, a) && INTISH(e->b, b)) e->anyInt = 1;
            return e->ty = (a == TY_FLOAT || b == TY_FLOAT) ? TY_FLOAT : TY_INT;
        }
        if (op == 'e' || op == 'n') {
            if ((a == TY_STRING) != (b == TY_STRING)) E(e, "문자열과 숫자는 비교할 수 없습니다 (%s %s %s)", ty_name(a), os, ty_name(b));
            return e->ty = TY_BIT;
        }
        if (a == TY_STRING || b == TY_STRING) E(e, "문자열에는 '%s' 를 쓸 수 없습니다", os);
        return e->ty = TY_BIT;
    }
    }
    return 0;
}

/* goTo 가 닿을 수 있는 블록들: 지금 블록 → 바깥 블록 (run · func 몸통까지) */
typedef struct { NodeList *v[256]; int n; } LS;
static int findReach(NodeList *l, LS *st, Node *target, LS *out) {
    LS st2 = *st; if (st2.n < 256) st2.v[st2.n++] = l;
    for (int i = 0; i < l->n; i++) {
        Node *x = l->v[i];
        if (x == target) { *out = st2; return 1; }
        if (x->k == N_LOOP) { if (findReach(&x->body, &st2, target, out)) return 1; }
        else if (x->k == N_RUN || x->k == N_FUNC) { LS z = {{0}, 0}; if (findReach(&x->body, &z, target, out)) return 1; }
        if (x->k == N_IF) { if (findReach(&x->then, &st2, target, out)) return 1; if (findReach(&x->els, &st2, target, out)) return 1; }
    }
    return 0;
}
static void allLines(NodeList *l) {
    for (int i = 0; i < l->n; i++) {
        Node *x = l->v[i];
        M->stmtLines = jb_realloc(M->stmtLines, sizeof(int) * (M->nstmt + 1)); M->stmtLines[M->nstmt++] = x->line;
        allLines(&x->body); allLines(&x->then); allLines(&x->els);
    }
}
static void walkLabels(NodeList *l) {
    for (int i = 0; i < l->n; i++) {
        Node *x = l->v[i];
        if (x->k == N_CALL && !strcmp(x->name, "label") && x->args.n == 1 && x->args.v[0]->k == N_STR && (!x->lib || (mod_use(M, x->lib) && !strcmp(mod_use(M, x->lib), "jawbreaker")))) {
            for (int q = 0; q < M->nlabels; q++) if (!strcmp(M->labels[q].name, x->args.v[0]->str))
                E(x, "label(\"%s\") 이(가) 두 번 있습니다 (%d번째 줄 · %d번째 줄) — 한 파일 안에서 라벨 이름은 하나씩", x->args.v[0]->str, M->labels[q].line, x->line);
            if (M->nlabels < 512) { M->labels[M->nlabels].name = x->args.v[0]->str; M->labels[M->nlabels].line = x->line; M->nlabels++; }
        }
        walkLabels(&x->body); walkLabels(&x->then); walkLabels(&x->els);
    }
}
static struct { const char *name; int line; } labelSeen[512]; static int nLabelSeen; static Module *labelSeenMod;

static void call(Node *c, InfoMap **sc, int ns, CS *cs, int ncs) {
    const Spec *sp = NULL; int params[64]; int np = 0;
    int ismath = math_n(c->name) >= 0, isstrf; { const char *sg; int r; isstrf = strf_n(c->name, &sg, &r) >= 0; }
    Node *dd;
    int libJaw = !c->lib || (mod_use(M, c->lib) && !strcmp(mod_use(M, c->lib), "jawbreaker"));
    if (ismath && libJaw && !(!c->lib && M->isLib && resolveFunc(M->name, c->name, &dd)))
        E(c, "'%s' 은(는) 값을 돌려주는 함수라 식 안에서 씁니다 (예: var float call y = %s(%s);)", c->name, c->name, math_n(c->name) ? "30" : "");
    if (isstrf && libJaw && !(!c->lib && M->isLib && resolveFunc(M->name, c->name, &dd)))
        E(c, "'%s' 은(는) 값을 돌려주는 함수라 식 안에서 씁니다 (예: var int call n = strLen(\"abc\");)", c->name);
#define SETSPEC(t, mod) do { np = (t)->n; memcpy(params, (t)->p, sizeof(int) * np); c->target = (mod); } while (0)
    if (!c->lib) {
        Node *def = NULL; Module *f = M->isLib ? resolveFunc(M->name, c->name, &def) : NULL;
        if (f) { np = def->nparams; for (int i = 0; i < np && i < 64; i++) params[i] = def->params[i].ty; c->target = M_USER; c->tmod = f; c->tdef = def; }
        else if ((sp = spec(BUILTINS, c->name))) SETSPEC(sp, M_JAW);
        else E(c, "함수 '%s' 을(를) 찾을 수 없습니다", c->name);
    } else {
        Info *li = listOf(c->lib, sc, ns);
        if (li) {
            if (!strcmp(c->name, "find")) E(c, "%s.find(값) 은(는) 번호를 돌려주는 값이라 식 안에서 씁니다 (예: var int call k = %s.find(3);)", c->lib, c->lib);
            const char *ps = !strcmp(c->name, "add") ? "v" : !strcmp(c->name, "insert") ? "iv" : !strcmp(c->name, "remove") ? "i" : !strcmp(c->name, "clear") ? "" : NULL;
            if (!ps) E(c, "리스트 명령어는 add, insert, remove, clear 입니다 (값: find, length) — 받은 이름: %s", c->name);
            if (c->args.n != (int)strlen(ps)) E(c, "'%s' 은(는) 값 %d개가 필요합니다 (받은 값: %d개)", c->name, (int)strlen(ps), c->args.n);
            for (int i = 0; i < c->args.n; i++) {
                Node *a = c->args.v[i];
                if (ps[i] == 'v' && li->ty == TY_COMPOUND) compMark(a, sc, ns);
                int t = ex(a, sc, ns);
                if (ps[i] == 'v') itemOk(a, t, li->ty == TY_COMPOUND);
                else if (t == TY_ANY) a->chk = TY_INT;
                else if (t != TY_INT && t != TY_BIT && !(t == TY_FLOAT && a->anyInt)) E(a, "리스트 번호는 int 여야 합니다 (받은 형식: %s)", tyTxt(t, a));
            }
            c->target = M_LIST; c->lnode = lnode(c->lib, li, c->line);
            return;
        }
        const char *al = mod_use(M, c->lib); if (!al) aliasErr(c->lib, c);
        if ((!strcmp(al, DIS_ID) && !strcmp(c->name, "BelongObj")) || (!strcmp(al, CAN_ID) && !strcmp(c->name, "BelongScene"))) {
            if (!strcmp(al, CAN_ID)) needDis(c, "Canvas 는 DisPlayer 의 하위 라이브러리라 이 파일에 'Use DisPlayer call 별칭;' 도 필요합니다");
            if (!c->belongOk) E(c, "%s 은(는) .jb 파일의 run{ } 바로 안에 씁니다 (if · loop · 함수 안은 안 됨) — 그 파일 전체가 귀속됩니다", c->name);
            c->target = M_BELONG; return;
        }
        if (!strcmp(al, CAN_ID)) {
            needDis(c, "Canvas 는 DisPlayer 의 하위 라이브러리라 이 파일에 'Use DisPlayer call 별칭;' 도 필요합니다");
            char key[128]; snprintf(key, sizeof key, "%s%s", c->canObj ? "obj." : "", c->name);
            sp = spec(CAN_FUNCS, key);
            if (!sp) {
                if (!strcmp(c->name, "setExistableScene")) E(c, "setExistableScene 은 %s.obj.setExistableScene(\"장면\", \"id\"); 로 씁니다", c->lib);
                char pre[64]; snprintf(pre, sizeof pre, "%s.", c->lib);
                E(c, "Canvas 에 '%s%s' 명령어가 없습니다 (사용 가능: %s · 값: ScreenClicked, MouseX, MouseY, SceneName)", c->canObj ? "obj." : "", c->name, specList(CAN_FUNCS, pre, 0));
            }
            SETSPEC(sp, M_CAN);
        } else if (!strcmp(al, SND_ID)) {
            if (!mod_uses_lib(M, MAG_ID) || !mod_uses_lib(M, DIS_ID)) E(c, "soundMaster 는 Magellan(DisPlayer 하위)의 하위 라이브러리라 이 파일에 'Use DisPlayer call 별칭;' 과 'Use Magellan call 별칭;' 도 필요합니다");
            char key[128]; snprintf(key, sizeof key, "%s%s", c->sub && !strcmp(c->sub, "spk") ? "spk." : "", c->name);
            sp = spec(SND_FUNCS, key);
            if (!sp) {
                if (!strcmp(c->name, "belongto") || !strcmp(c->name, "belongSpeakerto")) E(c, "스피커 귀속은 %s.spk.스피커id.belongto(\"오브젝트 id\"); 로 씁니다", c->lib);
                char pre[64]; snprintf(pre, sizeof pre, "%s.", c->lib);
                E(c, "soundMaster 에 '%s%s%s' 명령어가 없습니다 (사용 가능: %s)", c->sub ? c->sub : "", c->sub ? ".id." : "", c->name, specList(SND_FUNCS, pre, 2));
            }
            SETSPEC(sp, M_SND);
        } else if (!strcmp(al, DEM_ID)) {   /* deviceManager — 인자는 data 변수 이름 ("따옴표") */
            static const Spec DEM_FUNCS[] = {{"recordCamAs",1,{S_}},{"recordMicAs",1,{S_}},{"stopRecord",1,{S_}},{NULL}};
            sp = spec(DEM_FUNCS, c->name);
            if (!sp) { char pre[64]; snprintf(pre, sizeof pre, "%s.", c->lib); E(c, "deviceManager 에 '%s' 명령어가 없습니다 (사용 가능: %s)", c->name, specList(DEM_FUNCS, pre, 0)); }
            Node *a = c->args.n ? c->args.v[0] : NULL;
            if (c->args.n != 1 || a->k != N_STR) E(c, "%s 은(는) data 변수 이름을 \"따옴표\" 로 씁니다 (예: %s.%s(\"video\");)", c->name, c->lib, c->name);
            const char *nm = jb_in(a->str); Info *i = look(nm, sc, ns);
            if (!i) E(a, "변수 '%s' 이(가) 선언되지 않았습니다 (var data call %s; 로 먼저 만드세요)", a->str, a->str);
            if (i->ty != TY_DATA) E(a, "%s 은(는) data 변수에만 쓸 수 있습니다 ('%s' 은(는) %s)", c->name, a->str, ty_name(i->ty));
            c->jinfo = i; c->jname = nm;
            SETSPEC(sp, M_DEM);
        } else if (!strcmp(al, CAM_ID)) {
            if (!mod_uses_lib(M, CAN_ID) || !mod_uses_lib(M, DIS_ID)) E(c, "SeongBin 은 Canvas(DisPlayer 하위)의 하위 라이브러리라 이 파일에 'Use DisPlayer call 별칭;' 과 'Use Canvas call 별칭;' 도 필요합니다");
            char key[128]; snprintf(key, sizeof key, "%s%s", c->sub && !strcmp(c->sub, "cam") ? "cam." : "", c->name);
            sp = spec(CAM_FUNCS, key);
            if (!sp) {
                char k2[128]; snprintf(k2, sizeof k2, "cam.%s", c->name);
                if (!c->sub && spec(CAM_FUNCS, k2)) E(c, "%s 은(는) 카메라마다 씁니다 — %s.cam.카메라id.%s(…);", c->name, c->lib, c->name);
                char pre[64]; snprintf(pre, sizeof pre, "%s.", c->lib);
                E(c, "SeongBin 에 '%s%s%s' 명령어가 없습니다 (사용 가능: %s)", c->sub ? c->sub : "", c->sub ? ".id." : "", c->name, specList(CAM_FUNCS, pre, 3));
            }
            SETSPEC(sp, M_CAM);
        } else if (!strcmp(al, MAG_ID)) {
            needDis(c, "Magellan 은 DisPlayer 의 하위 라이브러리라 이 파일에 'Use DisPlayer call 별칭;' 도 필요합니다");
            char key[128]; snprintf(key, sizeof key, "%s%s", c->canObj ? "obj." : "", c->name);
            sp = spec(MAG_FUNCS, key);
            if (!sp) E(c, "Magellan 에 '%s%s' 명령어가 없습니다 (사용 가능: %s)", c->canObj ? "obj." : "", c->name, specList(MAG_FUNCS, "", 1));
            if (c->canObj && !c->objId) {
                if (!M->bound) E(c, "%s.obj.%s(…) 처럼 id 없이 쓰려면 이 파일이 dis.BelongObj 로 오브젝트에 귀속되어 있어야 합니다 (아니면 %s.obj.id.%s(…))", c->lib, c->name, c->lib, c->name);
                Node *s0 = node_new(N_STR, c->line, c->file); s0->str = jb_strdup("");
                nl_push(&c->args, NULL); memmove(&c->args.v[1], &c->args.v[0], sizeof(Node*) * (c->args.n - 1)); c->args.v[0] = s0;
                c->objId = 1;
            }
            SETSPEC(sp, M_MAG);
        } else if (!strcmp(al, "jawbreaker")) {
            sp = spec(BUILTINS, c->name);
            if (!sp) E(c, "jawbreaker 라이브러리에 '%s' 명령어가 없습니다 (사용 가능: %s)", c->name, specList(BUILTINS, "", 0));
            SETSPEC(sp, M_JAW);
        } else if (!strcmp(al, DIS_ID)) {
            sp = spec(DIS_FUNCS, c->name);
            if (!sp) E(c, "DisPlayer 에 '%s' 명령어가 없습니다 (사용 가능: %s)", c->name, specList(DIS_FUNCS, "", 0));
            SETSPEC(sp, M_DIS);
        } else if (!strcmp(al, COL_ID)) {
            needDis(c, "colliders 는 DisPlayer 의 하위 라이브러리라 이 파일에 'Use DisPlayer call 별칭;' 도 필요합니다");
            if (!strcmp(c->name, "setColliderof")) E(c, "setColliderof 는 setLink 로 바뀌었습니다 — dis.setLink(\"id1\", \"id2\"); (둘이 묶여 같이 움직임)");
            char key[128]; snprintf(key, sizeof key, "%s%s", c->canObj ? "obj." : "", c->name);
            sp = spec(COL_FUNCS, key);
            if (!sp) { char pre[64]; snprintf(pre, sizeof pre, "%s.", c->lib); E(c, "colliders 에 '%s%s' 명령어가 없습니다 (사용 가능: %s)", c->canObj ? "obj." : "", c->name, specList(COL_FUNCS, pre, 1)); }
            SETSPEC(sp, M_COL);
        } else if (ext_find(al)) {
            const JdhLib *xl = ext_find(al); const JdhCmd *xc = NULL;
            for (int q = 0; q < xl->ncmds; q++) if (!strcmp(xl->cmds[q].name, c->name)) xc = &xl->cmds[q];
            if (!xc) E(c, "라이브러리 '%s' 에 '%s' 명령어가 없습니다", xl->id, c->name);
            np = xc->np; for (int i = 0; i < np; i++) params[i] = ext_ty(xc->p[i].t);
            c->target = M_EXT; c->ext = xc; c->extLib = xl->id;
        } else {
            Node *def = NULL; Module *f = resolveFunc(al, c->name, &def);
            if (!f) E(c, "라이브러리 '%s' (부모 포함) 에 함수 '%s' 이(가) 없습니다", al, c->name);
            np = def->nparams; for (int i = 0; i < np && i < 64; i++) params[i] = def->params[i].ty;
            c->target = M_USER; c->tmod = f; c->tdef = def;
        }
    }
    int T_ = c->target;
#define IS(mod, nm) (T_ == (mod) && !strcmp(c->name, nm))
    if (IS(M_JAW, "return") && c->args.n == 1) {
        if (!curFn) E(c, "return(값) 은 값을 돌려주는 함수(func 형식 이름(...) { }) 안에서만 씁니다");
        if (!curFn->retTy) E(c, "함수 '%s' 은(는) 값을 돌려주지 않습니다 — 'func 형식 %s(...)' 처럼 돌려줄 형식을 쓰세요", curFn->name, curFn->name);
        Node *a = c->args.v[0];
        if (isL(curFn->retTy) && a->k == N_MAT) { a->asList = 1; a->comp = curFn->retTy == TY_COMPOUND; }
        int t = ex(a, sc, ns);
        if (curFn->retTy == TY_MATRIX) { char w[160]; snprintf(w, sizeof w, "함수 '%s' 이(가) 돌려줄 값", curFn->name); needMat(a, t, curFn->retDims, w); }
        else if (!fit(a, t, curFn->retTy)) E(a, "%s 값을 돌려줄 수 없습니다 (함수 '%s' 은(는) %s 를 돌려줌)", tyTxt(t, a), curFn->name, ty_name(curFn->retTy));
        c->ret = 1; c->retTy = curFn->retTy; c->retDims[0] = curFn->retDims[0]; c->retDims[1] = curFn->retDims[1]; return;
    }
    if (IS(M_SND, "break") && c->args.n == 2) { np = 2; params[0] = TY_STRING; params[1] = TY_BIT; }
    if (IS(M_SND, "break") && c->args.n > 2) E(c, "sou.break 는 (\"스피커\") 또는 (\"스피커\", 페이드 무시 bit) 입니다 (받은 값: %d개)", c->args.n);
    if (IS(M_DIS, "setObjPlg") && c->args.n == 5) { np = 5; params[0] = params[1] = params[2] = TY_MAT12; params[3] = TY_STRING; params[4] = TY_COLOR; }
    if (IS(M_DIS, "setObjPlg") && c->args.n != 5 && c->args.n != 6) E(c, "setObjPlg 는 꼭짓점 3개 또는 4개 다음에 \"id\" · 색을 씁니다 (예: dis.setObjPlg([0, 0], [100, 0], [50, 80], \"tri\", \"#ffffff\");)");
    if (IS(M_JAW, "label") && c->args.n == 1) {
        Node *a = c->args.v[0];
        if (a->k != N_STR) E(a, "label 이름은 따옴표 안의 문자열 상수여야 합니다 (예: label(\"메뉴\");)");
        for (const char *p = a->str; *p; p++) if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == '"' || *p == '\f' || *p == '\v') E(a, "label 이름은 빈칸 없이 한 단어로 씁니다 (받은 값: \"%s\")", a->str);
        if (!*a->str) E(a, "label 이름은 빈칸 없이 한 단어로 씁니다 (받은 값: \"%s\")", a->str);
        if (labelSeenMod != M) { labelSeenMod = M; nLabelSeen = 0; }
        for (int q = 0; q < nLabelSeen; q++) if (!strcmp(labelSeen[q].name, a->str) && labelSeen[q].line != c->line)
            E(a, "label(\"%s\") 이(가) 두 번 있습니다 (%d번째 줄 · %d번째 줄) — 한 파일 안에서 라벨 이름은 하나씩", a->str, labelSeen[q].line, c->line);
        if (nLabelSeen < 512) { labelSeen[nLabelSeen].name = a->str; labelSeen[nLabelSeen].line = c->line; nLabelSeen++; }
    }
    if (IS(M_JAW, "goTo") && c->args.n == 1 && c->args.v[0]->k == N_STR) {
        if (!M->labelsBuilt) { M->labelsBuilt = 1; walkLabels(&M->ast); }
        Node *a = c->args.v[0]; int ln = -1;
        for (int q = 0; q < M->nlabels; q++) if (!strcmp(M->labels[q].name, a->str)) ln = M->labels[q].line;
        if (ln < 0) E(a, "goTo(\"%s\"): 이 파일에 label(\"%s\"); 이(가) 없습니다", a->str, a->str);
        c->gotoLabel = a->str;
        Node *nn = node_new(N_NUM, a->line, a->file); nn->num = ln; snprintf(nn->raw, sizeof nn->raw, "%d", ln);
        c->args.v[0] = nn;
    }
    if (c->args.n != np) E(c, "'%s' 은(는) 값 %d개가 필요합니다 (받은 값: %d개)", c->name, np, c->args.n);
    int isObjSet = (T_ == M_DIS || T_ == M_COL) && (!strcmp(c->name,"setObject")||!strcmp(c->name,"setObjScale")||!strcmp(c->name,"setObjPos")||!strcmp(c->name,"moveObj")||
        !strcmp(c->name,"setText")||!strcmp(c->name,"setVelocity")||!strcmp(c->name,"setVelocityX")||!strcmp(c->name,"setVelocityY")||!strcmp(c->name,"setObjCyl")||!strcmp(c->name,"setObjPlg"));
    for (int i = 0; i < c->args.n; i++) {
        Node *a = c->args.v[i]; int pt = params[i];
        objCtx = isObjSet;
        if (pt == TY_JSON && a->k == N_MAT) { a->asList = 1; a->comp = 1; }
        if (isL(pt) && a->k == N_MAT) { a->asList = 1; a->comp = pt == TY_COMPOUND; }
        int t = ex(a, sc, ns);
        objCtx = 0;
        char w[160]; snprintf(w, sizeof w, "'%s' 의 %d번째 값", c->name, i + 1);
        if (pt == TY_JSON) continue;
        if (T_ == M_EXT && (pt == TY_LIST || pt == TY_MATRIX)) { ext_arg(c, i, a, t, &((const JdhCmd *)c->ext)->p[i]); continue; }
        if (pt == TY_MAT12) { int d[2] = {1, 2}; needMat(a, t, d, w); continue; }
        if (pt == TY_MATRIX && c->tdef) { needMat(a, t, c->tdef->params[i].dims, w); continue; }
        if (c->tdef && (pt == TY_DATA || t == TY_DATA)) continue;   /* 함수의 data 매개변수 ↔ 무엇이든 */
        if (IS(M_MAG, "wearTexture") && i == 1 && t == TY_DATA) continue;   /* mag.wearTexture("id", 데이터) */
        if (pt == TY_COLOR) { char w2[180]; snprintf(w2, sizeof w2, "%s(색)", w); colorCheck(a, t, w2); continue; }
        if (pt != TY_ANY && !fit(a, t, pt)) E(a, "'%s' 의 %d번째 값은 %s 형식이어야 합니다 (받은 형식: %s)", c->name, i + 1, ty_name(pt), tyTxt(t, a));
    }
    if (IS(M_JAW, "getPermit")) permitLit(c->args.v[0]);
    if (IS(M_JAW, "loadJson")) {
        Node *a = c->args.v[1];
        if (a->k != N_STR) E(a, "loadJson 의 두 번째 값은 compound 변수 이름을 \"따옴표\" 로 씁니다 (예: jaw.loadJson(\"save.json\", \"sav\");)");
        const char *nm = jb_in(a->str);
        Info *i = look(nm, sc, ns);
        if (!i) E(a, "변수 '%s' 이(가) 선언되지 않았습니다 (var compound call %s; 로 먼저 만드세요)", a->str, a->str);
        if (i->ty != TY_COMPOUND) E(a, "loadJson 은 compound 변수에만 넣을 수 있습니다 ('%s' 은(는) %s)", a->str, ty_name(i->ty));
        c->jinfo = i; c->jname = nm;
    }
    if (IS(M_DIS, "setParent")) {
        if (!c->relChild) E(c, "setParent 는 'dis.setParent() (\"자식\") as (\"부모\");' 형태로 씁니다");
        Node *xs[2] = {c->relChild, c->relParent};
        for (int q = 0; q < 2; q++) { int t = ex(xs[q], sc, ns); if (!fit(xs[q], t, TY_STRING)) E(xs[q], "setParent 의 ( ) 안에는 오브젝트 id (문자열)를 씁니다 (받은 형식: %s)", tyTxt(t, xs[q])); }
    }
    if (T_ == M_COL && (!strcmp(c->name, "setCollider") || !strcmp(c->name, "setColider")) && c->args.v[3]->k == N_NUM && (c->args.v[3]->num < 0 || c->args.v[3]->num > 1))
        E(c->args.v[3], "탄성계수는 0 ~ 1 사이입니다 (받은 값: %s)", c->args.v[3]->raw);
    int isMove = IS(M_DIS, "moveObj");
    if (IS(M_CAN, "bindInput")) {
        if (!c->toExpr) E(c, "bindInput 은 %s.bindInput(\"버튼 id\") to (\"키\"); 로 씁니다", c->lib);
        if (c->forExpr) E(c->forExpr, "bindInput 뒤에는 for 를 쓸 수 없습니다");
        int kt = ex(c->toExpr, sc, ns);
        if (!fit(c->toExpr, kt, TY_STRING)) E(c->toExpr, "bindInput 의 to 에는 키 이름(문자열)을 넣습니다 (예: to (\"space\")) — 받은 형식: %s", tyTxt(kt, c->toExpr));
    } else if (c->toExpr && !isMove) E(c, "'to' 는 dis.moveObj(...) · can.bindInput(...) 뒤에서만 쓸 수 있습니다");
    if (c->forExpr && !isMove) {
        int tw = T_ == M_DIS && (!strcmp(c->name,"setObjPos")||!strcmp(c->name,"setObjScale")||!strcmp(c->name,"setObjAlpha")||!strcmp(c->name,"setObjColor")||!strcmp(c->name,"setObjOverlay"));
        if (!tw && !IS(M_CAM, "moveCamto")) E(c->forExpr, "'for' 는 moveObj · setObjPos · setObjScale · setObjAlpha · setObjColor · setObjOverlay 뒤에서만 쓸 수 있습니다 (예: dis.setObjAlpha(\"id\", 0) for 500;)");
        int ft = ex(c->forExpr, sc, ns);
        if (ft == TY_ANY) c->forExpr->chk = TY_INT;
        else if (ft != TY_INT && ft != TY_BIT) E(c->forExpr, "for 다음은 걸리는 시간 ms(int)입니다 (받은 형식: %s)", tyTxt(ft, c->forExpr));
    }
    if (c->easeExpr) {
        if (!(isMove || IS(M_DIS, "setObjPos"))) E(c->easeExpr, "'ease' 는 dis.moveObj · dis.setObjPos 뒤에서만 쓸 수 있습니다 (예: dis.moveObj(\"id\") to [100, 0] for 500 ease (\"sinleft\");)");
        if (!c->forExpr) E(c->easeExpr, "'ease' 는 'for ms' 와 함께 씁니다 (예: dis.moveObj(\"id\") to [100, 0] for 500 ease (\"sin\");)");
        int et = ex(c->easeExpr, sc, ns);
        if (et != TY_STRING && et != TY_ANY) E(c->easeExpr, "ease 다음은 이름(문자열)입니다 — \"linear\" · \"sin\" · \"sinleft\" · \"sinright\" (받은 형식: %s)", tyTxt(et, c->easeExpr));
        if (c->easeExpr->k == N_STR && !jb_ease_ok(c->easeExpr->str)) E(c->easeExpr, "ease 이름은 \"linear\" · \"sin\" · \"sinleft\" · \"sinright\" 중 하나입니다 (받은 값: \"%s\")", c->easeExpr->str);
    }
    if (isMove) {
        if (!c->toExpr) E(c, "moveObj 는 갈 위치를 'to' 로 정해야 합니다 (예: dis.moveObj(\"yaju\") to [100, 200] for 500;)");
        objCtx = 1; int t = ex(c->toExpr, sc, ns); objCtx = 0;
        int d[2] = {1, 2}; needMat(c->toExpr, t, d, "moveObj 의 to (위치 [x, y])");
        if (c->forExpr) { int ft = ex(c->forExpr, sc, ns); if (ft != TY_INT && ft != TY_BIT) E(c->forExpr, "for 다음은 걸리는 시간 ms(int)입니다 (받은 형식: %s)", tyTxt(ft, c->forExpr)); }
    }
    int isImport = IS(M_MAG, "importFile");
    if (c->saveAs && !isImport && !IS(M_MAG, "setasTexture")) E(c, "'save as' 는 mag.importFile(...) · mag.setasTexture(...) 뒤에서만 쓸 수 있습니다");
    if (IS(M_MAG, "setasTexture")) {
        if (!c->saveAs) E(c, "setasTexture 는 텍스쳐 id 를 'save as' 로 정해야 합니다 (예: %s.setasTexture(\"hero\", [0, 0], [32, 32]) save as (\"walk1\");)", c->lib);
        int st = ex(c->saveAs, sc, ns);
        if (st != TY_STRING && st != TY_ANY) E(c->saveAs, "save as 다음은 id 문자열입니다 (받은 형식: %s)", tyTxt(st, c->saveAs));
        if (st == TY_ANY) c->saveAs->chk = TY_STRING;
    }
    if (IS(M_MAG, "setasAnime") && c->args.v[0]->k == N_MAT) for (int q = 0; q < c->args.v[0]->args.n; q++) {
        Node *x = c->args.v[0]->args.v[q];
        if (x->ty == TY_ANY) x->chk = TY_STRING;
        else if (x->ty != TY_STRING) E(x, "애니메이션의 프레임은 텍스쳐 id 문자열입니다 (받은 형식: %s)", tyTxt(x->ty, x));
    }
    if (IS(M_MAG, "importFolder")) {
        Node *a = c->args.v[0];
        if (a->k != N_STR) E(a, "importFolder 경로는 따옴표 안의 문자열 상수여야 합니다 (예: %s.importFolder(\"assets/sounds\");)", c->lib);
        char *raw = jb_strdup(a->str); size_t L = strlen(raw); while (L && raw[L-1] == '/') raw[--L] = 0;
        char *n = (!*raw || !strcmp(raw, ".")) ? jb_strdup("") : path_resolve(raw, M->file);
        if (!n) E(a, "경로가 프로젝트 폴더 밖을 가리킵니다: %s", a->str);
        char *pre = *n ? jb_fmt("%s/", n) : jb_strdup("");
        size_t pl = strlen(pre);
        int cap = 16; c->folderNames = jb_alloc(sizeof(char*) * cap); c->folderPaths = jb_alloc(sizeof(char*) * cap);
        extern int jb_is_img(const char *), jb_is_aud(const char *), jb_is_ttf(const char *), jb_is_tl(const char *);
        for (int i = 0; i < P.nfiles; i++) {   /* P.files 는 경로 순으로 정렬돼 있음 */
            const char *f = P.files[i].path;
            if (strncmp(f, pre, pl)) continue;
            if (!jb_is_img(f) && !jb_is_aud(f) && !jb_is_ttf(f) && !jb_is_tl(f)) continue;
            const char *b = strrchr(f, '/'); b = b ? b + 1 : f;
            const char *dot = strrchr(b, '.'); char *nm = jb_strndup(b, dot ? (size_t)(dot - b) : strlen(b));
            for (int q = 0; q < c->nfolder; q++) if (!strcmp(c->folderNames[q], nm))
                E(a, "importFolder: 이름 '%s' 이(가) 겹칩니다 — %s · %s (파일 이름을 바꾸거나 importFile 로 따로 이름을 붙이세요)", nm, c->folderPaths[q], f);
            if (c->nfolder == cap) { cap *= 2; c->folderNames = jb_realloc(c->folderNames, sizeof(char*) * cap); c->folderPaths = jb_realloc(c->folderPaths, sizeof(char*) * cap); }
            c->folderNames[c->nfolder] = jb_in(nm); c->folderPaths[c->nfolder] = f; c->nfolder++; free(nm);
        }
        if (!c->nfolder) E(a, "'%s' 폴더에 불러올 파일이 없습니다 (이미지 · 소리 · 글꼴 · 타일맵)", *n ? n : "(프로젝트 최상위)");
    }
    if (isImport) {
        if (!c->saveAs) E(c, "importFile 은 저장할 id 를 'save as' 로 정해야 합니다 (예: %s.importFile(\"img/hero.png\") save as (\"hero\");)", c->lib);
        Node *a = c->args.v[0];
        if (a->k != N_STR) E(a, "importFile 경로는 따옴표 안의 문자열 상수여야 합니다 (예: %s.importFile(\"img/hero.png\") save as (\"hero\");)", c->lib);
        char *n = path_resolve(a->str, M->file);
        if (!n) E(a, "경로가 프로젝트 폴더 밖을 가리킵니다: %s", a->str);
        if (!proj_file(n)) E(a, "프로젝트에 '%s' 파일이 없습니다 (이미지는 불러오기 · 끌어다 놓기로 프로젝트에 넣으세요)", n);
        extern int jb_is_img(const char *), jb_is_aud(const char *), jb_is_ttf(const char *), jb_is_tl(const char *);
        if (!jb_is_img(n) && !jb_is_aud(n) && !jb_is_ttf(n) && !jb_is_tl(n)) E(a, "importFile 은 이미지(.png · .jpg · .gif · .webp · .bmp) · 소리(.wav · .mp3 · .ogg · .m4a) · 글꼴(.ttf · .otf) · 타일맵(.tl) 파일만 불러옵니다: %s", n);
        c->imgPath = n;
        int st = ex(c->saveAs, sc, ns);
        if (st != TY_STRING && st != TY_ANY) E(c->saveAs, "save as 다음은 id 문자열입니다 (받은 형식: %s)", tyTxt(st, c->saveAs));
        if (st == TY_ANY) c->saveAs->chk = TY_STRING;
    }
    int isCopy = IS(M_JAW, "copyFileData");
    if (c->asName && !IS(M_DIS, "getVar") && !isCopy) E(c, "'as' 는 dis.getVar(...) · jaw.copyFileData(...) 뒤에서만 쓸 수 있습니다");
    if (isCopy) {   /* jaw.copyFileData("경로" | "permit") as ("data 변수"); */
        if (!c->asName || !c->asQ) E(c, "copyFileData 는 받을 data 변수를 as (\"이름\") 으로 정합니다 (예: jaw.copyFileData(\"permit\") as (\"doc\");)");
        c->asName = jb_in(c->asName); Info *i = look(c->asName, sc, ns);
        if (!i) EL(c->asLine, "변수 '%s' 이(가) 선언되지 않았습니다 (var data call %s; 로 먼저 만드세요)", c->asName, c->asName);
        if (i->ty != TY_DATA) EL(c->asLine, "copyFileData 는 data 변수에만 넣을 수 있습니다 ('%s' 은(는) %s)", c->asName, ty_name(i->ty));
        c->asInfo = i;
    }
    if (IS(M_DIS, "getVar")) {
        if (!c->asName) E(c, "getVar 는 받을 변수를 'as' 로 정해야 합니다 (예: dis.getVar(\"yaju\", \"scale[1, 2]\") as s;)");
        Info *info = look(c->asName, sc, ns);
        if (!info) EL(c->asLine, "변수 '%s' 이(가) 선언되지 않았습니다 (getVar 로 받을 변수를 먼저 만드세요)", c->asName);
        c->asInfo = info;
        Node *nameArg = c->args.v[1];
        if (nameArg->k == N_STR) {
            int key, idx[2];
            if (!jb_parse_objvar(nameArg->str, &key, idx)) E(nameArg, "getVar 이름은 coord(position) · scale · color · text 중 하나입니다 (성분은 \"scale[1, 2]\") — 받은 값: \"%s\"", nameArg->str);
            const char *kn = key == K_POS ? "pos" : key == K_SCALE ? "scale" : key == K_COLOR ? "color" : key == K_TEXT ? "text" : key == K_VEL ? "vel" : key == K_ISTRIG ? "istrig" : "trigid";
            if (idx[0]) {
                if (STR_KEY(key) || key == K_ISTRIG) E(nameArg, "%s 에는 [행, 열] 성분이 없습니다", kn);
                if (idx[0] != 1 || idx[1] < 1 || idx[1] > 2) E(nameArg, "%s 는 (1, 2) 행렬이라 성분은 [1, 1] 또는 [1, 2] 입니다", key == K_POS ? "coord" : "scale");
                if (!assignable(info->ty, TY_FLOAT)) EL(c->asLine, "행렬 성분(float)은 %s 변수 '%s' 에 넣을 수 없습니다 (float 변수가 필요)", ty_name(info->ty), c->asName);
            } else if (key == K_ISTRIG) {
                if (!assignable(info->ty, TY_BIT)) EL(c->asLine, "istrigged(bit)는 %s 변수 '%s' 에 넣을 수 없습니다", ty_name(info->ty), c->asName);
            } else if (STR_KEY(key)) {
                if (info->ty != TY_STRING) EL(c->asLine, "%s(string)는 %s 변수 '%s' 에 넣을 수 없습니다 (string 변수가 필요)", kn, ty_name(info->ty), c->asName);
            } else if (info->ty != TY_MATRIX || !info->hasDims || info->dims[0] != 1 || info->dims[1] != 2)
                EL(c->asLine, "%s 는 matrix[1, 2] 라서 %s 변수 '%s' 에 넣을 수 없습니다", key == K_POS ? "coord" : "scale", ty_name(info->ty), c->asName);
        }
    }
    if (IS(M_JAW, "sendto") || IS(M_JAW, "setup")) {
        if (M->isLib) E(c, "%s 는 코드 파일(.jb) 의 run{ } 안에서만 쓸 수 있습니다", c->name);
        if (!c->taskPath) E(c, "%s 경로는 따옴표 안의 문자열 상수여야 합니다 (예: jaw.%s(\"tools/a.jb\");)", c->name, c->name);
    }
    if (IS(M_JAW, "goTo") && c->args.v[0] && c->args.v[0]->k == N_NUM) {
        double ln = c->args.v[0]->num;
        if (ln != floor(ln) || ln < 1) E(c->args.v[0], "goTo 의 줄 번호는 1 이상의 정수입니다 (받은 값: %s)", c->args.v[0]->raw);
        if (!M->stmtBuilt) { M->stmtBuilt = 1; for (int i = 0; i < M->ast.n; i++) { Node *x = M->ast.v[i]; M->stmtLines = jb_realloc(M->stmtLines, sizeof(int) * (M->nstmt + 1)); M->stmtLines[M->nstmt++] = x->line; allLines(&x->body); } }
        int has = 0; for (int q = 0; q < M->nstmt; q++) if (M->stmtLines[q] == (int)ln) has = 1;
        if (!has) E(c->args.v[0], "goTo(%d): %d번째 줄에 문장이 없습니다 (그 줄에서 시작하는 문장으로만 갈 수 있음)", (int)ln, (int)ln);
        LS st = {{0}, 0}, out = {{0}, 0}; int found = 0;
        for (int i = 0; i < M->ast.n && !found; i++) { Node *x = M->ast.v[i]; if (x->k == N_RUN || x->k == N_FUNC) found = findReach(&x->body, &st, c, &out); }
        if (found) {
            int ok = 0;
            for (int q = 0; q < out.n && !ok; q++) for (int k = 0; k < out.v[q]->n; k++) if (out.v[q]->v[k]->line == (int)ln) { ok = 1; break; }
            if (!ok) {
                if (c->gotoLabel) E(c->args.v[0], "goTo(\"%s\" → %d): %d번째 줄은 지금 있는 블록이나 그 바깥 블록의 문장이 아닙니다 (안쪽 블록 · 다른 함수 · 다른 run 으로는 못 감)", c->gotoLabel, (int)ln, (int)ln);
                E(c->args.v[0], "goTo(%d): %d번째 줄은 지금 있는 블록이나 그 바깥 블록의 문장이 아닙니다 (안쪽 블록 · 다른 함수 · 다른 run 으로는 못 감)", (int)ln, (int)ln);
            }
        }
    }
    if (IS(M_JAW, "break") || IS(M_JAW, "return")) {
        Node *tg = NULL;
        for (int i = ncs - 1; i >= 0; i--) if (cs[i].kind == 1) { tg = cs[i].node; break; }
        if (!tg && ncs) tg = cs[ncs - 1].node;
        if (!tg) E(c, "%s() 는 반복문(loop)이나 조건문(if) 안에서만 쓸 수 있습니다", c->name);
        c->jump = tg; c->jumpIsBreak = !strcmp(c->name, "break"); if (tg->k == N_IF) tg->jumpTarget = 1;
    }
    if (IS(M_DIS, "setObjAlpha") && c->args.v[1]->k == N_NUM && (c->args.v[1]->num < 0 || c->args.v[1]->num > 255))
        E(c->args.v[1], "투명도는 0 (안 보임) ~ 255 (불투명) 입니다 (받은 값: %s)", c->args.v[1]->raw);
    if (IS(M_DIS, "setObjCyl") && c->args.v[2]->k == N_NUM && c->args.v[2]->num < 0)
        E(c->args.v[2], "반지름은 0 이상이어야 합니다 (받은 값: %s)", c->args.v[2]->raw);
    if (IS(M_DIS, "tagObj") && c->args.v[1]->k == N_STR && (!*c->args.v[1]->str || strchr(c->args.v[1]->str, '<') || strchr(c->args.v[1]->str, '>')))
        E(c->args.v[1], "태그 이름은 비어 있거나 < > 가 들어갈 수 없습니다 (붙일 때는 \"enemy\", 부를 때는 \"<enemy>\") — 받은 값: \"%s\"", c->args.v[1]->str);
    if ((IS(M_DIS, "setTextNudge") || IS(M_DIS, "setObjNudge")) && c->args.v[1]->k == N_STR) { int a, b; if (!jb_parse_nudge(c->args.v[1]->str, &a, &b))
        E(c->args.v[1], "기준점은 \"center\" · \"left\" · \"right\" · \"top\" · \"bottom\" 또는 \"top left\" 처럼 둘을 합친 것입니다 (받은 값: \"%s\")", c->args.v[1]->str); }
    if (IS(M_DIS, "setFont") && c->args.v[0]->k == N_STR) {
        Node *a = c->args.v[0]; char *n = path_resolve(a->str, M->file); int mag = mod_uses_lib(M, MAG_ID);
        extern int jb_is_ttf(const char *);
        if (n && proj_file(n)) { if (!jb_is_ttf(n)) E(a, "setFont 은 .ttf · .otf 글꼴 파일만 씁니다: %s", n); c->fontPath = n; }
        else if (!mag) {
            if (jb_is_ttf(a->str)) E(a, "프로젝트에 '%s' 파일이 없습니다 (글꼴은 불러오기 · 끌어다 놓기로 프로젝트에 넣으세요)", n && *n ? n : a->str);
            E(a, "setFont 에는 .ttf 경로를 씁니다 (Magellan 을 쓰면 mag.importFile 로 붙인 이름도) — 받은 값: \"%s\"", a->str);
        }
    }
    if (IS(M_DIS, "setTilemap")) {
        if (!mod_uses_lib(M, COL_ID)) E(c, "setTilemap 은 콜라이더도 깔아서 이 파일에 'Use colliders call 별칭;' 이 필요합니다");
        Node *a = c->args.v[1]; extern int jb_is_tl(const char *);
        if (a->k == N_STR && jb_is_tl(a->str)) {
            char *n = path_resolve(a->str, M->file);
            if (!n || !proj_file(n)) E(a, "프로젝트에 '%s' 파일이 없습니다 (타일맵 에디터에서 저장한 .tl 을 프로젝트에 넣으세요)", n && *n ? n : a->str);
            c->tlPath = n;
        }
    }
    if (IS(M_DIS, "setDisplayColor") && c->args.v[0]->k == N_STR && !HEX_ok(c->args.v[0]->str))
        E(c->args.v[0], "색은 헥스코드여야 합니다 (예: \"#1e90ff\", \"#fff\") — 받은 값: \"%s\"", c->args.v[0]->str);
#undef IS
}

static void body(NodeList *l, InfoMap **sc, int ns, CS *cs, int ncs);
static void stmt(Node *s, InfoMap **sc, int ns, CS *cs, int ncs, Node *chainHead) {
    switch (s->k) {
    case N_USE: case N_LIB: return;
    case N_VAR: {
        InfoMap *cur = sc[ns - 1];
        if (im_get(cur, s->name)) E(s, "'%s' 은(는) 이미 같은 범위에 선언되어 있습니다", s->name);
        if (s->pub) { s->info = publicOf(s->name); im_set(cur, s->info); return; }
        if (s->expr) {
            if (isL(s->ty) && s->expr->k == N_MAT) { s->expr->asList = 1; s->expr->comp = s->ty == TY_COMPOUND; }
            if (s->ty == TY_LIST && s->expr->k == N_CALL) s->expr->splitOK = 1;
            int t = ex(s->expr, sc, ns);
            if (s->ty == TY_DATA || t == TY_DATA) {}   /* data ↔ 무엇이든 (실행할 때 바꿈 · 안 맞으면 건너뜀) */
            else if (s->ty == TY_MATRIX) { char w[128]; snprintf(w, sizeof w, "행렬 변수 '%s'", s->name); needMat(s->expr, t, s->dims, w); }
            else if (!fit(s->expr, t, s->ty)) E(s, "%s 값을 %s 변수 '%s' 에 넣을 수 없습니다", tyTxt(t, s->expr), ty_name(s->ty), s->name);
        }
        Info *i = info_new(s->name, s->ty, s->hasDims ? s->dims : NULL);
        i->kind = ns == 1 ? (M->isLib ? VK_GLOBAL : VK_TVAR) : (!M->isLib && runK >= 0 && ns == 2) ? VK_RVAR : VK_LOCAL;
        i->mod = M->name; i->runK = runK; i->file = M->file; i->line = s->line;
        s->info = i; im_set(cur, i); return;
    }
    case N_DSET: {   /* data.이름.setPart(시작, 끝, 값); */
        curLine = s->line; dataVar(s, sc, ns);
        if (s->args.n != 3) E(s, "'setPart' 은(는) 값 3개가 필요합니다 (받은 값: %d개)", s->args.n);
        for (int i = 0; i < 2; i++) idxInt(s->args.v[i], ex(s->args.v[i], sc, ns), "setPart", i);
        ex(s->args.v[2], sc, ns); return;
    }
    case N_ASSIGN: {
        curLine = s->line;
        Info *i = look(s->name, sc, ns);
        if (!i) E(s, "변수 '%s' 이(가) 선언되지 않았습니다 (var 로 먼저 만드세요)", s->name);
        if (i->param && i->ty == TY_STRING) E(s, "string 매개변수 '%s' 의 값은 바꿀 수 없습니다", s->name);
        if (isL(i->ty) && s->expr->k == N_MAT) { s->expr->asList = 1; s->expr->comp = i->ty == TY_COMPOUND; }
        if (i->ty == TY_LIST && s->expr->k == N_CALL) s->expr->splitOK = 1;
        int t = ex(s->expr, sc, ns);
        if (i->ty == TY_DATA || t == TY_DATA) {}   /* data ↔ 무엇이든 */
        else if (i->ty == TY_MATRIX) { char w[128]; snprintf(w, sizeof w, "행렬 변수 '%s'", s->name); needMat(s->expr, t, i->dims, w); }
        else if (!fit(s->expr, t, i->ty)) E(s, "%s 값을 %s 변수 '%s' 에 넣을 수 없습니다", tyTxt(t, s->expr), ty_name(i->ty), s->name);
        s->info = i; s->ty = i->ty; return;
    }
    case N_EASSIGN: {
        int bt = ex(s->a, sc, ns);
        if (bt == TY_ANY && s->c) {
            idxCheck(s, sc, ns, NULL); int t = ex(s->expr, sc, ns);
            if (!isNumA(s->expr, t, 0)) E(s->expr, "행렬 성분에는 숫자(float)만 넣을 수 있습니다 (받은 형식: %s)", tyTxt(t, s->expr));
            s->anyMat = 1; return;
        }
        if (isL(bt) || bt == TY_ANY) {
            if (s->c) E(s, "리스트 항목은 [번호] 하나로 씁니다 (예: list[2] = 값;)");
            int it = ex(s->b, sc, ns);
            if (it == TY_ANY) s->b->chk = TY_INT;
            else if (it != TY_INT && it != TY_BIT && !(it == TY_FLOAT && s->b->anyInt)) E(s->b, "리스트 번호는 int 여야 합니다 (받은 형식: %s)", tyTxt(it, s->b));
            if (bt == TY_COMPOUND || bt == TY_ANY) compMark(s->expr, sc, ns);
            int vt = ex(s->expr, sc, ns);
            if (bt == TY_ANY) s->anyBase = 1; else itemOk(s->expr, vt, bt == TY_COMPOUND);
            s->isList = 1; return;
        }
        if (bt == TY_MATRIX && !s->c) E(s, "행렬 성분은 [행, 열] 두 번호로 씁니다 (예: m[1, 2] = 값;)");
        if (bt != TY_MATRIX) E(s, "[행, 열] 은 행렬에만 쓸 수 있습니다 ('%s' 은(는) %s)", s->a->name ? s->a->name : s->a->prop ? s->a->prop : "?", ty_name(bt));
        idxCheck(s, sc, ns, s->a->hasDims ? s->a->dims : NULL);
        int t = ex(s->expr, sc, ns);
        if (!isNumA(s->expr, t, 0)) E(s->expr, "행렬 성분에는 숫자(float)만 넣을 수 있습니다 (받은 형식: %s)", tyTxt(t, s->expr));
        return;
    }
    case N_OASSIGN: {
        Node *tg = s->a;
        if (tg->k == N_OBJVAR) {
            int i = ex(tg, sc, ns);
            if (isL(i) && s->expr->k == N_MAT) { s->expr->asList = 1; s->expr->comp = i == TY_COMPOUND; }
            if (i == TY_LIST && s->expr->k == N_CALL) s->expr->splitOK = 1;
            int t = ex(s->expr, sc, ns);
            char w[160]; snprintf(w, sizeof w, "obj.%s.%s", tg->id, tg->name);
            if (i == TY_MATRIX) needMat(s->expr, t, tg->dims, w);
            else if (!fit(s->expr, t, i)) E(s, "%s 값을 %s 변수 obj.%s.%s 에 넣을 수 없습니다", tyTxt(t, s->expr), ty_name(i), tg->id, tg->name);
            return;
        }
        ex(tg, sc, ns);
        int t = ex(s->expr, sc, ns);
        char on[160]; if (tg->id) snprintf(on, sizeof on, "obj.%s.%s", tg->id, tg->prop); else snprintf(on, sizeof on, "obj.%s", tg->prop);
        if (tg->key == K_TRIGID || tg->key == K_ISCOL || tg->key == K_COLID) E(s, "%s 은(는) 읽기 전용입니다", on);
        if (tg->key == K_ISTRIG) { if (!fit(s->expr, t, TY_BIT)) E(s->expr, "%s 에는 bit(0 · 1)를 넣습니다 (받은 형식: %s)", on, tyTxt(t, s->expr)); }
        else if (tg->key == K_COLOR) colorCheck(s->expr, t, on);
        else if (tg->key == K_TEXT) { if (!fit(s->expr, t, TY_STRING)) E(s->expr, "%s 에는 문자열이 필요합니다 (숫자는 \"\" + 값 으로)", on); }
        else { int d[2] = {1, 2}; needMat(s->expr, t, d, on); }
        return;
    }
    case N_IF: {
        Node *head = chainHead ? chainHead : s;
        int t = ex(s->expr, sc, ns);
        if (t == TY_ANY) s->expr->chk = TY_FLOAT;
        if (t == TY_STRING || t == TY_MATRIX || isL(t)) E(s->expr, "조건에는 %s을 쓸 수 없습니다", t == TY_STRING ? "문자열" : isL(t) ? "리스트" : "행렬");
        CS *ncs2 = jb_alloc(sizeof(CS) * (ncs + 1)); memcpy(ncs2, cs, sizeof(CS) * ncs); ncs2[ncs].kind = 0; ncs2[ncs].node = head;
        InfoMap **sc2 = jb_alloc(sizeof(InfoMap*) * (ns + 1)); memcpy(sc2, sc, sizeof(InfoMap*) * ns);
        sc2[ns] = im_new(); body(&s->then, sc2, ns + 1, ncs2, ncs + 1);
        if (s->hasEls) {
            if (s->elseIf) stmt(s->els.v[0], sc, ns, cs, ncs, head);
            else { sc2[ns] = im_new(); body(&s->els, sc2, ns + 1, ncs2, ncs + 1); }
        }
        free(ncs2); free(sc2);
        return;
    }
    case N_LOOP: {
        if (s->ms) {
            int t = ex(s->ms, sc, ns);
            if (t == TY_ANY) s->ms->chk = TY_INT;
            else if (!isNum(t)) E(s->ms, "loop for 의 시간(ms)은 숫자여야 합니다 (받은 형식: %s)", tyTxt(t, s->ms));
        }
        if (s->expr) {
            int t = ex(s->expr, sc, ns);
            if (t == TY_ANY) s->expr->chk = TY_FLOAT;
            if (t == TY_STRING || t == TY_MATRIX || isL(t)) E(s->expr, "조건에는 %s을 쓸 수 없습니다", t == TY_STRING ? "문자열" : isL(t) ? "리스트" : "행렬");
        }
        CS *ncs2 = jb_alloc(sizeof(CS) * (ncs + 1)); memcpy(ncs2, cs, sizeof(CS) * ncs); ncs2[ncs].kind = 1; ncs2[ncs].node = s;
        InfoMap **sc2 = jb_alloc(sizeof(InfoMap*) * (ns + 1)); memcpy(sc2, sc, sizeof(InfoMap*) * ns); sc2[ns] = im_new();
        body(&s->body, sc2, ns + 1, ncs2, ncs + 1);
        free(ncs2); free(sc2); return;
    }
    case N_RUN: {
        runK = runN++;
        InfoMap **sc2 = jb_alloc(sizeof(InfoMap*) * (ns + 1)); memcpy(sc2, sc, sizeof(InfoMap*) * ns); sc2[ns] = im_new();
        body(&s->body, sc2, ns + 1, NULL, 0);
        free(sc2); runK = -1; return;
    }
    case N_CALL: call(s, sc, ns, cs, ncs); return;
    }
}
static void body(NodeList *l, InfoMap **sc, int ns, CS *cs, int ncs) { for (int i = 0; i < l->n; i++) stmt(l->v[i], sc, ns, cs, ncs, NULL); }

void jb_check(Module *mod) {
    M = mod; runK = -1; runN = 0; curFn = NULL; objCtx = 0;
    mod->globals = im_new();
    int runs = 0;
    for (int i = 0; i < mod->ast.n; i++) {
        Node *s = mod->ast.v[i];
        if (s->k == N_FUNC) {
            if (mod_func(mod, s->name)) E(s, "함수 '%s' 이(가) 중복 정의되었습니다", s->name);
            mod->funcs = jb_realloc(mod->funcs, sizeof(Node*) * (mod->nfuncs + 1)); mod->funcs[mod->nfuncs++] = s;
        } else if (s->k == N_RUN) runs++;
    }
    if (!mod->isLib && runs == 0 && !mod->bound && !mod->sceneBound) jb_fail(mod->file, 1, "run{ } 블록이 없습니다 — 실행할 코드는 run{ ... } 안에 써야 합니다");
    InfoMap *gs = mod->globals;
    for (int i = 0; i < mod->ast.n; i++) {
        Node *s = mod->ast.v[i];
        if (s->k == N_FUNC) {
            InfoMap *fs = im_new();
            for (int q = 0; q < s->nparams; q++) {
                Param *p = &s->params[q];
                if (im_get(fs, p->name)) E(s, "매개변수 '%s' 이(가) 중복되었습니다", p->name);
                Info *in = info_new(p->name, p->ty, p->ty == TY_MATRIX ? p->dims : NULL); in->kind = VK_LOCAL; in->mod = mod->name; in->param = 1;
                p->info = in; im_set(fs, in);
            }
            curFn = s;
            InfoMap *sc[2] = {gs, fs};
            body(&s->body, sc, 2, NULL, 0);
            curFn = NULL;
        } else { InfoMap *sc[1] = {gs}; stmt(s, sc, 1, NULL, 0, NULL); }
    }
}

/* ---------- 프로그램 만들기 ---------- */
static Module *loading[256]; static int nloading;
static NodeList parseFile(const char *path, int isLib) {
    SrcFile *f = proj_file(path);
    int nt; Tok *t = jb_lex(f->text, f->path, &nt);
    return jb_parse(t, nt, f->path, isLib);
}
static const char *scanLibId(SrcFile *f) {
    /* 맨 앞 세 토큰이 Lib id 이름 이면 그 이름 · 아니면 파일 이름 (렉서 오류는 무시) */
    const char *s = f->text; int line = 1;
    (void)line;
    /* 간단한 앞부분 렉스: 공백 · 주석 건너뛰기 */
    const char *w[3]; size_t wl[3]; int nw = 0;
    while (*s && nw < 3) {
        if (*s == '\n' || *s == ' ' || *s == '\t' || *s == '\r') { s++; continue; }
        if ((unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) { s += 3; continue; }
        if (s[0] == '/' && s[1] == '/') { while (*s && *s != '\n') s++; continue; }
        if (s[0] == '/' && s[1] == '*') { const char *e = strstr(s + 2, "*/"); if (!e) break; s = e + 2; continue; }
        const unsigned char c = (unsigned char)*s;
        if (c == '_' || isalpha(c) || c >= 0x80) { const char *b = s; while (*s && ((unsigned char)*s == '_' || isalnum((unsigned char)*s) || (unsigned char)*s >= 0x80)) s++; w[nw] = b; wl[nw] = s - b; nw++; continue; }
        break;
    }
    if (nw == 3 && wl[0] == 3 && !strncmp(w[0], "Lib", 3) && wl[1] == 2 && !strncmp(w[1], "id", 2)) {
        /* 세 번째 낱말이 키워드 · 형식이면 id 가 아님 */
        return jb_intern(w[2], wl[2]);
    }
    const char *b = strrchr(f->path, '/'); b = b ? b + 1 : f->path;
    size_t L = strlen(b); if (L > 4 && !strcmp(b + L - 4, ".jbl")) L -= 4;
    return jb_intern(b, L);
}
static void bindUses(Module *mod);
static Module *loadLib(const char *id, const char *fromFile, int fromLine) {
    Module *have = lib_by_id(id); if (have) return have;
    const char *paths[16]; int np = 0;
    for (int i = 0; i < P.nfiles; i++) {
        const char *f = P.files[i].path; size_t L = strlen(f);
        if (strncmp(f, "lib/", 4) || L < 9 || strcmp(f + L - 4, ".jbl")) continue;
        if (!strcmp(scanLibId(&P.files[i]), id) && np < 16) paths[np++] = f;
    }
    if (!np) jb_fail(fromFile, fromLine, "라이브러리 id '%s' 을(를) 찾을 수 없습니다 (lib/ 폴더의 .jbl 파일 맨 위에 'Lib id %s;' 가 있어야 합니다)", id, id);
    if (np > 1) { char b[1024] = ""; for (int q = 0; q < np; q++) { if (q) strcat(b, ", "); strncat(b, paths[q], 200); } jb_fail(fromFile, fromLine, "라이브러리 id '%s' 이(가) 여러 파일에 중복되었습니다: %s", id, b); }
    for (int q = 0; q < nloading; q++) if (!strcmp(loading[q]->name, id)) {
        char b[2048] = ""; for (int k = 0; k < nloading; k++) { strncat(b, loading[k]->name, 100); strcat(b, " → "); } strncat(b, id, 100);
        jb_fail(fromFile, fromLine, "라이브러리가 서로를 순환 참조합니다: %s", b);
    }
    Module *mod = jb_calloc(sizeof(Module)); mod->name = jb_in(id); mod->file = paths[0]; mod->isLib = 1;
    loading[nloading++] = mod;
    mod->ast = parseFile(paths[0], 1);
    Node *h = mod->ast.n && mod->ast.v[0]->k == N_LIB ? mod->ast.v[0] : NULL;
    if (h && h->parent && strcmp(h->parent, "jawbreaker")) { mod->parentId = h->parent; loadLib(h->parent, paths[0], h->line); }
    bindUses(mod);
    nloading--;
    P.libs = jb_realloc(P.libs, sizeof(Module*) * (P.nlibs + 1)); P.libs[P.nlibs++] = mod;
    return mod;
}
static void bindUses(Module *mod) {
    for (int i = 0; i < mod->ast.n; i++) {
        Node *s = mod->ast.v[i]; if (s->k != N_USE) continue;
        if (mod_use(mod, s->alias)) jb_fail(mod->file, s->line, "별칭 '%s' 이(가) 중복되었습니다", s->alias);
        if (!is_builtin_lib(s->libId) && !ext_find(s->libId)) loadLib(s->libId, mod->file, s->line);
        if (mod->nuses >= 64) jb_fail(mod->file, s->line, "Use 가 너무 많습니다");
        mod->uses[mod->nuses].alias = s->alias; mod->uses[mod->nuses].lib = s->libId; mod->nuses++;
    }
}
static void walkCalls(NodeList *l, void (*fn)(Node *, void *), void *ud) {
    for (int i = 0; i < l->n; i++) {
        Node *s = l->v[i];
        if (s->k == N_CALL) fn(s, ud);
        else if (s->k == N_RUN || s->k == N_LOOP) walkCalls(&s->body, fn, ud);
        else if (s->k == N_IF) { walkCalls(&s->then, fn, ud); walkCalls(&s->els, fn, ud); }
    }
}
typedef struct { Module *m; Node **v; int n; } Found;
static void findTask(Node *c, void *ud) {
    Found *f = ud;
    if (strcmp(c->name, "sendto") && strcmp(c->name, "setup")) return;
    if (c->lib && (!mod_use(f->m, c->lib) || strcmp(mod_use(f->m, c->lib), "jawbreaker"))) return;
    if (c->args.n != 1) return;
    if (c->args.v[0]->k != N_STR) jb_fail(f->m->file, c->line, "%s 경로는 따옴표 안의 문자열 상수여야 합니다 (예: jaw.%s(\"tools/a.jb\");)", c->name, c->name);
    f->v = jb_realloc(f->v, sizeof(Node*) * (f->n + 1)); f->v[f->n++] = c;
}
static Module *loadTask(const char *path) {
    Module *have = task_by_path(path); if (have) return have;
    Module *mod = jb_calloc(sizeof(Module)); mod->name = jb_in(jb_fmt("@%s", path)); mod->file = proj_file(path)->path; mod->isLib = 0;
    P.tasks = jb_realloc(P.tasks, sizeof(Module*) * (P.ntasks + 1)); P.tasks[P.ntasks++] = mod;
    mod->ast = parseFile(path, 0);
    bindUses(mod);
    for (int i = 0; i < mod->ast.n; i++) {
        Node *r = mod->ast.v[i]; if (r->k != N_RUN) continue;
        for (int q = 0; q < r->body.n; q++) {
            Node *c = r->body.v[q];
            if (c->k != N_CALL || !c->lib || (strcmp(c->name, "BelongObj") && strcmp(c->name, "BelongScene"))) continue;
            int scene = !strcmp(c->name, "BelongScene");
            const char *al = mod_use(mod, c->lib);
            if (!al) jb_fail(path, c->line, "'%s' 은(는) 라이브러리 별칭이 아닙니다 (파일 위쪽에 'Use %s call %s;' 이 필요합니다)", c->lib, scene ? "Canvas" : "DisPlayer", c->lib);
            if (strcmp(al, scene ? CAN_ID : DIS_ID)) continue;
            char ex_[128]; snprintf(ex_, sizeof ex_, "%s.%s(\"%s\");", c->lib, c->name, scene ? "title" : "player");
            if (c->args.n != 1 || c->args.v[0]->k != N_STR) jb_fail(path, c->line, "%s 에는 %s을(를) 따옴표 안의 문자열 하나로 씁니다 (예: %s)", c->name, scene ? "장면 이름" : "오브젝트 id", ex_);
            const char *id = c->args.v[0]->str;
            if (!*id) jb_fail(path, c->line, "%s 의 %s 이(가) 비어 있습니다 — 귀속시킬 %s을(를) 써야 합니다 (예: %s)", c->name, scene ? "장면 이름" : "id", scene ? "장면" : "오브젝트", ex_);
            if (scene && !strcmp(id, "*")) jb_fail(path, c->line, "'*' 은(는) 장면 이름으로 쓸 수 없습니다");
            if (!scene && (strchr(id, '<') || strchr(id, '>'))) jb_fail(path, c->line, "BelongObj 에는 태그(\"<태그>\")가 아니라 오브젝트 id 를 씁니다");
            if (!strcmp(path, P.main) && scene) jb_fail(path, c->line, "처음 실행하는 파일(%s)은 장면에 귀속시킬 수 없습니다 — 장면 코드는 다른 .jb 파일에 두세요", path);
            c->belongOk = 1;
            if (mod->nbelong < 16) { mod->belong[mod->nbelong].scene = scene; mod->belong[mod->nbelong].id = jb_in(id); mod->nbelong++; }
        }
    }
    Found f = {mod, NULL, 0};
    walkCalls(&mod->ast, findTask, &f);
    for (int i = 0; i < f.n; i++) {
        Node *c = f.v[i]; const char *raw = c->args.v[0]->str; char *n = path_resolve(raw, path);
        if (!n) jb_fail(path, c->line, "경로가 프로젝트 폴더 밖을 가리킵니다: %s", raw);
        if (!proj_file(n)) jb_fail(path, c->line, "프로젝트에 '%s' 파일이 없습니다", n);
        size_t L = strlen(n); if (L < 3 || strcmp(n + L - 3, ".jb")) jb_fail(path, c->line, "%s 는 .jb 코드 파일만 실행할 수 있습니다: %s", c->name, n);
        c->taskPath = proj_file(n)->path;
        loadTask(n);
    }
    free(f.v);
    return mod;
}
static char *stripComments(const char *s) {
    size_t L = strlen(s); char *o = jb_alloc(L + 1); size_t n = 0;
    for (size_t i = 0; i < L;) {
        if (s[i] == '/' && s[i+1] == '/') { while (i < L && s[i] != '\n') i++; continue; }
        if (s[i] == '/' && s[i+1] == '*') { const char *e = strstr(s + i + 2, "*/"); i = e ? (size_t)(e - s) + 2 : L; continue; }
        o[n++] = s[i++];
    }
    o[n] = 0; return o;
}
static int hasBelongDecl(const char *src) {   /* /\.\s*Belong(?:Obj|Scene)\s*\(/ */
    char *s = stripComments(src); int r = 0;
    for (char *p = s; *p && !r; p++) {
        if (*p != '.') continue;
        char *q = p + 1; while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
        if (!strncmp(q, "Belong", 6)) {
            q += 6;
            if (!strncmp(q, "Obj", 3)) q += 3; else if (!strncmp(q, "Scene", 5)) q += 5; else continue;
            while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
            if (*q == '(') r = 1;
        }
    }
    free(s); return r;
}
static Belong *belongAdd(Belong **arr, int *n, const char *id, const char *file) {
    Belong *b = NULL;
    for (int i = 0; i < *n; i++) if (!strcmp((*arr)[i].id, id)) b = &(*arr)[i];
    if (!b) { *arr = jb_realloc(*arr, sizeof(Belong) * (*n + 1)); b = &(*arr)[(*n)++]; b->id = id; b->files = NULL; b->n = 0; }
    for (int i = 0; i < b->n; i++) if (!strcmp(b->files[i], file)) return b;
    b->files = jb_realloc(b->files, sizeof(char*) * (b->n + 1)); b->files[b->n++] = file;
    return b;
}
static int dimsEq(Info *a, int ty, const int *d) { return a->ty == ty && (ty != TY_MATRIX || (a->dims[0] == d[0] && a->dims[1] == d[1])); }
void jb_build(void) {
    if (!proj_file(P.main)) jb_fail("", 1, "실행할 .jb 파일이 없습니다: %s", P.main);
    loadTask(P.main);
    for (int i = 0; i < P.nfiles; i++) {
        const char *f = P.files[i].path; size_t L = strlen(f);
        if (L < 3 || strcmp(f + L - 3, ".jb") || task_by_path(f)) continue;
        if (hasBelongDecl(P.files[i].text)) loadTask(f);
    }
    /* public 변수 */
    typedef struct { NodeList ast; const char *file; } Decl;
    Decl *ds = NULL; int nd = 0;
    for (int i = 0; i < P.nlibs; i++) { ds = jb_realloc(ds, sizeof(Decl) * (nd + 1)); ds[nd].ast = P.libs[i]->ast; ds[nd].file = P.libs[i]->file; nd++; }
    for (int i = 0; i < P.ntasks; i++) { ds = jb_realloc(ds, sizeof(Decl) * (nd + 1)); ds[nd].ast = P.tasks[i]->ast; ds[nd].file = P.tasks[i]->file; nd++; }
    for (int i = 0; i < P.nfiles; i++) {
        const char *f = P.files[i].path; size_t L = strlen(f);
        int isjb = L >= 3 && !strcmp(f + L - 3, ".jb"), isjbl = L >= 4 && !strcmp(f + L - 4, ".jbl");
        if (!isjb && !isjbl) continue;
        int loaded = 0; for (int q = 0; q < nd; q++) if (!strcmp(ds[q].file, f)) loaded = 1;
        if (loaded) continue;
        if (isjbl && strncmp(f, "lib/", 4)) continue;
        extern int jb_try_parse(const char *path, int isLib, NodeList *out);
        NodeList ast; if (!jb_try_parse(f, isjbl, &ast)) continue;
        ds = jb_realloc(ds, sizeof(Decl) * (nd + 1)); ds[nd].ast = ast; ds[nd].file = f; nd++;
    }
    for (int q = 0; q < nd; q++) for (int i = 0; i < ds[q].ast.n; i++) {
        Node *s = ds[q].ast.v[i]; if (s->k != N_VAR || !s->pub) continue;
        Info *ex_ = publicOf(s->name);
        if (ex_ && !dimsEq(ex_, s->ty, s->dims)) {
            char a[64], b[64];
            if (ex_->ty == TY_MATRIX) snprintf(a, sizeof a, "matrix[%d, %d]", ex_->dims[0], ex_->dims[1]); else snprintf(a, sizeof a, "%s", ty_name(ex_->ty));
            if (s->ty == TY_MATRIX) snprintf(b, sizeof b, "matrix[%d, %d]", s->dims[0], s->dims[1]); else snprintf(b, sizeof b, "%s", ty_name(s->ty));
            jb_fail(ds[q].file, s->line, "public 변수 '%s' 의 형식이 다릅니다 (%s:%d 에서는 %s, 여기서는 %s)", s->name, ex_->file, ex_->line, a, b);
        }
        if (!ex_) {
            Info *in = info_new(s->name, s->ty, s->hasDims ? s->dims : NULL); in->kind = VK_PUBLIC; in->mod = "public"; in->file = ds[q].file; in->line = s->line;
            P.publics = jb_realloc(P.publics, sizeof(Info*) * (P.npublics + 1)); P.publics[P.npublics++] = in;
        }
    }
    free(ds);
    /* 귀속 */
    typedef struct { const char *file; const char *ids[16]; int n; } Bound;
    Bound *bd = jb_calloc(sizeof(Bound) * (P.ntasks + 1));
    for (int i = 0; i < P.ntasks; i++) {
        Module *m = P.tasks[i]; bd[i].file = m->file;
        for (int q = 0; q < m->nbelong; q++) {
            if (m->belong[q].scene) { belongAdd(&P.sceneBelongs, &P.nsceneBelongs, m->belong[q].id, m->file); m->sceneBound = 1; continue; }
            int dup = 0; for (int k = 0; k < bd[i].n; k++) if (!strcmp(bd[i].ids[k], m->belong[q].id)) dup = 1;
            if (!dup && bd[i].n < 16) bd[i].ids[bd[i].n++] = m->belong[q].id;
            if (!strcmp(m->file, P.main)) continue;
            belongAdd(&P.belongs, &P.nbelongs, m->belong[q].id, m->file);
        }
    }
    for (int i = 0; i < P.ntasks; i++) {
        Module *m = P.tasks[i]; m->bound = bd[i].n > 0;
        /* 파일 최상위 var + run{ } 바로 안의 var */
        typedef struct { Node *s; int runK; } OV; OV *ov = NULL; int nov = 0;
        for (int q = 0; q < m->ast.n; q++) { Node *s = m->ast.v[q]; if (s->k == N_VAR && !s->pub) { ov = jb_realloc(ov, sizeof(OV) * (nov + 1)); ov[nov].s = s; ov[nov].runK = -1; nov++; } }
        int rk = 0;
        for (int q = 0; q < m->ast.n; q++) { Node *r = m->ast.v[q]; if (r->k != N_RUN) continue;
            for (int k = 0; k < r->body.n; k++) { Node *s = r->body.v[k]; if (s->k == N_VAR && !s->pub) { ov = jb_realloc(ov, sizeof(OV) * (nov + 1)); ov[nov].s = s; ov[nov].runK = rk; nov++; } }
            rk++; }
        for (int q = 0; q < nov; q++) {
            Node *s = ov[q].s;
            for (int k = 0; k < bd[i].n; k++) {
                const char *id = bd[i].ids[k]; int oi = -1;
                for (int z = 0; z < P.nobjVars; z++) if (!strcmp(P.objVars[z].id, id)) oi = z;
                if (oi < 0) { P.objVars = jb_realloc(P.objVars, sizeof(*P.objVars) * (P.nobjVars + 1)); oi = P.nobjVars++; P.objVars[oi].id = id; P.objVars[oi].v = NULL; P.objVars[oi].n = 0; }
                Info *ex_ = NULL; for (int z = 0; z < P.objVars[oi].n; z++) if (P.objVars[oi].v[z]->name == s->name) ex_ = P.objVars[oi].v[z];
                if (ex_ && !dimsEq(ex_, s->ty, s->dims)) jb_fail(m->file, s->line, "오브젝트 '%s' 에 귀속된 파일들에서 변수 '%s' 의 형식이 다릅니다 (%s 과 여기)", id, s->name, ex_->file);
                if (!ex_) {
                    Info *in = info_new(s->name, s->ty, s->hasDims ? s->dims : NULL); in->file = m->file; in->runK = ov[q].runK; in->kind = VK_LOCAL;
                    P.objVars[oi].v = jb_realloc(P.objVars[oi].v, sizeof(Info*) * (P.objVars[oi].n + 1)); P.objVars[oi].v[P.objVars[oi].n++] = in;
                }
            }
        }
        free(ov);
    }
    for (int z = 0; z < P.nobjVars; z++) for (int q = 0; q < P.objVars[z].n; q++) {
        Info *i = P.objVars[z].v[q], *ex_ = parentVarOf(i->name);
        if (!ex_) {
            Info *in = info_new(i->name, i->ty, i->hasDims ? i->dims : NULL); in->kind = VK_PVAR; in->file = i->file;
            P.parentVars = jb_realloc(P.parentVars, sizeof(Info*) * (P.nparentVars + 1)); P.parentVars[P.nparentVars++] = in;
        } else if (!dimsEq(ex_, i->ty, i->dims)) ex_->clash = i->file;
    }
    free(bd);
    for (int i = 0; i < P.nlibs; i++) jb_check(P.libs[i]);
    for (int i = 0; i < P.ntasks; i++) jb_check(P.tasks[i]);
}
