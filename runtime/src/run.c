/* 실행기 — 편집기의 execProgram (ev · exec · seq · runIf · runLoop · callFn · callValue · runTask) 를 옮김
 *   run{ } 하나 = 스레드 하나. 한 번에 하나만 실행 (차례표) · 기다리는 동안 · 오래 돌면 차례를 넘김
 */
#define _GNU_SOURCE
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(p, m) _mkdir(p)
#endif

RtOpt RT_OPT = {1280, 720, 0, 0, 0, 0, NULL, 0, NULL, 0};
__thread Ctl C;
double RT_T0;
double perf_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000.0 + t.tv_nsec / 1e6 - RT_T0; }
double jb_now(void) { return perf_now(); }
void rt_sys(const char *fmt, ...) {
    if (!RT_OPT.sysLog) return;
    va_list ap; va_start(ap, fmt); fputs("· ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap);
}

/* ---------- 차례 (FIFO) ---------- */
static pthread_mutex_t Gm = PTHREAD_MUTEX_INITIALIZER;
typedef struct GW { pthread_cond_t c; struct GW *next; int go; int init; } GW;
static GW *qh, *qt; static int gOwned; static int gWaiting;
static __thread GW myW;
void gil_lock(void) {
    pthread_mutex_lock(&Gm);
    if (!gOwned && !qh) { gOwned = 1; pthread_mutex_unlock(&Gm); C.sliceT = perf_now(); return; }
    if (!myW.init) { pthread_cond_init(&myW.c, NULL); myW.init = 1; }
    myW.go = 0; myW.next = NULL;
    if (qt) qt->next = &myW; else qh = &myW;
    qt = &myW; gWaiting++;
    while (!myW.go) pthread_cond_wait(&myW.c, &Gm);
    pthread_mutex_unlock(&Gm);
    C.sliceT = perf_now();
}
void gil_unlock(void) {
    pthread_mutex_lock(&Gm);
    if (qh) { GW *w = qh; qh = w->next; if (!qh) qt = NULL; gWaiting--; w->go = 1; pthread_cond_signal(&w->c); }
    else gOwned = 0;
    pthread_mutex_unlock(&Gm);
}
int gil_waiters(void) { return gWaiting; }
void gil_yield(void) { if (!gWaiting) { C.sliceT = perf_now(); return; } gil_unlock(); gil_lock(); }
/* 틱 · 프레임 신호 */
static pthread_mutex_t Tm = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t Tc = PTHREAD_COND_INITIALIZER, Fc = PTHREAD_COND_INITIALIZER;
static volatile long tickNo;
volatile long RT_FRAME;
void rt_tick_signal(void) { pthread_mutex_lock(&Tm); tickNo++; pthread_cond_broadcast(&Tc); pthread_mutex_unlock(&Tm); }
void rt_frame_signal(void) { pthread_mutex_lock(&Tm); RT_FRAME++; pthread_cond_broadcast(&Fc); pthread_mutex_unlock(&Tm); }
static void tswait(pthread_cond_t *cv, volatile long *ctr, double maxMs) {
    gil_unlock();
    pthread_mutex_lock(&Tm);
    long t0 = *ctr;
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    long ns = ts.tv_nsec + (long)(maxMs * 1e6); ts.tv_sec += ns / 1000000000L; ts.tv_nsec = ns % 1000000000L;
    while (*ctr == t0) { if (pthread_cond_timedwait(cv, &Tm, &ts) == ETIMEDOUT) break; }
    pthread_mutex_unlock(&Tm);
    gil_lock();
}
void rt_wait_tick(void) { tswait(&Tc, &tickNo, 20); }
void rt_wait_frame(void) { tswait(&Fc, &RT_FRAME, 100); }
void rt_sleep_ms(double ms) {
    gil_unlock();
    if (ms > 0) { struct timespec t; t.tv_sec = (time_t)(ms / 1000); t.tv_nsec = (long)(fmod(ms, 1000) * 1e6); while (nanosleep(&t, &t) && errno == EINTR); }
    else sched_yield();
    gil_lock();
}

/* ---------- 환경 ---------- */
Env *env_new(Env *p) { Env *e = jb_calloc(sizeof(Env)); e->p = p; e->tk = p ? p->tk : NULL; return e; }
static void env_free(Env *e) { for (int i = 0; i < e->n; i++) { v_free(&e->s[i]->v); free(e->s[i]); } free(e->s); free(e->runEnvs); free(e); }
Slot *env_get(Env *e, const char *n) {
    for (; e; e = e->p) for (int i = e->n - 1; i >= 0; i--) if (e->s[i]->name == n) return e->s[i];
    return NULL;
}
static Slot *env_own(Env *e, const char *n) { for (int i = 0; i < e->n; i++) if (e->s[i]->name == n) return e->s[i]; return NULL; }
Slot *env_def(Env *e, const char *n, int ty, const int *dims, Value v) {
    Slot *s = env_own(e, n);
    if (s) { v_free(&s->v); s->ty = ty; s->v = v; return s; }
    if (e->n == e->cap) { e->cap = e->cap ? e->cap * 2 : 8; e->s = jb_realloc(e->s, sizeof(Slot*) * e->cap); }
    s = jb_calloc(sizeof(Slot)); s->name = n; s->ty = ty; s->v = v; if (dims) { s->dims[0] = dims[0]; s->dims[1] = dims[1]; }
    e->s[e->n++] = s; return s;
}
const char *env_self(Env *e) { for (; e; e = e->p) if (e->hasSelf) return e->self; return NULL; }
void env_add_run(Env *e, Env *r) { if (e->nrun == e->caprun) { e->caprun = e->caprun ? e->caprun * 2 : 4; e->runEnvs = jb_realloc(e->runEnvs, sizeof(Env*) * e->caprun); } e->runEnvs[e->nrun++] = r; }
static Dc dc0;
static Dc *dcOf(Env *e) { for (; e; e = e->p) if (e->dc) return e->dc; return &dc0; }
Env *PUB;
Env **LIB_ENVS; Module **LIB_MODS; int NLIB_ENVS;
Env *lib_env(const char *mod) { for (int i = 0; i < NLIB_ENVS; i++) if (LIB_MODS[i]->name == mod || !strcmp(LIB_MODS[i]->name, mod)) return LIB_ENVS[i]; return NULL; }

Slot *slot_of(Info *info, const char *name, Env *env, Node *n) {
    if (info && info->kind == VK_PUBLIC) return env_get(PUB, name);
    if (info && info->kind == VK_PVAR) return gfx_pslot(name, env, n);
    Slot *s = env_get(env, name);
    if (!s) RT(n, "변수 '%s' 을(를) 찾을 수 없습니다", name);
    return s;
}

/* ---------- 값 ---------- */
static int32_t i32(double x) { if (!isfinite(x)) return 0; double t = fmod(trunc(x), 4294967296.0); if (t < 0) t += 4294967296.0; return (int32_t)(uint32_t)t; }
Value co(Value v, int ty) {
    switch (ty) {
    case TY_INT: return v_num(v.k == V_NUM ? i32(v.u.n) : 0);
    case TY_BIT: return v_num(v.k == V_NUM ? (v.u.n != 0) : 1);
    case TY_MATRIX: case TY_COMPOUND: return v_copy(v);
    case TY_LIST: { Value r = v_copy(v); if (r.k == V_LIST) r.u.l->comp = 0; return r; }
    case TY_STRING: return v_copy(v);
    }
    return v_copy(v);
}
Value dflt(int ty, const int *dims) {
    switch (ty) {
    case TY_STRING: return v_str("");
    case TY_MATRIX: return v_mat(dims ? dims[0] : 1, dims ? dims[1] : 1);
    case TY_LIST: return v_list(0);
    case TY_COMPOUND: return v_list(1);
    }
    return v_num(0);
}
static Value dynChk(Value v, Node *n) {
    int w = n->chk;
    if (w == TY_STRING) { if (v.k != V_STR) { char *k = kind_txt(v); RT(n, "리스트 항목이 문자열이 아닙니다 (값: %s)", k); } return v; }
    if (w == -1) { if (v.k == V_MAT || v.k == V_LIST) { char *k = kind_txt(v); RT(n, "리스트에는 숫자나 문자열만 넣을 수 있습니다 (받은 값: %s) — compound 를 쓰세요", k); } return v; }
    if (w == TY_MATRIX) {
        if (v.k != V_MAT) { char *k = kind_txt(v); RT(n, "항목이 행렬이 아닙니다 (값: %s)", k); }
        if (n->hasChkDims && (v.u.m->r != n->chkDims[0] || v.u.m->c != n->chkDims[1])) RT(n, "matrix[%d, %d] 행렬이 필요합니다 (항목: matrix[%d, %d])", n->chkDims[0], n->chkDims[1], v.u.m->r, v.u.m->c);
        return v;
    }
    if (w == TY_LIST) { if (v.k != V_LIST || v.u.l->comp) { char *k = kind_txt(v); RT(n, "항목이 리스트가 아닙니다 (값: %s)", k); } return v; }
    if (w == TY_COMPOUND) { if (v.k != V_LIST || !v.u.l->comp) { char *k = kind_txt(v); RT(n, "항목이 compound 가 아닙니다 (값: %s)", k); } return v; }
    if (v.k == V_STR) RT(n, "리스트 항목이 숫자가 아닙니다 (값: \"%s\")", v.u.s);
    if (v.k != V_NUM) { char *k = kind_txt(v); RT(n, "항목이 숫자가 아닙니다 (값: %s)", k); }
    if (w == TY_INT) v.u.n = trunc(v.u.n);
    return v;
}
static Value chkd(Value v, Node *n) { return n->chk ? dynChk(v, n) : v; }

/* ---------- 사용자 라이브러리 (JDH) — 실행기와 함께 컴파일된 C 구현 ---------- */
#include "jdh_native.h"
static const char *EXT_CUR = "";   /* 지금 불리고 있는 라이브러리 (jdh_log · jdh_error 가 누구 것인지) */
static Node *EXT_NODE = NULL;
/* 디버그 로그: print 와 같은 곳 (표준 출력) — 내보낸 EXE 는 창 프로그램이라 화면에는 안 보이고,  game.exe > log.txt  처럼 받아야 보임 */
void jdh_log(const char *text) { printf("[%s] %s\n", EXT_CUR, text ? text : ""); fflush(stdout); }
void jdh_error(const char *text) {
    if (EXT_NODE) jb_fail(EXT_NODE->file, EXT_NODE->line, "라이브러리 '%s': %s", EXT_CUR, text ? text : "");
    jb_fail("", 0, "라이브러리 '%s': %s", EXT_CUR, text ? text : "");
}
/* host 함수 쪽 (gfx.c · render.c) 에서: 지금 불리고 있는 라이브러리 · 그 라이브러리 이름으로 오류 */
const char *jdh__cur(void) { return EXT_CUR; }
void jdh__fail(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); char b[600]; vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
    jdh_error(b);
}
/* "files.read": 이 프로젝트의 파일 (ext/ 와 프로젝트 밖은 안 됨) — 텍스트 파일은 에디터처럼 줄바꿈이 \n */
extern const unsigned char *proj_bytes(const char *path, size_t *len);
static const unsigned char *xfile(const char *path, size_t *len) {
    if (!path || !*path || path[0] == '/' || path[0] == '\\' || strstr(path, "..") || strchr(path, ':') || !strncmp(path, "ext/", 4)) return NULL;
    return proj_bytes(path, len);
}
int jdh_file_size(const char *path) { size_t n; const unsigned char *b = xfile(path, &n); return b ? (int)(n > 0x7fffffff ? 0x7fffffff : n) : -1; }
int jdh_file_read(const char *path, void *buf, int n) {
    size_t L; const unsigned char *b = xfile(path, &L); if (!b) return -1;
    if (n < 0) n = 0; size_t k = L < (size_t)n ? L : (size_t)n;
    if (k && buf) memcpy(buf, b, k);
    return (int)k;
}
/* 리스트 · 행렬 인자 — 에디터의 jbxArg 와 같은 검사 · 같은 문구 */
static const char *xtyName(int t) { return t == JDH_T_INT ? "int" : t == JDH_T_FLOAT ? "float" : t == JDH_T_STRING ? "string" : "bit"; }
static int xint(double x) { return x != x ? 0 : x >= 2147483647.0 ? 2147483647 : x <= -2147483648.0 ? (-2147483647 - 1) : (int)x; }   /* 소수점 아래는 버림 */
static void xitem(Node *n, const char *cmd, int ai, int ii, int t, Value it, JdhArg *out, char **sOut) {
    if (t == JDH_T_STRING ? it.k != V_STR : it.k != V_NUM) {
        char *k = kind_txt(it);
        RT(n, "'%s' 의 %d번째 값의 %d번째 항목은 %s 형식이어야 합니다 (받은 값: %s)", cmd, ai + 1, ii + 1, xtyName(t), k);
    }
    if (t == JDH_T_STRING) *sOut = it.u.s;
    else if (t == JDH_T_FLOAT) out->d = it.u.n;
    else if (t == JDH_T_BIT) out->i = it.u.n != 0 && it.u.n == it.u.n;
    else out->i = xint(it.u.n);
}
static int ext_call(Node *c, Env *env) {
    const JdhCmd *cm = c->ext; JdhArg a[JDH_MAXSLOT + 8]; Value keep[8]; int nk = 0; void *heap[8]; int nh = 0; char *tmp[8]; int nt = 0, k = 0;
    a[0].i = 0;
    for (int i = 0; i < cm->np; i++) {
        const JdhPT *pt = &cm->p[i]; Node *an = c->args.v[i];
        Value v = chkd(ev(an, env), an);
        switch (pt->t) {
            case JDH_T_INT: a[k++].i = v.k == V_NUM ? (int)v.u.n : 0; v_free(&v); break;
            case JDH_T_BIT: a[k++].i = v.k == V_NUM && v.u.n != 0; v_free(&v); break;
            case JDH_T_FLOAT: a[k++].d = v.k == V_NUM ? v.u.n : 0; v_free(&v); break;
            case JDH_T_MAT12: a[k].d = v.k == V_MAT ? v.u.m->d[0] : 0; a[k + 1].d = v.k == V_MAT ? v.u.m->d[1] : 0; k += 2; v_free(&v); break;
            case JDH_T_LIST: {
                if (v.k != V_LIST) { char *kt = kind_txt(v); RT(an, "'%s' 의 %d번째 값은 리스트여야 합니다 (받은 값: %s)", c->name, i + 1, kt); }
                List *L = v.u.l;
                if (!pt->var) {
                    if (L->n != pt->n) RT(an, "'%s' 의 %d번째 값은 항목이 %d개인 리스트여야 합니다 (받은 것: %d개)", c->name, i + 1, pt->n, L->n);
                    for (int q = 0; q < pt->n; q++) { char *so = NULL; xitem(an, c->name, i, q, pt->el[q], L->items[q], &a[k], &so); if (so) a[k].s = so; k++; }
                } else {
                    int t = pt->el[0], n = L->n; void *arr;
                    if (t == JDH_T_FLOAT) { double *d = jb_alloc(sizeof(double) * (n ? n : 1)); for (int q = 0; q < n; q++) { JdhArg o; xitem(an, c->name, i, q, t, L->items[q], &o, NULL); d[q] = o.d; } arr = d; }
                    else if (t == JDH_T_STRING) { const char **ss = jb_alloc(sizeof(char *) * (n ? n : 1)); for (int q = 0; q < n; q++) { char *so = NULL; xitem(an, c->name, i, q, t, L->items[q], NULL, &so); ss[q] = so; } arr = (void *)ss; }
                    else { int *d = jb_alloc(sizeof(int) * (n ? n : 1)); for (int q = 0; q < n; q++) { JdhArg o; xitem(an, c->name, i, q, t, L->items[q], &o, NULL); d[q] = o.i; } arr = d; }
                    heap[nh++] = arr; a[k++].p = arr; a[k++].i = n;
                }
                keep[nk++] = v;   /* 글자 항목은 리스트 안의 것을 그대로 넘기므로 명령이 끝날 때까지 둠 */
                break;
            }
            case JDH_T_MAT: {
                if (v.k != V_MAT) { char *kt = kind_txt(v); RT(an, "'%s' 의 %d번째 값은 행렬이어야 합니다 (받은 값: %s)", c->name, i + 1, kt); }
                Mat *m = v.u.m;
                if ((pt->r && m->r != pt->r) || (pt->c && m->c != pt->c)) {
                    char want[64]; snprintf(want, sizeof want, "matrix[%s%.0d, %s%.0d]", pt->r ? "" : "...", pt->r, pt->c ? "" : "...", pt->c);
                    RT(an, "'%s' 의 %d번째 값은 %s 행렬이어야 합니다 (받은 값: matrix[%d, %d])", c->name, i + 1, want, m->r, m->c);
                }
                a[k++].p = m->d; if (!pt->r) a[k++].i = m->r; if (!pt->c) a[k++].i = m->c;
                keep[nk++] = v; break;
            }
            default:
                if (v.k == V_STR) { tmp[nt] = v.u.s; a[k++].s = tmp[nt++]; }
                else { v_free(&v); a[k++].s = ""; }
        }
    }
    EXT_CUR = c->extLib; EXT_NODE = c;
    cm->call(a);
    EXT_NODE = NULL;
    for (int i = 0; i < nt; i++) free(tmp[i]);
    for (int i = 0; i < nh; i++) free(heap[i]);
    for (int i = 0; i < nk; i++) v_free(&keep[i]);
    return S_OK;
}
static Value ext_value(Node *e) {
    const JdhVal *xv = e->ext; JdhArg o; memset(&o, 0, sizeof o);
    EXT_CUR = e->extLib; EXT_NODE = e;
    xv->get(&o);
    EXT_NODE = NULL;
    if (xv->ty == JDH_T_STRING) return v_str(o.s ? o.s : "");
    if (xv->ty == JDH_T_FLOAT) return v_num(o.d);
    return v_num(xv->ty == JDH_T_BIT ? (o.i != 0) : o.i);
}
static void ext_init_all(void) { for (int i = 0; i < JDH_NLIBS; i++) if (JDH_LIBS[i].init) { EXT_CUR = JDH_LIBS[i].id; JDH_LIBS[i].init(); } }
static void ext_frame_all(double now) {
    static double last = -1; double dt = last < 0 ? 0 : now - last; last = now;
    for (int i = 0; i < JDH_NLIBS; i++) if (JDH_LIBS[i].frame) { EXT_CUR = JDH_LIBS[i].id; JDH_LIBS[i].frame(dt); }
}
static void ext_stop_all(void) { for (int i = 0; i < JDH_NLIBS; i++) if (JDH_LIBS[i].stop) { EXT_CUR = JDH_LIBS[i].id; JDH_LIBS[i].stop(); } }

static int lix(double i, List *L, Node *n, int extra) {
    int len = L->n + extra;
    if (!len) RT(n, "리스트가 비어 있습니다");
    if (!(i >= 1 && i <= len)) { char *s = fmt_any(v_num(i)); RT(n, "리스트 번호 %s 이(가) 범위(1~%d)를 벗어났습니다", s, len); }
    return (int)i - 1;
}
static int ix(double v, int n, const char *w, Node *node) {
    if (!(v >= 1 && v <= n)) { char *s = fmt_any(v_num(v)); RT(node, "행렬의 %s 번호 %s 이(가) 범위(1~%d)를 벗어났습니다", w, s, n); }
    return (int)v - 1;
}
double evn(Node *e, Env *env) { Value v = ev(e, env); double r = v.k == V_NUM ? v.u.n : (v_free(&v), NAN); return r; }
char *evs(Node *e, Env *env) {
    Value v = ev(e, env);
    if (v.k == V_STR) return v.u.s;
    char *s = fmt_any(v); v_free(&v); return s;
}
static Value callValue(Node *c, Env *env);
/* 복사하지 않고 읽기 — 변수 · 라이브러리 변수 · obj.id.변수 · 리스트 항목 */
Value *ev_ref(Node *e, Env *env) {
    switch (e->k) {
    case N_ID: return &slot_of(e->info, e->name, env, e)->v;
    case N_OBJVAR: return &gfx_ovslot(e, env)->v;
    case N_MEMBER:
        if (e->lnode || e->target == M_CAN || e->target == M_DIS || e->target == M_EXT) return NULL;
        if (e->info && e->info->kind == VK_PUBLIC) return &env_get(PUB, e->name)->v;
        { Env *le = lib_env(e->info->mod); Slot *s = le ? env_own(le, e->name) : NULL; return s ? &s->v : NULL; }
    case N_IDX:
        if (!e->c) {
            Value *L = ev_ref(e->a, env); if (!L) return NULL;
            if (e->anyBase && L->k != V_LIST) { char *k = kind_txt(*L); RT(e, "[번호] 는 리스트 · compound 에만 쓸 수 있습니다 (값: %s)", k); }
            if (e->chk) return NULL;   /* 확인이 필요하면 복사본으로 */
            double i = evn(e->b, env);
            L = ev_ref(e->a, env);
            return &L->u.l->items[lix(i, L->u.l, e, 0)];
        }
        return NULL;
    }
    return NULL;
}
Value ev(Node *e, Env *env) {
    switch (e->k) {
    case N_NUM: return v_num(e->num);
    case N_STR: return v_str(e->str);
    case N_BOOL: return v_num(e->num ? 1 : 0);
    case N_ID: return v_copy(slot_of(e->info, e->name, env, e)->v);
    case N_MAT: {
        if (e->asList) { Value l = v_list(e->comp); for (int i = 0; i < e->args.n; i++) list_push(l.u.l, v_own(ev(e->args.v[i], env))); return l; }
        Value m = v_mat(e->dims[0], e->dims[1]);
        for (int i = 0; i < e->args.n; i++) { Value x = chkd(ev(e->args.v[i], env), e->args.v[i]); m.u.m->d[i] = x.k == V_NUM ? x.u.n : NAN; v_free(&x); }
        return m;
    }
    case N_IDX: {
        if (!e->c) {
            Value tmp; int own = 0; Value *L = ev_ref(e->a, env);
            if (!L) { tmp = ev(e->a, env); L = &tmp; own = 1; }
            if (e->anyBase && L->k != V_LIST) { char *k = kind_txt(*L); RT(e, "[번호] 는 리스트 · compound 에만 쓸 수 있습니다 (값: %s)", k); }
            double i = evn(e->b, env);
            if (!own) { L = ev_ref(e->a, env); }
            Value v = v_copy(L->u.l->items[lix(i, L->u.l, e, 0)]);
            if (own) v_free(&tmp);
            return chkd(v, e);
        }
        Value tmp; int own = 0; Value *m = NULL;
        int r = 0, cc = 0; double *d = NULL;
        if (e->a->k == N_OBJPROP) d = gfx_objmat(e->a, env, &r, &cc);
        else {
            m = ev_ref(e->a, env);
            if (!m) { tmp = ev(e->a, env); m = &tmp; own = 1; }
            if (e->anyBase && m->k != V_MAT) { char *k = kind_txt(*m); RT(e, "[행, 열] 은 행렬에만 쓸 수 있습니다 (값: %s)", k); }
            d = m->u.m->d; r = m->u.m->r; cc = m->u.m->c;
        }
        double vi = evn(e->b, env), vj = evn(e->c, env);
        if (e->a->k == N_OBJPROP) d = gfx_objmat(e->a, env, &r, &cc);
        else if (!own) { m = ev_ref(e->a, env); d = m->u.m->d; }
        double v = d[ix(vi, r, "행", e) * cc + ix(vj, cc, "열", e)];
        if (own) v_free(&tmp);
        return v_num(v);
    }
    case N_CALL: {
        if (e->lfind) {
            Value *L = ev_ref(e->lnode, env); Value x = ev(e->args.v[0], env); int r = 0;
            for (int i = 0; i < L->u.l->n; i++) {
                Value y = L->u.l->items[i];
                if (x.k == V_MAT || x.k == V_LIST || L->u.l->comp) { if (v_eq(y, x)) { r = i + 1; break; } }
                else if (y.k == x.k && (x.k == V_NUM ? y.u.n == x.u.n : !strcmp(y.u.s, x.u.s))) { r = i + 1; break; }
            }
            v_free(&x); return v_num(r);
        }
        if (e->canKey) { char *k = evs(e->args.v[0], env); int r = key_on(k); free(k); return v_num(r); }
        if (e->fileEx) {
            char *raw = evs(e->args.v[0], env); char *path = path_resolve(raw, e->file); free(raw);
            extern int rt_file_exists(const char *path);
            int r = path ? rt_file_exists(path) : 0; free(path); return v_num(r);
        }
        if (e->isStrf) {
            Value a[3]; for (int i = 0; i < e->args.n; i++) a[i] = chkd(ev(e->args.v[i], env), e->args.v[i]);
            Value r = strf_call(e->name, a);
            for (int i = 0; i < e->args.n; i++) v_free(&a[i]);
            return r;
        }
        if (!e->isMath) return callValue(e, env);
        double xs[2] = {0, 0};
        for (int i = 0; i < e->args.n; i++) xs[i] = evn(e->args.v[i], env);
        double v = math_call(e->name, xs);
        if (!isfinite(v)) {
            char b[256] = ""; for (int i = 0; i < e->args.n; i++) { char *s = fmt_float(xs[i]); if (i) strcat(b, ", "); strncat(b, s, 60); free(s); }
            RT(e, "%s(%s) 은(는) 계산할 수 없습니다 (범위를 벗어난 값)", e->name, b);
        }
        return v_num(e->ty == TY_INT ? i32(v) : v);
    }
    case N_OBJPROP: case N_OBJKEEP: case N_CANOBJ: return gfx_value(e, env);
    case N_OBJVAR: return v_copy(gfx_ovslot(e, env)->v);
    case N_MEMBER:
        if (e->lnode) return v_num(ev_ref(e->lnode, env)->u.l->n);
        if (e->target == M_CAN || e->target == M_DIS) return gfx_value(e, env);
        if (e->target == M_EXT) return ext_value(e);
        if (e->info && e->info->kind == VK_PUBLIC) return v_copy(env_get(PUB, e->name)->v);
        { Env *le = lib_env(e->info->mod); Slot *s = le ? env_own(le, e->name) : NULL; if (!s) RT(e, "라이브러리 변수 '%s' 이(가) 아직 없습니다", e->name); return v_copy(s->v); }
    case N_UN: {
        Value v = chkd(ev(e->a, env), e->a);
        double x = v.k == V_NUM ? v.u.n : NAN; v_free(&v);
        if (e->op == '!') return v_num(x == 0 ? 1 : 0);
        return v_num(e->ty == TY_INT ? i32(-x) : -x);
    }
    case N_BIN: {
        int op = e->op;
        if (e->ty == TY_ANY) {
            Value a = chkd(ev(e->a, env), e->a), b = chkd(ev(e->b, env), e->b), v;
            if (a.k == V_STR || b.k == V_STR) {
                char *x = fmt_val(a, e->a->ty), *y = fmt_val(b, e->b->ty);
                v = v_strown(jb_fmt("%s%s", x, y)); free(x); free(y);
            } else if (a.k != V_NUM || b.k != V_NUM) RT(e, "행렬 · 리스트 항목에는 '+' 를 쓸 수 없습니다 (문자열과 이어 붙이기만 됨)");
            else v = v_num(a.u.n + b.u.n);
            v_free(&a); v_free(&b);
            return chkd(v, e);
        }
        if (op == '&') { double a = evn(e->a, env); if (a == 0) return v_num(0); return v_num(evn(e->b, env) != 0); }
        if (op == '|') { double a = evn(e->a, env); if (a != 0) return v_num(1); return v_num(evn(e->b, env) != 0); }
        Value a = chkd(ev(e->a, env), e->a), b = chkd(ev(e->b, env), e->b);
        int isI = e->ty == TY_INT; Value r;
        if (op == '+' && e->ty == TY_STRING) {
            char *x = fmt_val(a, e->a->ty), *y = fmt_val(b, e->b->ty);
            size_t lx = strlen(x), ly = strlen(y); char *s = jb_alloc(lx + ly + 1); memcpy(s, x, lx); memcpy(s + lx, y, ly + 1); free(x); free(y);
            v_free(&a); v_free(&b); return v_strown(s);
        }
        if (op == 'e' || op == 'n') { int q = v_eq(a, b); v_free(&a); v_free(&b); return v_num(op == 'e' ? q : !q); }
        double x = a.k == V_NUM ? a.u.n : NAN, y = b.k == V_NUM ? b.u.n : NAN;
        if (a.k == V_STR && b.k == V_STR && (op == '<' || op == '>' || op == 'l' || op == 'g')) {
            int c = strcmp(a.u.s, b.u.s); v_free(&a); v_free(&b);
            return v_num(op == '<' ? c < 0 : op == '>' ? c > 0 : op == 'l' ? c <= 0 : c >= 0);
        }
        v_free(&a); v_free(&b);
        switch (op) {
        case '+': r = v_num(isI ? i32(x + y) : x + y); break;
        case '-': r = v_num(isI ? i32(x - y) : x - y); break;
        case '*': r = v_num(isI ? (double)(int32_t)((uint32_t)i32(x) * (uint32_t)i32(y)) : x * y); break;
        case '/': if (isI) { if (y == 0) RT(e, "0으로 나눌 수 없습니다"); r = v_num(i32(trunc(x / y))); } else r = v_num(x / y); break;
        case '%': if (y == 0) RT(e, "0으로 나눈 나머지는 구할 수 없습니다"); r = v_num(fmod(x, y)); break;
        case '<': r = v_num(x < y); break; case '>': r = v_num(x > y); break;
        case 'l': r = v_num(x <= y); break; case 'g': r = v_num(x >= y); break;
        default: r = v_num(0);
        }
        return r;
    }
    }
    RT(e, "계산할 수 없는 식입니다");
    return v_num(0);
}

/* ---------- 문장 ---------- */
static void slice(void) {
    if ((++C.ops & 127) != 0) return;
    if (C.sync > 0) {
        if (perf_now() - C.syncStart > 8000) RT(C.syncNode, "값을 돌려주는 함수가 8초 넘게 끝나지 않습니다 (무한 반복?)");
        return;
    }
    if (perf_now() - C.sliceT > 4) gil_yield();
}
int rt_frozen_env(Tk *tk, Env *env);
int seq(NodeList *l, int i, Env *env) {
    for (; i < l->n; i++) {
        if (env->tk && env->tk->dead) return S_DEAD;
        if (env->tk && env->tk->paused) {
            while (env->tk->paused && !env->tk->dead) rt_wait_frame();
            if (env->tk->dead) return S_DEAD;
        }
        if (C.sync == 0 && env_self(env)) { int g = gfx_seq_gate(env); if (g != S_OK) return g; }
        int r = exec(l->v[i], env);
        if (r == S_GOTO) {
            int g = -1; for (int q = 0; q < l->n; q++) if (l->v[q]->line == C.gotoLine && l->v[q]->file == C.gotoFile) { g = q; break; }
            if (g < 0) return r;
            slice();
            i = g - 1; continue;
        }
        if (r != S_OK) return r;
    }
    return S_OK;
}
int block(NodeList *l, Env *env) { Env *b = env_new(env); int r = seq(l, 0, b); env_free(b); return r; }
static int runIf(Node *s, Env *env) {
    for (;;) {
        int r;
        if (evn(s->expr, env) != 0) r = block(&s->then, env);
        else if (s->hasEls) r = s->elseIf ? exec(s->els.v[0], env) : block(&s->els, env);
        else r = S_OK;
        if ((r == S_BREAK || r == S_CONT) && C.jump == s) { if (r == S_BREAK) return S_OK; slice(); continue; }
        return r;
    }
}
static int runLoop(Node *s, Env *env) {
    double end = -1;
    if (s->ms) end = perf_now() + evn(s->ms, env);
    for (;;) {
        if (end >= 0 && perf_now() >= end) return S_OK;
        if (s->expr && evn(s->expr, env) == 0) return S_OK;
        int r = block(&s->body, env);
        if ((r == S_BREAK || r == S_CONT) && C.jump == s) { if (r == S_BREAK) return S_OK; r = S_OK; }
        if (r != S_OK) return r;
        slice();
    }
}
int exec(Node *s, Env *env) {
    switch (s->k) {
    case N_USE: case N_LIB: case N_FUNC: return S_OK;
    case N_VAR:
        if (s->pub) return S_OK;
        env_def(env, s->name, s->ty, s->hasDims ? s->dims : NULL, s->expr ? co(chkd(ev(s->expr, env), s->expr), s->ty) : dflt(s->ty, s->dims));
        return S_OK;
    case N_ASSIGN: {
        Slot *sl = slot_of(s->info, s->name, env, s);
        Value v = co(chkd(ev(s->expr, env), s->expr), sl->ty);
        sl = slot_of(s->info, s->name, env, s);
        v_free(&sl->v); sl->v = v; return S_OK;
    }
    case N_EASSIGN: {
        if (s->isList) {
            Value *L = ev_ref(s->a, env); Value tmp; int own = 0;
            if (!L) { tmp = ev(s->a, env); L = &tmp; own = 1; }
            if (s->anyBase && L->k != V_LIST) { char *k = kind_txt(*L); RT(s, "[번호] 는 리스트 · compound 에만 쓸 수 있습니다 (값: %s)", k); }
            double i = chkd(ev(s->b, env), s->b).u.n;
            if (!(i >= 1) || !isfinite(i)) { char *t = fmt_any(v_num(i)); RT(s->b, "리스트 번호는 1 이상이어야 합니다 (받은 값: %s)", t); }
            if (!own) L = ev_ref(s->a, env);
            if (i > L->u.l->n && i > 1e6) { char *t = fmt_any(v_num(i)); RT(s->b, "리스트 번호 %s 이(가) 너무 큽니다 (최대 1000000)", t); }
            while (L->u.l->n < (int)trunc(i)) list_push(L->u.l, v_num(0));
            int k = lix(i, L->u.l, s, 0);
            Value v = chkd(ev(s->expr, env), s->expr);
            if (!own) L = ev_ref(s->a, env);
            if (!L->u.l->comp && (v.k == V_LIST || v.k == V_MAT)) { char *kt = kind_txt(v); RT(s->expr, "리스트에는 숫자나 문자열만 넣을 수 있습니다 (받은 값: %s) — compound 를 쓰세요", kt); }
            if (k >= L->u.l->n) k = L->u.l->n - 1;
            v_free(&L->u.l->items[k]); L->u.l->items[k] = v;
            if (own) v_free(&tmp);
            return S_OK;
        }
        int r = 0, cc = 0; double *d;
        Value *m = NULL;
        if (s->a->k == N_OBJPROP) d = gfx_objmat(s->a, env, &r, &cc);
        else {
            m = ev_ref(s->a, env);
            if (!m) RT(s, "값을 넣을 수 있는 변수가 아닙니다");
            if (s->anyMat && m->k != V_MAT) { char *k = kind_txt(*m); RT(s, "[행, 열] 은 행렬에만 쓸 수 있습니다 (값: %s)", k); }
            d = m->u.m->d; r = m->u.m->r; cc = m->u.m->c;
        }
        double vi = evn(s->b, env), vj = evn(s->c, env);
        double v = chkd(ev(s->expr, env), s->expr).u.n;
        if (s->a->k == N_OBJPROP) d = gfx_objmat(s->a, env, &r, &cc); else { m = ev_ref(s->a, env); d = m->u.m->d; }
        int k = ix(vi, r, "행", s) * cc + ix(vj, cc, "열", s);
        if (d[k] == v) return S_OK;
        double old = d[k]; d[k] = v;
        if (s->a->k == N_OBJPROP) gfx_eassign_obj(s->a, env, k, v, old);
        return S_OK;
    }
    case N_OASSIGN: return gfx_oassign(s, env);
    case N_IF: return runIf(s, env);
    case N_LOOP: return runLoop(s, env);
    case N_RUN: return block(&s->body, env);
    case N_CALL: {
        int r;
        if (s->target == M_JAW || s->target == M_LIST) r = jaw_call(s, env);
        else if (s->target == M_EXT) r = ext_call(s, env);
        else if (s->target == M_USER) {
            Node *def = s->tdef; Dc *dc = dcOf(env);
            if (dc->n >= 200) RT(s, "함수 호출이 너무 깊습니다 (무한 재귀?)");
            Env *fenv = env_new(lib_env(s->tmod->name)); fenv->dc = dc; fenv->tk = env->tk;
            for (int i = 0; i < def->nparams; i++) env_def(fenv, def->params[i].name, def->params[i].ty, def->params[i].dims, co(chkd(ev(s->args.v[i], env), s->args.v[i]), def->params[i].ty));
            dc->n++;
            r = seq(&def->body, 0, fenv);
            dc->n--;
            if (r == S_RET) { v_free(&C.ret); r = S_OK; }
            if (r == S_GOTO) jb_fail(C.gotoFile, C.gotoAt, "goTo(%d): %d번째 줄의 문장을 찾을 수 없습니다 — 지금 있는 블록이나 그 바깥 블록의 문장으로만 갈 수 있습니다 (안쪽 블록 · 다른 함수 · 다른 run 으로는 못 감)", C.gotoLine, C.gotoLine);
            env_free(fenv);
        }
        else if (s->target == M_BELONG) r = gfx_belong(s, env);
        else r = gfx_call(s, env);
        if (r == S_OK && C.sig) { r = C.sig; C.sig = 0; }   /* 값 함수 안의 sendto · 귀속 해제 */
        return r;
    }
    }
    RT(s, "알 수 없는 문장입니다");
    return S_OK;
}
static Value callValue(Node *c, Env *env) {
    Node *def = c->tdef; Dc *dc = dcOf(env);
    if (dc->n >= 200) RT(c, "함수 호출이 너무 깊습니다 (무한 재귀?)");
    Env *fenv = env_new(lib_env(c->tmod->name)); fenv->dc = dc; fenv->tk = NULL;
    for (int i = 0; i < def->nparams; i++) env_def(fenv, def->params[i].name, def->params[i].ty, def->params[i].dims, co(chkd(ev(c->args.v[i], env), c->args.v[i]), def->params[i].ty));
    const char *prevSelf = C.selfNow;
    dc->n++;
    if (C.sync++ == 0) { C.syncNode = c; C.syncStart = perf_now(); }
    int r = seq(&def->body, 0, fenv);
    dc->n--; C.sync--; C.selfNow = prevSelf;
    Value out;
    if (r == S_RET) out = C.ret;
    else {
        if (r == S_GOTO) jb_fail(C.gotoFile, C.gotoAt, "goTo(%d): %d번째 줄의 문장을 찾을 수 없습니다 — 지금 있는 블록이나 그 바깥 블록의 문장으로만 갈 수 있습니다 (안쪽 블록 · 다른 함수 · 다른 run 으로는 못 감)", C.gotoLine, C.gotoLine);
        if (r == S_SEND || r == S_DEAD) C.sig = r;
        out = dflt(def->retTy, def->retDims);
    }
    C.ret = v_num(0);
    env_free(fenv);
    return out;
}

/* ---------- 실행 (스레드) ---------- */
static pthread_mutex_t aliveM = PTHREAD_MUTEX_INITIALIZER;
static volatile int alive;
int rt_alive(void) { return alive; }
typedef struct { void (*fn)(void *); void *ud; int now; } Spawn;
static volatile int handoffPending;
static pthread_cond_t handoffC = PTHREAD_COND_INITIALIZER;
static void *spawnMain(void *a) {
    Spawn s = *(Spawn *)a; free(a);
    if (s.now) {   /* 만든 실행에게서 차례를 바로 넘겨받음 */
        pthread_mutex_lock(&Gm); while (!handoffPending) pthread_cond_wait(&handoffC, &Gm); handoffPending = 0; pthread_mutex_unlock(&Gm);
        C.sliceT = perf_now();
    } else gil_lock();
    memset(&C, 0, sizeof C); C.sliceT = perf_now();
    s.fn(s.ud);
    pthread_mutex_lock(&aliveM); alive--; pthread_mutex_unlock(&aliveM);
    gil_unlock();
    return NULL;
}
static void spawnT(void (*fn)(void *), void *ud, int now);
void rt_spawn(void (*fn)(void *), void *ud) { spawnT(fn, ud, 0); }
void rt_spawn_now(void (*fn)(void *), void *ud) {
    if (C.sync > 0) { spawnT(fn, ud, 0); return; }
    Ctl save = C;
    spawnT(fn, ud, 1);
    /* 맨 앞에 줄 서서 새 실행이 처음 차례를 넘길 때 이어서 */
    pthread_mutex_lock(&Gm);
    if (!myW.init) { pthread_cond_init(&myW.c, NULL); myW.init = 1; }
    myW.go = 0; myW.next = qh; qh = &myW; if (!qt) qt = &myW; gWaiting++;
    handoffPending = 1; pthread_cond_signal(&handoffC);
    while (!myW.go) pthread_cond_wait(&myW.c, &Gm);
    pthread_mutex_unlock(&Gm);
    C = save; C.sliceT = perf_now();
}
static void spawnT(void (*fn)(void *), void *ud, int now) {
    Spawn *s = jb_alloc(sizeof *s); s->fn = fn; s->ud = ud; s->now = now;
    pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 32 << 20);
    pthread_mutex_lock(&aliveM); alive++; pthread_mutex_unlock(&aliveM);
    if (pthread_create(&t, &at, spawnMain, s)) { fprintf(stderr, "실행(스레드)을 시작할 수 없습니다\n"); _exit(1); }
    pthread_attr_destroy(&at);
}
typedef struct { NodeList *body; Env *env; } RunArg;
static void runThread(void *a) {
    RunArg r = *(RunArg *)a; free(a);
    rt_run_body_thread(r.body, r.env);
}
/* run{ } 몸통을 이 스레드에서 끝까지 — sendto 면 그 파일로 넘어감 */
int rt_run_body_thread(NodeList *body, Env *env) {
    int r = seq(body, 0, env);
    if (r == S_SEND) {
        const char *p = C.send;
        rt_sys("→ sendto %s", p);
        rt_sleep_ms(0);
        r = rt_run_task_here(p);
    }
    if (r == S_GOTO) jb_fail(C.gotoFile, C.gotoAt, "goTo(%d): %d번째 줄의 문장을 찾을 수 없습니다 — 지금 있는 블록이나 그 바깥 블록의 문장으로만 갈 수 있습니다 (안쪽 블록 · 다른 함수 · 다른 run 으로는 못 감)", C.gotoLine, C.gotoLine);
    return r;
}
static void spawnRun(NodeList *body, Env *env) { RunArg *a = jb_alloc(sizeof *a); a->body = body; a->env = env; rt_spawn_now(runThread, a); }
/* 파일 최상위 var 를 만들고 run{ } 들을 차례로 시작 (각 run 은 첫 기다림까지 바로 실행 — 편집기와 같음) */
int rt_run_task_here(const char *path) {
    Module *m = task_by_path(path);
    Env *env = env_new(NULL);
    for (int i = 0; i < m->ast.n; i++) if (m->ast.v[i]->k == N_VAR) exec(m->ast.v[i], env);
    for (int i = 0; i < m->ast.n; i++) {
        Node *r = m->ast.v[i]; if (r->k != N_RUN) continue;
        Env *renv = env_new(env); env_add_run(env, renv); renv->dc = jb_calloc(sizeof(Dc));
        spawnRun(&r->body, renv);
    }
    return S_OK;
}
typedef struct { const char *path; } SetupArg;
static void setupThread(void *a) { const char *p = ((SetupArg *)a)->path; free(a); rt_run_task_here(p); }
void rt_setup_task(const char *path) { SetupArg *a = jb_alloc(sizeof *a); a->path = path; rt_spawn(setupThread, a); }

/* 조건이 될 때까지 (프레임마다 확인) — S_OK · S_DEAD */
int rt_wait_until(int (*pred)(void *), void *ud, Env *env, int countFrozen) {
    (void)countFrozen;
    while (!pred(ud)) {
        if (env && env->tk && env->tk->dead) return S_DEAD;
        rt_wait_tick();
    }
    return S_OK;
}

/* ---------- jawbreaker · 리스트 명령 ---------- */
typedef struct { char *path; char *text; } Out;
static Out *outs; static int nouts;
static struct { const char *id; Value v; } *jsons; static int njsons;
static const char *outText(const char *path) { for (int i = 0; i < nouts; i++) if (!strcmp(outs[i].path, path)) return outs[i].text; return NULL; }
int rt_file_exists(const char *path) {
    if (outText(path) || proj_file(path)) return 1;
    size_t L = strlen(path);
    for (int i = 0; i < nouts; i++) if (!strncmp(outs[i].path, path, L) && outs[i].path[L] == '/') return 1;
    for (int i = 0; i < P.nfiles; i++) if (!strncmp(P.files[i].path, path, L) && P.files[i].path[L] == '/') return 1;
    return 0;
}
static int jsonPathOk(const char *p) {   /* ^([\p{L}\p{N}_-]+/)*[\p{L}\p{N}_-]+\.json$ */
    size_t L = strlen(p); if (L < 6 || strcmp(p + L - 5, ".json")) return 0;
    const char *seg = p;
    for (const char *q = p; ; q++) {
        if (*q == '/' || q == p + L - 5) {
            if (q == seg) return 0;
            if (q == p + L - 5) break;
            seg = q + 1; continue;
        }
        unsigned char c = (unsigned char)*q;
        if (!(c == '_' || c == '-' || (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0x80)) return 0;
    }
    return 1;
}
static void writeOut(const char *path, const char *text) {
    int k = -1; for (int i = 0; i < nouts; i++) if (!strcmp(outs[i].path, path)) k = i;
    if (k < 0) { outs = jb_realloc(outs, sizeof(Out) * (nouts + 1)); k = nouts++; outs[k].path = jb_strdup(path); outs[k].text = NULL; }
    free(outs[k].text); outs[k].text = jb_strdup(text);
    char *full = jb_fmt("%s/%s", P.root, path);
    for (char *q = full + strlen(P.root) + 1; *q; q++) if (*q == '/') { *q = 0; mkdir(full, 0755); *q = '/'; }
    FILE *f = fopen(full, "wb"); if (f) { fputs(text, f); fclose(f); }
    free(full);
}
/* 난독화 (jaw.exportFile · openFile): xorshift 키 · base64 · 뒤집기 · "JBX1" */
static void keyStream(unsigned char *k, size_t n) { uint32_t x = 0x6A62786Bu; for (size_t i = 0; i < n; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; k[i] = (unsigned char)((x ^ (uint32_t)(i * 131)) & 255); } }
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char *obf(const char *t) {
    size_t n = strlen(t); unsigned char *k = jb_alloc(n + 1), *b = jb_alloc(n + 1); keyStream(k, n);
    for (size_t i = 0; i < n; i++) b[i] = (unsigned char)t[i] ^ k[i];
    size_t on = 4 * ((n + 2) / 3); char *o = jb_alloc(on + 5); size_t q = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = b[i] << 16 | (i + 1 < n ? b[i+1] << 8 : 0) | (i + 2 < n ? b[i+2] : 0);
        o[q++] = B64[v >> 18 & 63]; o[q++] = B64[v >> 12 & 63]; o[q++] = i + 1 < n ? B64[v >> 6 & 63] : '='; o[q++] = i + 2 < n ? B64[v & 63] : '=';
    }
    char *r = jb_alloc(on + 5); memcpy(r, "JBX1", 4); for (size_t i = 0; i < q; i++) r[4 + i] = o[q - 1 - i]; r[4 + q] = 0;
    free(k); free(b); free(o); return r;
}
static char *deobf(const char *t0) {
    while (*t0 == ' ' || *t0 == '\n' || *t0 == '\r' || *t0 == '\t') t0++;
    if (strncmp(t0, "JBX1", 4)) return jb_strdup(t0);
    char *s = jb_strdup(t0 + 4); size_t L = strlen(s); while (L && (s[L-1] == ' ' || s[L-1] == '\n' || s[L-1] == '\r')) s[--L] = 0;
    for (size_t i = 0; i < L / 2; i++) { char c = s[i]; s[i] = s[L-1-i]; s[L-1-i] = c; }
    unsigned char *b = jb_alloc(L + 1); size_t n = 0; uint32_t acc = 0; int bits = 0;
    for (size_t i = 0; i < L; i++) { const char *p = strchr(B64, s[i]); if (!p || !s[i]) continue; acc = acc << 6 | (uint32_t)(p - B64); bits += 6; if (bits >= 8) { bits -= 8; b[n++] = (unsigned char)(acc >> bits & 255); } }
    unsigned char *k = jb_alloc(n + 1); keyStream(k, n);
    for (size_t i = 0; i < n; i++) b[i] ^= k[i];
    b[n] = 0; free(k); free(s); return (char *)b;
}
static int needAsync(Node *c) {
    if (C.sync > 0) RT(c, "값을 돌려주는 함수 안에서는 기다리는 명령(wait · dis.waitF · moveObj … for · sendto)을 쓸 수 없습니다");
    return 1;
}
int rt_need_async(Node *c) { return needAsync(c); }
static double SLEEP_MIN = 0;
static int doSleep(double ms, Env *env, Node *c) {
    needAsync(c);
    double end = perf_now() + (ms > 0 ? (double)i32(ms) : 0), last = perf_now(); int waited = 0;
    if (ms > 0 && (double)i32(ms) < 0) end = perf_now();
    for (;;) {
        if (env->tk && env->tk->dead) return S_DEAD;
        double now = perf_now(); if (rt_frozen_env(env->tk, env)) end += now - last; last = now;
        double rem = end - perf_now(); if (rem <= 0 && waited) break;
        double w = rem < 0 ? 0 : rem > 50 ? 50 : rem; if (w < SLEEP_MIN) w = SLEEP_MIN;
        rt_sleep_ms(w); waited = 1;
    }
    C.sliceT = perf_now();
    return S_OK;
}
int jaw_call(Node *c, Env *env) {
    C.selfNow = env_self(env);
    const char *n = c->name;
    if (c->target == M_LIST) {
        Value *L = ev_ref(c->lnode, env);
        if (!strcmp(n, "add")) { Value v = v_own(chkd(ev(c->args.v[0], env), c->args.v[0])); L = ev_ref(c->lnode, env); list_push(L->u.l, v); return S_OK; }
        if (!strcmp(n, "insert")) {
            double i = chkd(ev(c->args.v[0], env), c->args.v[0]).u.n; L = ev_ref(c->lnode, env);
            int k = lix(i, L->u.l, c, 1);
            Value v = v_own(chkd(ev(c->args.v[1], env), c->args.v[1])); L = ev_ref(c->lnode, env);
            list_push(L->u.l, v_num(0)); if (k > L->u.l->n - 1) k = L->u.l->n - 1;
            memmove(&L->u.l->items[k + 1], &L->u.l->items[k], sizeof(Value) * (L->u.l->n - 1 - k)); L->u.l->items[k] = v;
            return S_OK;
        }
        if (!strcmp(n, "remove")) {
            double i = chkd(ev(c->args.v[0], env), c->args.v[0]).u.n; L = ev_ref(c->lnode, env);
            int k = lix(i, L->u.l, c, 0);
            v_free(&L->u.l->items[k]); memmove(&L->u.l->items[k], &L->u.l->items[k + 1], sizeof(Value) * (L->u.l->n - 1 - k)); L->u.l->n--;
            return S_OK;
        }
        if (!strcmp(n, "clear")) { for (int i = 0; i < L->u.l->n; i++) v_free(&L->u.l->items[i]); L->u.l->n = 0; return S_OK; }
        return S_OK;
    }
    if (!strcmp(n, "log") || !strcmp(n, "print")) {
        Value *r = ev_ref(c->args.v[0], env); char *s;
        if (r && !c->args.v[0]->chk) s = fmt_val(*r, c->args.v[0]->ty);
        else { Value v = chkd(ev(c->args.v[0], env), c->args.v[0]); s = fmt_val(v, c->args.v[0]->ty); v_free(&v); }
        gil_unlock(); fputs(s, stdout); fputc('\n', stdout); fflush(stdout); gil_lock();
        free(s); return S_OK;
    }
    if (!strcmp(n, "wait")) { double ms = chkd(ev(c->args.v[0], env), c->args.v[0]).u.n; return doSleep(ms, env, c); }
    if (!strcmp(n, "keyClick") || !strcmp(n, "keyPush") || !strcmp(n, "keyRlse")) {
        char *k = evs(c->args.v[0], env); char kb[64]; key_canon(k, kb, sizeof kb); free(k);
        if (!*kb) RT(c->args.v[0], "키 이름이 비어 있습니다");
        extern void in_virtual_key(const char *k, int mode);
        in_virtual_key(kb, !strcmp(n, "keyClick") ? 0 : !strcmp(n, "keyPush") ? 1 : 2);
        return S_OK;
    }
    if (!strcmp(n, "setJson")) {
        char *id = evs(c->args.v[0], env);
        if (!*id) RT(c->args.v[0], "JSON id 가 비어 있습니다");
        Value v = ev(c->args.v[1], env); const char *iid = jb_in(id); free(id);
        for (int i = 0; i < njsons; i++) if (jsons[i].id == iid) { v_free(&jsons[i].v); jsons[i].v = v; return S_OK; }
        jsons = jb_realloc(jsons, sizeof(*jsons) * (njsons + 1)); jsons[njsons].id = iid; jsons[njsons].v = v; njsons++;
        return S_OK;
    }
    if (!strcmp(n, "genJson")) {
        char *raw = evs(c->args.v[0], env), *id = evs(c->args.v[1], env), *path = path_resolve(raw, c->file);
        if (!path || !jsonPathOk(path)) RT(c->args.v[0], "genJson 의 경로는 이름.json 형태여야 합니다 (폴더/이름.json 가능 · 받은 값: \"%s\")", raw);
        const char *iid = jb_in(id); int k = -1; for (int i = 0; i < njsons; i++) if (jsons[i].id == iid) k = i;
        if (k < 0) RT(c->args.v[1], "JSON '%s' 이(가) 없습니다 (먼저 jaw.setJson(\"%s\", 값);)", id, id);
        char *txt = json_of(jsons[k].v); writeOut(path, txt); free(txt);
        rt_sys("💾 %s ← JSON '%s'", path, id);
        free(raw); free(id); free(path); return S_OK;
    }
    if (!strcmp(n, "exportFile")) {
        char *raw = evs(c->args.v[0], env), *name = evs(c->args.v[1], env), *path = path_resolve(raw, c->file);
        if (!*name) { free(name); name = jb_strdup("file.jbx"); }
        if (!path || !jsonPathOk(path)) RT(c->args.v[0], "exportFile 의 경로는 이름.json 형태여야 합니다 (받은 값: \"%s\")", raw);
        const char *txt = outText(path); if (!txt) txt = proj_text(path);
        if (!txt) RT(c->args.v[0], "프로젝트에 '%s' 파일이 없습니다", path);
        char *o = obf(txt);
        const char *b = strrchr(name, '/'); b = b ? b + 1 : name;   /* 내려받기 = 지금 폴더에 그 이름으로 */
#ifdef _WIN32
        /* Windows: 저장 창 (취소하면 저장 안 함) */
        char sp[4096]; gil_unlock(); int picked = jb_win_pick(1, b, sp, sizeof sp); gil_lock();
        if (!picked) { rt_sys("📤 %s 저장 취소", b); free(o); free(raw); free(name); free(path); return S_OK; }
        b = sp;
#endif
        FILE *f = fopen(b, "wb"); if (f) { fputs(o, f); fclose(f); }
        rt_sys("📤 %s → %s (난독화)", path, b);
        free(o); free(raw); free(name); free(path); return S_OK;
    }
    if (!strcmp(n, "openFile")) {
        char *raw = evs(c->args.v[0], env), *path = path_resolve(raw, c->file);
        if (!path || !jsonPathOk(path)) RT(c->args.v[0], "openFile 의 경로는 이름.json 형태여야 합니다 (받은 값: \"%s\")", raw);
        needAsync(c);
        /* 파일 고르기: 터미널에서 경로를 입력 (없으면 취소) — 환경 변수 JB_OPEN_FILE 이 있으면 그 파일 */
        char line[4096] = "";
        const char *envp = getenv("JB_OPEN_FILE");
        gil_unlock();
        if (envp) snprintf(line, sizeof line, "%s", envp);
#ifdef _WIN32
        else if (!jb_win_pick(0, NULL, line, sizeof line)) line[0] = 0;   /* Windows: 열기 창 */
#else
        else if (isatty(0)) { fprintf(stderr, "📥 jaw.openFile(\"%s\") — 열 파일 경로 (빈 줄 = 취소): ", path); fflush(stderr); if (!fgets(line, sizeof line, stdin)) line[0] = 0; }
#endif
        gil_lock();
        size_t L = strlen(line); while (L && (line[L-1] == '\n' || line[L-1] == '\r')) line[--L] = 0;
        if (*line) {
            FILE *f = fopen(line, "rb");
            if (f) {
                fseek(f, 0, SEEK_END); long fl = ftell(f); fseek(f, 0, SEEK_SET);
                char *b = jb_alloc(fl + 1); size_t rr = fread(b, 1, fl, f); b[rr] = 0; fclose(f);
                char *t = deobf(b); Value tmp; int ok = json_parse(t, &tmp);
                if (ok == 1) { v_free(&tmp); writeOut(path, t); rt_sys("📥 %s → %s", line, path); }
                else if (ok == -1) { writeOut(path, t); rt_sys("📥 %s → %s", line, path); }
                else rt_sys("📥 %s: 읽을 수 없는 파일 (JSON 이 아님)", line);
                free(t); free(b);
            }
        }
        free(raw); free(path); return S_OK;
    }
    if (!strcmp(n, "loadJson")) {
        char *raw = evs(c->args.v[0], env), *path = path_resolve(raw, c->file);
        if (!path || !jsonPathOk(path)) RT(c->args.v[0], "loadJson 의 경로는 이름.json 형태여야 합니다 (받은 값: \"%s\")", raw);
        const char *txt = outText(path); if (!txt) txt = proj_text(path);
        if (!txt) RT(c->args.v[0], "프로젝트에 '%s' 파일이 없습니다", path);
        Value v; int ok = json_parse(txt, &v);
        if (ok == 0) RT(c->args.v[0], "'%s' 은(는) 올바른 JSON 이 아닙니다", path);
        if (ok < 0) RT(c->args.v[0], "'%s' 에 { } (객체)가 있습니다 — loadJson 은 [ ] · 숫자 · 문자열 · true/false · null 만 읽습니다", path);
        if (v.k != V_LIST) { Value l = v_list(1); list_push(l.u.l, v); v = l; }
        Slot *sl = slot_of(c->jinfo, c->jname, env, c);
        v_free(&sl->v); sl->v = v;
        rt_sys("📂 %s → %s", path, c->jname);
        free(raw); free(path); return S_OK;
    }
    if (!strcmp(n, "terminate")) { fflush(stdout); au_stop_all(); _exit(0); }
    if (!strcmp(n, "sendto")) { C.send = c->taskPath; return S_SEND; }
    if (!strcmp(n, "goTo")) { C.gotoLine = i32(evn(c->args.v[0], env)); C.gotoFile = c->file; C.gotoAt = c->line; return S_GOTO; }
    if (!strcmp(n, "label")) return S_OK;
    if (!strcmp(n, "break") || !strcmp(n, "return")) {
        if (c->ret) { C.ret = co(chkd(ev(c->args.v[0], env), c->args.v[0]), c->retTy); return S_RET; }
        C.jump = c->jump; return !strcmp(n, "break") ? S_BREAK : S_CONT;
    }
    if (!strcmp(n, "setup")) {
        if (rt_alive() >= 4096) RT(c, "setup 으로 동시에 실행 중인 파일이 너무 많습니다");
        rt_sys("⇉ setup %s", c->taskPath);
        rt_setup_task(c->taskPath);
        return S_OK;
    }
    return S_OK;
}

/* ---------- 시작 ---------- */
static void mainTask(void *ud) { (void)ud; rt_run_task_here(P.main); }
void jb_run(void) {
    { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); RT_T0 = t.tv_sec * 1000.0 + t.tv_nsec / 1e6; }
    srand((unsigned)time(NULL));
    gil_lock();
    memset(&C, 0, sizeof C);
    PUB = env_new(NULL);
    for (int i = 0; i < P.npublics; i++) env_def(PUB, P.publics[i]->name, P.publics[i]->ty, P.publics[i]->hasDims ? P.publics[i]->dims : NULL, dflt(P.publics[i]->ty, P.publics[i]->dims));
    NLIB_ENVS = P.nlibs; LIB_ENVS = jb_calloc(sizeof(Env*) * (P.nlibs + 1)); LIB_MODS = jb_calloc(sizeof(Module*) * (P.nlibs + 1));
    for (int i = 0; i < P.nlibs; i++) {
        Module *m = P.libs[i]; LIB_MODS[i] = m;
        LIB_ENVS[i] = env_new(m->parentId ? lib_env(m->parentId) : NULL);
    }
    gfx_init();
    ext_init_all();
    for (int i = 0; i < P.nlibs; i++) for (int q = 0; q < P.libs[i]->ast.n; q++) if (P.libs[i]->ast.v[q]->k == N_VAR) exec(P.libs[i]->ast.v[q], LIB_ENVS[i]);
    rt_spawn(mainTask, NULL);
    /* 메인 스레드: 입력 · 내부 틱 (250 tps) · 화면 (60 fps) */
    double last = perf_now(), nextFrame = perf_now();
    for (;;) {
        double now = perf_now();
        rd_poll();
        gfx_tick_loop(now - last); last = now;
        rt_tick_signal();
        if (now >= nextFrame) {
            ext_frame_all(now);
            if (rd_is_open()) rd_paint();
            nextFrame += 1000.0 / 60; if (nextFrame < now) nextFrame = now + 1000.0 / 60;
            gil_unlock();
            if (rd_is_open()) rd_present();
            rt_frame_signal();
            gil_lock();
        }
        int done = rt_alive() == 0;
        if (done && !rd_is_open()) break;
        gil_unlock();
        double wait = 4 - (perf_now() - now); if (wait > 0) { struct timespec t = {0, (long)(wait * 1e6)}; nanosleep(&t, NULL); }
        gil_lock();
    }
    ext_stop_all();
    gfx_end();
    gil_unlock();
    fflush(stdout);
    au_quit();
    rd_quit();
}
