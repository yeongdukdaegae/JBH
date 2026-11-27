/* DisPlayer · colliders · Canvas · Magellan · soundMaster · SeongBin — 편집기 실행기의 오브젝트 부분을 옮김
 *   오브젝트 · 부모/자식 · 묶음 · 회전 · 충돌 · 트리거 · moveObj · tween · 애니메이션 · 타일맵 · 장면 · 팝업 · 카메라 · 버튼 · 스피커
 */
#define _GNU_SOURCE
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define D2R (M_PI / 180.0)
#define GRAVITY 980.0

extern int jb_hex_ok(const char *);
extern int jb_parse_nudge(const char *v, int *nx, int *ny);
extern int jb_ease_id(const char *v);
extern double jb_ease(int id, double k);
extern int jb_is_img(const char *), jb_is_aud(const char *), jb_is_ttf(const char *), jb_is_tl(const char *);
extern int jb_parse_objvar(const char *v, int *key, int *idx);
extern int jb_assignable(int to, int from);
static int32_t i32(double x) { if (!isfinite(x)) return 0; double t = fmod(trunc(x), 4294967296.0); if (t < 0) t += 4294967296.0; return (int32_t)(uint32_t)t; }

/* ---------- 포인터 키 해시 (이름은 jb_intern 이라 포인터로 비교) ---------- */
typedef struct { const void *k; void *v; } HE;
typedef struct { HE *t; int cap, n; } HM;
static unsigned hp(const void *k) { uintptr_t x = (uintptr_t)k; x ^= x >> 17; x *= 0xed5ad4bbU; x ^= x >> 11; return (unsigned)x; }
static void *hm_get(HM *m, const void *k) {
    if (!m->cap) return NULL;
    for (unsigned i = hp(k) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) { if (!m->t[i].k) return NULL; if (m->t[i].k == k) return m->t[i].v; }
}
static void hm_put(HM *m, const void *k, void *v);
static void hm_grow(HM *m) {
    HM n = {jb_calloc(sizeof(HE) * (m->cap ? m->cap * 2 : 64)), m->cap ? m->cap * 2 : 64, 0};
    for (int i = 0; i < m->cap; i++) if (m->t[i].k && m->t[i].v) hm_put(&n, m->t[i].k, m->t[i].v);
    free(m->t); *m = n;
}
static void hm_put(HM *m, const void *k, void *v) {
    if ((m->n + 1) * 2 > m->cap) hm_grow(m);
    for (unsigned i = hp(k) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) {
        if (!m->t[i].k) { m->t[i].k = k; m->t[i].v = v; m->n++; return; }
        if (m->t[i].k == k) { m->t[i].v = v; return; }
    }
}
static void hm_del(HM *m, const void *k) { if (!m->cap) return; for (unsigned i = hp(k) & (m->cap - 1);; i = (i + 1) & (m->cap - 1)) { if (!m->t[i].k) return; if (m->t[i].k == k) { m->t[i].v = NULL; return; } } }

/* 순서 있는 id 집합 */
typedef struct { const char **v; int n, cap; } IdSet;
static int is_has(IdSet *s, const char *id) { if (!s) return 0; for (int i = 0; i < s->n; i++) if (s->v[i] == id) return 1; return 0; }
static void is_add(IdSet *s, const char *id) { if (is_has(s, id)) return; if (s->n == s->cap) { s->cap = s->cap ? s->cap * 2 : 4; s->v = jb_realloc(s->v, sizeof(char*) * s->cap); } s->v[s->n++] = id; }
static void is_del(IdSet *s, const char *id) { if (!s) return; for (int i = 0; i < s->n; i++) if (s->v[i] == id) { memmove(&s->v[i], &s->v[i + 1], sizeof(char*) * (s->n - 1 - i)); s->n--; return; } }
static IdSet *setOf(HM *m, const char *id, int make) { IdSet *s = hm_get(m, id); if (!s && make) { s = jb_calloc(sizeof(IdSet)); hm_put(m, id, s); } return s; }

/* ---------- 오브젝트 저장 (만든 순서 · 같은 id 로 다시 넣으면 자리 그대로) ---------- */
static Obj **ORD; static int NORD, CAPORD, HOLES;
static HM OIDX;   /* id → (index + 1) */
static Obj *obj_get(const char *id) { intptr_t i = (intptr_t)hm_get(&OIDX, id); return i ? ORD[i - 1] : NULL; }
static int obj_has(const char *id) { return obj_get(id) != NULL; }
static void obj_compact(void) {
    int w = 0; for (int i = 0; i < NORD; i++) if (ORD[i]) ORD[w++] = ORD[i];
    NORD = w; HOLES = 0; free(OIDX.t); memset(&OIDX, 0, sizeof OIDX);
    for (int i = 0; i < NORD; i++) hm_put(&OIDX, ORD[i]->id, (void *)(intptr_t)(i + 1));
}
static void obj_set(Obj *o) {
    intptr_t i = (intptr_t)hm_get(&OIDX, o->id);
    if (i) { Obj *old = ORD[i - 1]; if (old && old != o) old->dead = 1; ORD[i - 1] = o; return; }
    if (NORD == CAPORD) { CAPORD = CAPORD ? CAPORD * 2 : 256; ORD = jb_realloc(ORD, sizeof(Obj*) * CAPORD); }
    ORD[NORD++] = o; hm_put(&OIDX, o->id, (void *)(intptr_t)NORD);
}
static void obj_delete(const char *id) {
    intptr_t i = (intptr_t)hm_get(&OIDX, id); if (!i) return;
    if (ORD[i - 1]) ORD[i - 1]->dead = 1;
    ORD[i - 1] = NULL; hm_del(&OIDX, id); HOLES++;
    if (HOLES > 64 && HOLES * 2 > NORD) obj_compact();
}
#define FOR_OBJ(o) for (int _i = 0; _i < NORD; _i++) for (Obj *o = ORD[_i]; o; o = NULL)
static Obj *obj_new(const char *id, int kind) {
    Obj *o = jb_calloc(sizeof(Obj)); o->id = id; o->kind = kind; o->alpha = -1; strcpy(o->color, "#000");
    return o;
}

/* ---------- 상태 ---------- */
static int objOn, layerOn, offOn, sceneOn, curScene, sceneN, colOn, trigOn, camOn, disOn;
static double gravScale = 1, renderLim = 0;
static long physN = 1;
static HM kids, links;           /* id → IdSet */
static HM imgAlias;              /* 이름 → 경로 */
typedef struct { const char *img, *path; double x1, y1, x2, y2; Node *n; } TexDef;
static HM texDefs;
typedef struct { const char **frames; int n; double ms; } AnimeDef;
static HM animes;
static Obj **animObjs; static int nAnimObjs, capAnimObjs;
static HM sceneByName; static const char **sceneNames; static int nSceneNames;   /* 번호 → 이름 */
static HM camMainByScene;        /* (장면 + 1) → id */
static const char **camOrder; static int nCamOrder;
static HM keyBinds;              /* 키 → IdSet */
int GFX_SHOW_COL;
static char bgColor[8] = "#000";
const char *gfx_bg_color(void) { return bgColor; }
int gfx_dis_on(void) { return disOn; }
static const char *ID_CAM_DEFAULT;

/* 장면 코드 · 팝업 */
static Tk **sceneInsts; static int nSceneInsts;
typedef struct { int scene; Tk **insts; int n; double at; int hasCam; double camPos[2], camZoom; const char *camFollow; volatile int closed; } Pop;
static Pop **popStack; static int nPop;

/* tween · moveObj */
typedef struct Tw { Obj *o; int prop; double from[4], to[4]; double start, ms; int nv; volatile int started, done; int active; } Tw;   /* prop: 1 scale 2 alpha 3 angle 4 ov 5 color */
static Tw **tweens; static int nTw, capTw;
typedef struct An { Obj *o; const char *id; int ease; double from[2], to[2], start, ms; int startOnly; volatile int started, done; int active; } An;
static An **anims; static int nAn, capAn;

static void physStart(void);
static void redraw(void);
static void checkTriggers(void);
static void fitText(Obj *o);

/* ---------- 살아 있음 (활성 · 지금 장면) ---------- */
static int inScene(Obj *x) {
    if (!x->hasScenes) return 1;
    for (int i = 0; i < x->nscenes; i++) if (x->scenes[i] == -1 || x->scenes[i] == curScene) return 1;
    return 0;
}
int gfx_live(Obj *o) {
    if (!offOn && !sceneOn) return 1;
    for (Obj *x = o; x; ) {
        if (x->off) return 0;
        if (sceneOn && x->hasScenes && !inScene(x)) return 0;
        x = x->parent ? obj_get(x->parent) : NULL;
    }
    return 1;
}
#define live gfx_live
static void scenesSet1(Obj *o, int s) { free(o->scenes); o->scenes = jb_alloc(sizeof(int)); o->scenes[0] = s; o->nscenes = 1; o->hasScenes = 1; }
static void scenesAdd(Obj *o, int s) { for (int i = 0; i < o->nscenes; i++) if (o->scenes[i] == s) return; o->scenes = jb_realloc(o->scenes, sizeof(int) * (o->nscenes + 1)); o->scenes[o->nscenes++] = s; o->hasScenes = 1; }

/* ---------- 카메라 ---------- */
static int camNow(double *x, double *y, double *z) {
    if (!camOn) return 0;
    Obj *o = obj_get(hm_get(&camMainByScene, (void *)(intptr_t)((sceneOn ? curScene : 0) + 1)));
    if (!o || !live(o)) o = obj_get(ID_CAM_DEFAULT);
    if (!o) return 0;
    *x = o->pos[0]; *y = o->pos[1]; *z = o->zoom ? o->zoom : 1; return 1;
}
static void toW(double x, double y, double *ox, double *oy) {
    double cx, cy, cz; if (camNow(&cx, &cy, &cz)) { *ox = cx + x / cz; *oy = cy + y / cz; } else { *ox = x; *oy = y; }
}
void gfx_draw_list(DrawList *dl) {
    static Obj **buf; static int cap;
    if (cap < NORD + 1) { cap = NORD + 64; buf = jb_realloc(buf, sizeof(Obj*) * cap); }
    int n = 0;
    double cx = 0, cy = 0, cz = 1; int k = camNow(&cx, &cy, &cz);
    FOR_OBJ(o) {
        if ((offOn || sceneOn) && !live(o)) continue;
        if (renderLim > 0 && !o->ui) {
            double R = renderLim / (k ? cz : 1), dx = o->pos[0] - (k ? cx : 0), dy = o->pos[1] - (k ? cy : 0), r = R + hypot(o->scale[0], o->scale[1]) / 2;
            if (dx * dx + dy * dy > r * r) continue;
        }
        if (o->kind == OK_TEXT) fitText(o);
        buf[n++] = o;
    }
    if (layerOn) {   /* 안정 정렬 (같은 레이어는 만든 순서) */
        for (int i = 1; i < n; i++) { Obj *x = buf[i]; int j = i - 1; while (j >= 0 && buf[j]->layer > x->layer) { buf[j + 1] = buf[j]; j--; } buf[j + 1] = x; }
    }
    dl->v = buf; dl->n = n; dl->hasCam = k; dl->camX = cx; dl->camY = cy; dl->camZ = cz;
}

/* ---------- 공통 ---------- */
static const char *NOOBJ = "오브젝트 기능이 꺼져 있습니다 — 먼저 dis.setupObject(); 를 실행하세요";
static const char *NOSCENE = "장면 기능이 꺼져 있습니다 — 먼저 can.setUpScene(); 을 실행하세요";
static Obj *objOf(const char *id, Node *n) {
    if (!objOn) RT(n, "%s", NOOBJ);
    if (!*id && C.selfNow) id = C.selfNow;
    Obj *o = obj_get(jb_in(id));
    if (!o) RT(n, "오브젝트 '%s' 이(가) 없습니다 (dis.setObject 로 먼저 만드세요)", id);
    return o;
}
static void parseColor(Value v, Node *n, char out[8]) {
    if (v.k == V_STR && jb_hex_ok(v.u.s)) {
        const char *h = v.u.s[0] == '#' ? v.u.s + 1 : v.u.s;
        if (strlen(h) == 3) snprintf(out, 8, "#%c%c%c%c%c%c", h[0], h[0], h[1], h[1], h[2], h[2]); else snprintf(out, 8, "#%s", h);
        return;
    }
    if (v.k == V_NUM) {
        char raw[40];
        if (n && n->k == N_NUM && !n->isFloat) snprintf(raw, sizeof raw, "%s", n->raw);
        else { char *s = fmt_any(v); snprintf(raw, sizeof raw, "%s", s); free(s); }
        size_t L = strlen(raw); int dig = 1; for (size_t i = 0; i < L; i++) if (!isdigit((unsigned char)raw[i])) dig = 0;
        if (dig && (L == 3 || L == 6)) { if (L == 3) snprintf(out, 8, "#%c%c%c%c%c%c", raw[0], raw[0], raw[1], raw[1], raw[2], raw[2]); else snprintf(out, 8, "#%s", raw); return; }
        RT(n, "색은 헥스코드여야 합니다 (예: \"#ff8800\" 또는 114514) — 받은 값: %s", raw);
    }
    if (v.k == V_STR) RT(n, "색은 헥스코드여야 합니다 (예: \"#ff8800\" 또는 114514) — 받은 값: \"%s\"", v.u.s);
    RT(n, "색은 헥스코드여야 합니다 (예: \"#ff8800\" 또는 114514)");
}
static void colorArg(Node *a, Env *env, char out[8]) { Value v = ev(a, env); if (a->chk == TY_STRING && v.k != V_STR) { char *k = kind_txt(v); RT(a, "리스트 항목이 문자열이 아닙니다 (값: %s)", k); } parseColor(v, a, out); v_free(&v); }
static double textH(Obj *o) { int lines = 1; for (const char *p = o->text; p && *p; p++) if (*p == '\n') lines++; return fabs(o->scale[1]) * (1 + 1.2 * (lines - 1)); }
static double nudX(Obj *o) { return o->hasNud ? o->nud[0] * fabs(o->scale[0]) / 2 : 0; }
static double nudY(Obj *o) { return o->hasNud ? o->nud[1] * (o->kind == OK_TEXT ? textH(o) : fabs(o->scale[1])) / 2 : 0; }
static double measure(const char *t, double size) {
    if (!strchr(t, '\n')) return rd_measure(t, size);
    double m = 0; const char *p = t;
    for (;;) { const char *e = strchr(p, '\n'); char *l = jb_strndup(p, e ? (size_t)(e - p) : strlen(p)); double w = rd_measure(l, size); free(l); if (w > m) m = w; if (!e) break; p = e + 1; }
    return m;
}
static void fitText(Obj *o) {
    if (o->kind != OK_TEXT) return;
    double size = fabs(o->scale[1]); if (!size) size = o->size;
    if (o->fitOk && o->fitT && !strcmp(o->fitT, o->text) && o->fitS == size && o->scale[0] == o->fitW) return;
    o->size = size; free(o->fitT); o->fitT = jb_strdup(o->text); o->fitS = size;
    o->fitW = o->scale[0] = measure(o->text, size); o->scale[1] = size; o->fitOk = 1;
}
void gfx_font_changed(void) { FOR_OBJ(o) if (o->kind == OK_TEXT) o->fitOk = 0; }
static int needRedraw;
static void redraw(void) { needRedraw = 1; }

/* ---------- 부모 · 자식 · 묶음 · 회전 ---------- */
static void shiftKids(const char *id, double dx, double dy, IdSet *seen) {
    IdSet *ks = setOf(&kids, id, 0), *fw = setOf(&links, id, 0);
    if ((!ks || !ks->n) && (!fw || !fw->n)) return;
    if (dx == 0 && dy == 0) return;
    IdSet local = {0}; int mine = 0;
    if (!seen) { seen = &local; is_add(seen, id); mine = 1; }
    for (int pass = 0; pass < 2; pass++) {
        IdSet *S_ = pass ? fw : ks; if (!S_) continue;
        for (int i = 0; i < S_->n; i++) {
            const char *k = S_->v[i];
            if (is_has(seen, k)) continue; is_add(seen, k);
            Obj *c = obj_get(k); if (!c) continue;
            c->pos[0] += dx; c->pos[1] += dy; shiftKids(k, dx, dy, seen);
        }
    }
    if (mine) free(local.v);
}
static void unlinkAll(const char *id) {
    IdSet *L = setOf(&links, id, 0); if (!L) return;
    for (int i = 0; i < L->n; i++) { IdSet *o = setOf(&links, L->v[i], 0); if (o) { is_del(o, id); if (!o->n) hm_del(&links, L->v[i]); } }
    hm_del(&links, id);
}
static void movePos(Obj *o, double x, double y) { double dx = x - o->pos[0], dy = y - o->pos[1]; o->pos[0] = x; o->pos[1] = y; shiftKids(o->id, dx, dy, NULL); }
static int isAncestor(const char *a, const char *b) {
    int n = 0; for (Obj *x = obj_get(b); x && x->parent && n < 1000; x = obj_get(x->parent), n++) if (x->parent == a) return 1;
    return 0;
}
static void rotGroup(Obj *o, double da) {
    if (!da) return;
    double px = o->pos[0], py = o->pos[1];
    IdSet *L0 = setOf(&links, o->id, 0);
    if (L0 && L0->n) {
        IdSet g = {0}, st = {0}; is_add(&g, o->id); is_add(&st, o->id);
        while (st.n) { const char *x = st.v[--st.n]; IdSet *L = setOf(&links, x, 0); if (L) for (int i = 0; i < L->n; i++) if (!is_has(&g, L->v[i]) && obj_has(L->v[i])) { is_add(&g, L->v[i]); if (st.n == st.cap) { st.cap = st.cap ? st.cap * 2 : 4; st.v = jb_realloc(st.v, sizeof(char*) * st.cap); } st.v[st.n++] = L->v[i]; } }
        px = 0; py = 0; for (int i = 0; i < g.n; i++) { Obj *m = obj_get(g.v[i]); px += m->pos[0]; py += m->pos[1]; } px /= g.n; py /= g.n;
        free(g.v); free(st.v);
    }
    IdSet seen = {0}; is_add(&seen, o->id);
    Obj **all = jb_alloc(sizeof(Obj*) * 8); int na = 0, ca = 8; all[na++] = o;
    for (int qi = 0; qi < na; qi++) {   /* 너비 우선 대신 깊이 우선 (편집기 walk 와 같은 결과) */
        Obj *cur = all[qi];
        for (int pass = 0; pass < 2; pass++) {
            IdSet *S_ = setOf(pass ? &links : &kids, cur->id, 0); if (!S_) continue;
            for (int i = 0; i < S_->n; i++) if (!is_has(&seen, S_->v[i])) { is_add(&seen, S_->v[i]); Obj *m = obj_get(S_->v[i]); if (m) { if (na == ca) { ca *= 2; all = jb_realloc(all, sizeof(Obj*) * ca); } all[na++] = m; } }
        }
    }
    double r = da * D2R, c = cos(r), sn = sin(r);
    for (int i = 0; i < na; i++) { Obj *m = all[i]; double x = m->pos[0] - px, y = m->pos[1] - py; m->pos[0] = px + x * c - y * sn; m->pos[1] = py + x * sn + y * c; m->angle += da; }
    free(all); free(seen.v);
}
static void setAng(Obj *o, double a) { rotGroup(o, a - o->angle); o->angle = a; }
static void uiTree(Obj *o) { o->ui = 1; IdSet *ks = setOf(&kids, o->id, 0); if (ks) for (int i = 0; i < ks->n; i++) { Obj *c = obj_get(ks->v[i]); if (c && !c->ui) uiTree(c); } }

/* ---------- moveObj · tween ---------- */
static An *animOf(const char *id) { for (int i = 0; i < nAn; i++) if (anims[i]->active && anims[i]->o->id == id) return anims[i]; return NULL; }
static void animEnd(An *a) { a->active = 0; a->started = 1; a->done = 1; }
static void animBlock(Obj *o) { An *a = animOf(o->id); if (a && a->o == o) animEnd(a); else if (a) animEnd(a); }
static Tw *twOf(Obj *o, int prop) { for (int i = 0; i < nTw; i++) if (tweens[i]->active && tweens[i]->o->id == o->id && tweens[i]->prop == prop) return tweens[i]; return NULL; }
static void twEnd(Tw *w) { w->active = 0; w->started = 1; w->done = 1; }
static void hexRgb(const char *h, double *rgb) {
    const char *x = h[0] == '#' ? h + 1 : h; char b[7];
    if (strlen(x) == 3) { b[0] = b[1] = x[0]; b[2] = b[3] = x[1]; b[4] = b[5] = x[2]; b[6] = 0; } else { snprintf(b, sizeof b, "%s", x); }
    long n = strtol(b, NULL, 16); rgb[0] = n >> 16 & 255; rgb[1] = n >> 8 & 255; rgb[2] = n & 255;
}
static void rgbHex(const double *v, char out[8]) {
    int c[3]; for (int i = 0; i < 3; i++) { double x = floor(v[i] + 0.5); c[i] = x < 0 ? 0 : x > 255 ? 255 : (int)x; }
    snprintf(out, 8, "#%02x%02x%02x", c[0], c[1], c[2]);
}
static int waitFlag(void *p) { return *(volatile int *)p; }
/* 지금 값 → to 로 ms 동안 — 바뀌기 시작하면 (첫 프레임) 다음 줄 */
static int tween(Obj *o, int prop, const double *to, int nv, double ms, Env *env, Node *c) {
    rt_need_async(c);
    Tw *prev = twOf(o, prop); if (prev) twEnd(prev);
    Tw *w = jb_calloc(sizeof(Tw)); w->o = o; w->prop = prop; w->nv = nv; w->ms = ms; w->start = perf_now(); w->active = 1;
    if (prop == 1) { w->from[0] = o->scale[0]; w->from[1] = o->scale[1]; }
    else if (prop == 2) w->from[0] = o->alpha < 0 ? 255 : o->alpha;
    else if (prop == 3) w->from[0] = o->angle;
    else if (prop == 4) { hexRgb(o->ovColor, w->from); w->from[3] = o->ovA; }
    else hexRgb(o->color, w->from);
    memcpy(w->to, to, sizeof(double) * nv);
    if (nTw == capTw) { capTw = capTw ? capTw * 2 : 16; tweens = jb_realloc(tweens, sizeof(Tw*) * capTw); }
    tweens[nTw++] = w;
    physStart();
    int r = rt_wait_until(waitFlag, (void *)&w->started, env, 0);
    C.sliceT = perf_now();
    return r;
}
static void tweenStep(double now) {
    int w_ = 0;
    for (int i = 0; i < nTw; i++) {
        Tw *w = tweens[i];
        if (!w->active) continue;
        if (obj_get(w->o->id) != w->o) { twEnd(w); continue; }
        if (nPop && !live(w->o)) { tweens[w_++] = w; continue; }
        double k = (now - w->start) / w->ms; if (k > 1) k = 1;
        double v[4]; for (int q = 0; q < w->nv; q++) v[q] = w->from[q] + (w->to[q] - w->from[q]) * k;
        if (w->prop == 1) { w->o->scale[0] = v[0]; w->o->scale[1] = v[1]; }
        else if (w->prop == 2) w->o->alpha = (int)floor(v[0] + 0.5);
        else if (w->prop == 3) setAng(w->o, v[0]);
        else if (w->prop == 4) { rgbHex(v, w->o->ovColor); double a = floor(v[3] + 0.5); w->o->ovA = a < 0 ? 0 : a > 255 ? 255 : (int)a; w->o->hasOv = 1; }
        else rgbHex(v, w->o->color);
        w->started = 1;
        if (k >= 1) { twEnd(w); continue; }
        tweens[w_++] = w;
    }
    nTw = w_;
}
static int pathBlocked(Obj *o, const double *to) {
    if (!colOn || !o->hasCol || !live(o) || o->ui) return 0;
    double hw = fabs(o->colSize[0]) / 2, hh = fabs(o->colSize[1]) / 2, E = 1e-6;
    double sx = o->pos[0] + o->colOff[0], sy = o->pos[1] + o->colOff[1], ex = to[0] + o->colOff[0], ey = to[1] + o->colOff[1];
    double x0 = fmin(sx, ex) - hw, x1 = fmax(sx, ex) + hw, y0 = fmin(sy, ey) - hh, y1 = fmax(sy, ey) + hh;
    IdSet *lk = setOf(&links, o->id, 0);
    FOR_OBJ(b) {
        if (b == o || !b->hasCol || !live(b) || b->ui) continue;
        if ((o->parent || b->parent) && (isAncestor(o->id, b->id) || isAncestor(b->id, o->id))) continue;
        if (lk && is_has(lk, b->id)) continue;
        double bx = b->pos[0] + b->colOff[0], by = b->pos[1] + b->colOff[1], bw = fabs(b->colSize[0]) / 2, bh = fabs(b->colSize[1]) / 2;
        if (fabs(bx - sx) < bw + hw - E && fabs(by - sy) < bh + hh - E) continue;
        if (bx - bw < x1 - E && bx + bw > x0 + E && by - bh < y1 - E && by + bh > y0 + E) return 1;
    }
    return 0;
}
static int moveAnim(Obj *o, const double *to, double ms, int startOnly, int ease, Env *env, Node *c) {
    if (pathBlocked(o, to)) { An *p = animOf(o->id); if (p) animEnd(p); return S_OK; }
    rt_need_async(c);
    An *p = animOf(o->id); if (p) animEnd(p);
    An *a = jb_calloc(sizeof(An)); a->o = o; a->id = o->id; a->ease = ease; a->from[0] = o->pos[0]; a->from[1] = o->pos[1]; a->to[0] = to[0]; a->to[1] = to[1];
    a->start = perf_now(); a->ms = ms; a->startOnly = startOnly; a->active = 1;
    if (nAn == capAn) { capAn = capAn ? capAn * 2 : 16; anims = jb_realloc(anims, sizeof(An*) * capAn); }
    anims[nAn++] = a;
    physStart();
    int r = rt_wait_until(waitFlag, startOnly ? (void *)&a->started : (void *)&a->done, env, 0);
    C.sliceT = perf_now();
    return r;
}

/* ---------- 텍스쳐 · 애니메이션 ---------- */
typedef struct { Img *img; int w, h, failed; const char *path; } ImgE;
static HM imgCache;
static ImgE *imgOf(const char *path, Node *n) {
    ImgE *e = hm_get(&imgCache, path);
    if (!e) {
        e = jb_calloc(sizeof(ImgE)); e->path = path; hm_put(&imgCache, path, e);
        e->img = rd_image(path, &e->w, &e->h, &e->failed);
        if (e->failed) RT(n, "이미지 '%s' 을(를) 읽지 못했습니다 (손상되었거나 지원하지 않는 형식)", path);
    }
    return e;
}
static void showTex(Obj *o, const char *tid, Node *n) {
    TexDef *d = hm_get(&texDefs, tid);
    if (!o->hasTex) strcpy(o->color, "#ffffff");
    ImgE *e = imgOf(d->path, n ? n : d->n);
    o->hasTex = 1; o->tex.ref = e->img; o->tex.tid = d->img; o->tex.path = d->path; o->tex.x1 = d->x1; o->tex.y1 = d->y1; o->tex.x2 = d->x2; o->tex.y2 = d->y2;
    redraw();
}
static void animObjAdd(Obj *o) { for (int i = 0; i < nAnimObjs; i++) if (animObjs[i] == o) return; if (nAnimObjs == capAnimObjs) { capAnimObjs = capAnimObjs ? capAnimObjs * 2 : 16; animObjs = jb_realloc(animObjs, sizeof(Obj*) * capAnimObjs); } animObjs[nAnimObjs++] = o; }
static void animObjDel(Obj *o) { for (int i = 0; i < nAnimObjs; i++) if (animObjs[i] == o) { animObjs[i] = animObjs[--nAnimObjs]; return; } }
static void animeStart(Obj *o, const char *aid, double sp, double t0, Node *n) {
    AnimeDef *A = hm_get(&animes, aid);
    Anime *an = o->an ? o->an : jb_calloc(sizeof(Anime));
    an->id = aid; an->frames = A->frames; an->nframes = A->n; an->dur = A->ms / sp; an->start = t0; an->k = 0; an->hasNext = 0; an->n = n;
    o->an = an; showTex(o, A->frames[0], n); animObjAdd(o); physStart();
}
static void animeStop(Obj *o) { if (o->an) { free(o->an); o->an = NULL; animObjDel(o); } }
static void animeStep(double now) {
    for (int i = 0; i < nAnimObjs; i++) {
        Obj *o = animObjs[i];
        if (obj_get(o->id) != o || !o->an) { animObjs[i--] = animObjs[--nAnimObjs]; continue; }
        if (nPop && !live(o)) continue;
        Anime *A = o->an; long k = (long)floor((now - A->start) / A->dur);
        if (k >= A->nframes) {
            if (A->hasNext) animeStart(o, A->nextId, A->nextSp, A->start + A->dur * A->nframes, A->n);
            else { free(o->an); o->an = NULL; animObjs[i--] = animObjs[--nAnimObjs]; }
            continue;
        }
        if (k != A->k && k >= 0) { A->k = (int)k; showTex(o, A->frames[k], A->n); }
    }
}

/* ---------- 콜라이더 · 트리거 · 물리 ---------- */
typedef struct { double x, y, hx, hy, c, s; } OBB;
static OBB obb(Obj *o, const double *off, const double *size) {
    double r = o->angle * D2R, c = cos(r), s = sin(r); OBB b;
    b.x = o->pos[0] + off[0] * c - off[1] * s; b.y = o->pos[1] + off[0] * s + off[1] * c; b.hx = fabs(size[0]) / 2; b.hy = fabs(size[1]) / 2; b.c = c; b.s = s; return b;
}
static int satHit(OBB *A, OBB *B, int touch, double *out) {
    double dx = A->x - B->x, dy = A->y - B->y, best = INFINITY, nx = 0, ny = 0;
    double ax[4][2] = {{A->c, A->s}, {-A->s, A->c}, {B->c, B->s}, {-B->s, B->c}};
    for (int q = 0; q < 4; q++) {
        double lx = ax[q][0], ly = ax[q][1];
        double ra = A->hx * fabs(A->c * lx + A->s * ly) + A->hy * fabs(-A->s * lx + A->c * ly);
        double rb = B->hx * fabs(B->c * lx + B->s * ly) + B->hy * fabs(-B->s * lx + B->c * ly);
        double d = dx * lx + dy * ly, ov = ra + rb - fabs(d);
        if (touch ? ov < 0 : ov <= 0) return 0;
        if (ov < best) { best = ov; double sg = d < 0 ? -1 : 1; nx = lx * sg; ny = ly * sg; }
    }
    out[0] = nx; out[1] = ny; out[2] = best; return 1;
}
static int pushable(Obj *o) { return !o->anchored && !animOf(o->id) && !(o->kind == OK_NONE && o->vel[0] == 0 && o->vel[1] == 0 && !o->colGrav); }
typedef struct { Obj *o; double x0, x1; int d; } CB;
static int cbcmp(const void *a, const void *b) { double x = ((const CB*)a)->x0, y = ((const CB*)b)->x0; return x < y ? -1 : x > y; }
/* 카메라에서 나온 바이트 (너비 · 높이 (4바이트 big-endian) · RGBA) 인지 */
int data_frame(const unsigned char *b, size_t n, int *w, int *h) {
    if (n < 8) return 0;
    unsigned W = (unsigned)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3], H = (unsigned)b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7];
    if (!W || !H || W > 8192 || H > 8192 || n != 8 + (size_t)W * H * 4) return 0;
    *w = (int)W; *h = (int)H; return 1;
}
/* dem.stopRecord: 그 귀속의 영상을 입은 오브젝트 → 마지막 그림 (그림이 없으면 텍스쳐를 벗음) */
void gfx_cam_unbind(int bind, const unsigned char *b, size_t n) {
    int w, h, fr = data_frame(b, n, &w, &h); Img *im = NULL;
    for (int i = 0; i < NORD; i++) {
        Obj *o = ORD[i];
        if (!o || o->dead || !o->hasTex || o->tex.ref != rd_cam_img() || o->camBind != bind) continue;
        if (fr && !im) { unsigned char *px = jb_alloc((size_t)w * h * 4); memcpy(px, b + 8, (size_t)w * h * 4); im = rd_image_rgba(px, w, h); }
        if (im) { static int seq; o->tex.ref = im; o->tex.path = jb_in(jb_fmt("@data#f%d", ++seq)); o->tex.x1 = 0; o->tex.y1 = 0; o->tex.x2 = w; o->tex.y2 = h; }
        else o->hasTex = 0;
        o->camBind = 0;
    }
    redraw();
}
static void collide(Obj **cs, int n) {
    if (n < 2) return;
    CB *L = jb_alloc(sizeof(CB) * n);
    for (int i = 0; i < n; i++) {
        Obj *o = cs[i]; double cx, ex;
        if (o->angle) { OBB B = obb(o, o->colOff, o->colSize); cx = B.x; ex = B.hx * fabs(B.c) + B.hy * fabs(B.s); }
        else { cx = o->pos[0] + o->colOff[0]; ex = fabs(o->colSize[0]) / 2; }
        L[i].o = o; L[i].x0 = cx - ex; L[i].x1 = cx + ex; L[i].d = pushable(o) || (!o->anchored && animOf(o->id));
    }
    /* 같은 x 면 원래 순서 (안정 정렬) */
    for (int i = 1; i < n; i++) { CB x = L[i]; int j = i - 1; while (j >= 0 && L[j].x0 > x.x0) { L[j + 1] = L[j]; j--; } L[j + 1] = x; }
    for (int i = 0; i < n; i++) {
        CB *A = &L[i]; Obj *a = A->o;
        for (int k = i + 1; k < n; k++) {
            CB *B = &L[k]; if (B->x0 > A->x1) break;
            if (!A->d && !B->d) continue;
            Obj *b = B->o;
            if ((a->parent || b->parent) && (isAncestor(a->id, b->id) || isAncestor(b->id, a->id))) continue;
            IdSet *lk = setOf(&links, a->id, 0); if (lk && is_has(lk, b->id)) continue;
            int da = A->d, db = B->d; double nx, ny, pen;
            if (a->angle || b->angle) {
                OBB oa = obb(a, a->colOff, a->colSize), ob = obb(b, b->colOff, b->colSize); double h[3];
                if (!satHit(&oa, &ob, 0, h)) continue; nx = h[0]; ny = h[1]; pen = h[2];
            } else {
                double ax = a->pos[0] + a->colOff[0], ay = a->pos[1] + a->colOff[1], bx = b->pos[0] + b->colOff[0], by = b->pos[1] + b->colOff[1];
                double dx = ax - bx, dy = ay - by;
                double px = (fabs(a->colSize[0]) + fabs(b->colSize[0])) / 2 - fabs(dx); if (px <= 0) continue;
                double py = (fabs(a->colSize[1]) + fabs(b->colSize[1])) / 2 - fabs(dy); if (py <= 0) continue;
                nx = px < py ? (dx < 0 ? -1 : 1) : 0; ny = px < py ? 0 : (dy < 0 ? -1 : 1); pen = px < py ? px : py;
            }
            a->colN = b->colN = physN; a->colWith = b->id; b->colWith = a->id;
            double e = fmax(a->colE, b->colE);
            double wa = da && db ? 0.5 : da ? 1 : 0, wb = da && db ? 0.5 : db ? 1 : 0;
            if (wa) movePos(a, a->pos[0] + nx * pen * wa, a->pos[1] + ny * pen * wa);
            if (wb) movePos(b, b->pos[0] - nx * pen * wb, b->pos[1] - ny * pen * wb);
            if (wa) animBlock(a); if (wb) animBlock(b);
            double *va = a->vel, *vb = b->vel;
            double rv = (va[0] - (db ? vb[0] : 0)) * nx + (va[1] - (db ? vb[1] : 0)) * ny;
            if (da && !db) { double vn = va[0] * nx + va[1] * ny; if (vn < 0) { double nv = -vn * e; if (nv < 40) nv = 0; va[0] += (nv - vn) * nx; va[1] += (nv - vn) * ny; } }
            else if (!da && db) { double vn = -(vb[0] * nx + vb[1] * ny); if (vn < 0) { double nv = -vn * e; if (nv < 40) nv = 0; vb[0] -= (nv - vn) * nx; vb[1] -= (nv - vn) * ny; } }
            else if (rv < 0) { double jj = -(1 + e) * rv / 2; va[0] += jj * nx; va[1] += jj * ny; vb[0] -= jj * nx; vb[1] -= jj * ny; }
        }
    }
    free(L);
}
static int boxOf(Obj *o, double *b) {
    if (o->hasCol) { b[0] = o->pos[0] + o->colOff[0]; b[1] = o->pos[1] + o->colOff[1]; b[2] = fabs(o->colSize[0]) / 2; b[3] = fabs(o->colSize[1]) / 2; return 1; }
    if (o->kind == OK_NONE) return 0;
    if (o->kind == OK_TEXT) { fitText(o); b[0] = o->pos[0] + nudX(o); b[1] = o->pos[1] + nudY(o); b[2] = fabs(o->scale[0]) / 2; b[3] = textH(o) / 2; return 1; }
    b[0] = o->pos[0] + nudX(o); b[1] = o->pos[1] + nudY(o); b[2] = fabs(o->scale[0]) / 2; b[3] = fabs(o->scale[1]) / 2; return 1;
}
static int trigTouch(Obj *t, Obj *o, double tx, double ty, double tw, double th, OBB *TB, int *hasTB) {
    double b[4]; if (!boxOf(o, b)) return 0;
    if (!t->angle && !o->angle) return fabs(b[0] - tx) <= b[2] + tw && fabs(b[1] - ty) <= b[3] + th;
    if (!*hasTB) { *TB = obb(t, t->trigOff, t->trigSize); *hasTB = 1; }
    OBB ob; if (o->hasCol) ob = obb(o, o->colOff, o->colSize);
    else { double r = o->angle * D2R; ob.x = b[0]; ob.y = b[1]; ob.hx = b[2]; ob.hy = b[3]; ob.c = cos(r); ob.s = sin(r); }
    double h[3]; return satHit(TB, &ob, 1, h);
}
static void checkTriggers(void) {
    if (!trigOn) return;
    FOR_OBJ(t) {
        if (!t->hasTrig || !live(t) || t->ui) continue;
        double tx = t->pos[0] + t->trigOff[0], ty = t->pos[1] + t->trigOff[1], tw = fabs(t->trigSize[0]) / 2, th = fabs(t->trigSize[1]) / 2;
        OBB TB; int hasTB = 0; const char *hit = NULL;
        if (t->trigId && *t->trigId && obj_has(t->trigId)) { Obj *o = obj_get(t->trigId); if (live(o) && trigTouch(t, o, tx, ty, tw, th, &TB, &hasTB)) hit = t->trigId; }
        if (!hit) for (int j = 0; j < NORD && !hit; j++) {
            Obj *o = ORD[j]; if (!o) continue;
            if (o == t || o->ui || (o->hasTrig && !o->hasCol && o->kind == OK_NONE) || !live(o) || isAncestor(o->id, t->id) || isAncestor(t->id, o->id)) continue;
            if (trigTouch(t, o, tx, ty, tw, th, &TB, &hasTB)) hit = o->id;
        }
        if (hit) { t->isTrig = 1; t->trigId = hit; }
        else if (!t->trigHold) { t->isTrig = 0; t->trigId = ""; }
    }
}
static Obj *trigOf(Obj *o, Node *n) { if (!o->hasTrig) RT(n, "'%s' 은(는) 트리거가 아닙니다 (col.setTrigger 로 만든 오브젝트만)", o->id); checkTriggers(); return o; }
static int isActive(Obj *o) { return !o->anchored && (o->vel[0] != 0 || o->vel[1] != 0 || (o->hasCol && o->colGrav)); }
static int physOn; static double physLast, tickAcc;
static void physStart(void) { if (physOn) return; physOn = 1; physLast = perf_now(); tickAcc = 0; }
static void physKick(Obj *o) { if (isActive(o)) physStart(); }
static void spatial(void);
static double spatT;
static int camFollow(void) {
    int any = 0;
    for (int i = 0; i < nCamOrder; i++) {
        Obj *k = obj_get(camOrder[i]); if (!k || !k->follow) continue;
        any = 1; Obj *t = obj_get(k->follow);
        if (t && (k->pos[0] != t->pos[0] || k->pos[1] != t->pos[1])) movePos(k, t->pos[0], t->pos[1]);
    }
    return any;
}
static int physTick(double dt) {
    physN++;
    double now = perf_now(); int any = nTw > 0 || nAnimObjs > 0, active = nAn > 0 || nTw > 0 || nAnimObjs > 0;
    if (nAnimObjs) animeStep(now);
    if (nTw) { tweenStep(now); for (int i = 0; i < NORD; i++) if (ORD[i] && ORD[i]->kind == OK_TEXT) fitText(ORD[i]); }
    if (nAn) {
        int w_ = 0;
        for (int i = 0; i < nAn; i++) {
            An *a = anims[i]; if (!a->active) continue;
            Obj *o = obj_get(a->id); if (!o) { animEnd(a); continue; }
            if (nPop && !live(o)) { anims[w_++] = a; continue; }
            double k = (now - a->start) / a->ms; if (k > 1) k = 1;
            double q = a->ease > 0 && k < 1 ? jb_ease(a->ease, k) : k;
            movePos(o, a->from[0] + (a->to[0] - a->from[0]) * q, a->from[1] + (a->to[1] - a->from[1]) * q); any = 1;
            a->started = 1;
            if (k >= 1) { animEnd(a); continue; }
            anims[w_++] = a;
        }
        nAn = w_;
    }
    static Obj **cs; static int capcs; int ncs = 0; double minH = INFINITY, maxV = 0;
    if (capcs < NORD + 1) { capcs = NORD + 64; cs = jb_realloc(cs, sizeof(Obj*) * capcs); }
    if (colOn) FOR_OBJ(o) if (o->hasCol && live(o) && !o->ui) {
        cs[ncs++] = o;
        double a = fabs(o->colSize[0]) / 2, b = fabs(o->colSize[1]) / 2;
        if (!a) a = INFINITY; if (!b) b = INFINITY;
        minH = fmin(minH, fmin(a, b));
    }
    FOR_OBJ(o) if (!o->anchored && (o->vel[0] != 0 || o->vel[1] != 0 || (o->hasCol && o->colGrav))) {
        double g = o->hasCol && o->colGrav ? fabs(GRAVITY * gravScale) * dt : 0;
        maxV = fmax(maxV, fabs(o->vel[0]) + fabs(o->vel[1]) + g);
    }
    int steps = 1;
    if (ncs && minH < INFINITY) { steps = (int)ceil(maxV * dt / fmax(1, minH)); if (steps < 1) steps = 1; if (steps > 16) steps = 16; }
    double h = dt / steps;
    for (int st = 0; st < steps; st++) {
        int moved = 0;
        FOR_OBJ(o) {
            if (animOf(o->id) || !isActive(o) || !live(o)) continue;
            active = 1;
            double *v = o->vel;
            if (o->hasCol && o->colGrav) v[1] -= GRAVITY * gravScale * h;
            if (v[0] == 0 && v[1] == 0) continue;
            movePos(o, o->pos[0] + v[0] * h, o->pos[1] + v[1] * h); moved = 1;
        }
        if (ncs > 1 && (moved || st == 0)) collide(cs, ncs);
        any = any || moved;
    }
    if (camOn && camFollow()) { any = 1; active = 1; }
    if (any || nAn) checkTriggers();
    if (now - spatT >= 16) { spatT = now; spatial(); }
    return active;
}
#define TICK_MS 4.0
void gfx_tick_loop(double dtms) {
    (void)dtms;
    if (needRedraw) {
        needRedraw = 0; checkTriggers();
        for (int i = 0; i < NORD; i++) if (ORD[i] && ORD[i]->kind == OK_TEXT) fitText(ORD[i]);
    }
    if (perf_now() - spatT >= 33) { spatT = perf_now(); spatial(); }
    if (!physOn) return;
    double now = perf_now(), el = now - physLast; physLast = now;
    if (el > 250) el = 250;
    tickAcc += el;
    int n = 0;
    while (tickAcc >= TICK_MS) {
        tickAcc -= TICK_MS;
        if (!physTick(TICK_MS / 1000)) { tickAcc = 0; physOn = 0; return; }
        if (++n * TICK_MS >= 250) { tickAcc = 0; break; }
    }
}
int gfx_alive_ticker(void) { return physOn; }

/* ---------- 같은 id 로 다시 만들 때 ---------- */
static Obj *keepRel(Obj *old, Obj *o) {
    if (old) {
        o->parent = old->parent; o->hasCol = old->hasCol; memcpy(o->colOff, old->colOff, sizeof o->colOff); memcpy(o->colSize, old->colSize, sizeof o->colSize); o->colE = old->colE; o->colGrav = old->colGrav;
        o->anchored = old->anchored; o->hasTrig = old->hasTrig; memcpy(o->trigOff, old->trigOff, sizeof o->trigOff); memcpy(o->trigSize, old->trigSize, sizeof o->trigSize); o->trigHold = old->trigHold;
        o->isTrig = old->isTrig; o->trigId = old->trigId; o->insts = old->insts; o->ninsts = old->ninsts; o->layer = old->layer; o->hasLayer = old->hasLayer;
        o->tags = old->tags; o->ntags = old->ntags; o->alpha = old->alpha; o->hasOv = old->hasOv; memcpy(o->ovColor, old->ovColor, 8); o->ovA = old->ovA;
        o->hasGlow = old->hasGlow; memcpy(o->glowColor, old->glowColor, 8); o->glowS = old->glowS; o->off = old->off;
        o->scenes = old->scenes; o->nscenes = old->nscenes; o->hasScenes = old->hasScenes; o->hasTex = old->hasTex; o->tex = old->tex; o->angle = old->angle;
        o->colN = old->colN; o->colWith = old->colWith; o->an = old->an; if (o->an) { animObjDel(old); animObjAdd(o); } old->an = NULL;
        o->ui = old->ui; o->hasNud = old->hasNud; o->nud[0] = old->nud[0]; o->nud[1] = old->nud[1]; o->btn = old->btn; o->btnSeen = old->btnSeen; o->relSeen = old->relSeen; o->bst = old->bst;
        o->cam = old->cam; o->zoom = old->zoom; o->follow = old->follow; o->spk = old->spk;
        shiftKids(o->id, o->pos[0] - old->pos[0], o->pos[1] - old->pos[1], NULL);
    }
    if (o->hasCol) physKick(o);
    return o;
}
static void velNew(Obj *o, Obj *old) { if (old) { o->vel[0] = old->vel[0]; o->vel[1] = old->vel[1]; } }
static void camName(const char *id, Node *n) {
    if (!strncmp(id, "cam.", 4)) RT(n, "'cam.' 으로 시작하는 이름은 카메라 전용입니다 ('%s') — 카메라는 seo.setCam(\"이름\"); 으로 만드세요", id);
    if (!strncmp(id, "spk.", 4)) RT(n, "'spk.' 으로 시작하는 이름은 스피커 전용입니다 ('%s') — 스피커는 sou.setSpeaker(\"이름\"); 으로 만드세요", id);
}
static void setPos(Obj *o, const double *m) { if (o->pos[0] == m[0] && o->pos[1] == m[1]) return; movePos(o, m[0], m[1]); redraw(); }
static void setScale(Obj *o, const double *m) { if (o->scale[0] == m[0] && o->scale[1] == m[1]) return; o->scale[0] = m[0]; o->scale[1] = m[1]; redraw(); }

/* ---------- 귀속 (BelongObj) · 장면 코드 ---------- */
static void adopt(Obj *o) {
    const char *s = C.selfNow;
    if (!s || s == o->id || o->parent || !obj_has(s)) return;
    o->parent = s; is_add(setOf(&kids, s, 1), o->id);
}
static Slot *instSlot(Obj *o, const char *name) {
    for (int i = 0; i < o->ninsts; i++) {
        Inst *in = o->insts[i];
        for (int q = 0; q < in->env->n; q++) if (in->env->s[q]->name == name) return in->env->s[q];
        for (int r = 0; r < in->env->nrun; r++) { Env *re = in->env->runEnvs[r]; for (int q = 0; q < re->n; q++) if (re->s[q]->name == name) return re->s[q]; }
    }
    return NULL;
}
static Inst *initInst(Obj *o, const char *path) {
    for (int i = 0; i < o->ninsts; i++) if (!strcmp(o->insts[i]->path, path)) return NULL;
    Inst *in = jb_calloc(sizeof(Inst)); in->path = path; in->tk = jb_calloc(sizeof(Tk)); in->env = env_new(NULL);
    in->env->hasSelf = 1; in->env->self = o->id; in->env->tk = in->tk;
    o->insts = jb_realloc(o->insts, sizeof(Inst*) * (o->ninsts + 1)); o->insts[o->ninsts++] = in;
    rt_sys("⊕ %s → obj.%s", path, o->id);
    const char *prev = C.selfNow;
    Module *m = task_by_path(path);
    for (int i = 0; i < m->ast.n; i++) if (m->ast.v[i]->k == N_VAR) { C.selfNow = o->id; exec(m->ast.v[i], in->env); }
    C.selfNow = prev;
    return in;
}
typedef struct { NodeList *body; Env *env; } IR;
static void instRun(void *a) { IR r = *(IR *)a; free(a); C.selfNow = env_self(r.env); rt_run_body_thread(r.body, r.env); }
static void startInst(Obj *o, Inst *in) {
    (void)o;
    Module *m = task_by_path(in->path);
    for (int i = 0; i < m->ast.n; i++) {
        Node *r = m->ast.v[i]; if (r->k != N_RUN) continue;
        Env *renv = env_new(in->env); env_add_run(in->env, renv); renv->dc = jb_calloc(sizeof(Dc));
        IR *a = jb_alloc(sizeof *a); a->body = &r->body; a->env = renv;
        const char *prev = C.selfNow;
        rt_spawn_now(instRun, a);
        C.selfNow = prev;
    }
}
static int spawnDepth;
static void born(Obj *o, Node *n) {
    if (sceneOn && !o->hasScenes) scenesSet1(o, curScene);
    adopt(o);
    Belong *b = NULL; for (int i = 0; i < P.nbelongs; i++) if (!strcmp(P.belongs[i].id, o->id)) b = &P.belongs[i];
    if (!b) return;
    if (spawnDepth >= 64) RT(n, "귀속된 파일이 오브젝트를 너무 깊게 연달아 만들고 있습니다");
    spawnDepth++;
    Inst **made = jb_alloc(sizeof(Inst*) * (b->n + 1)); int nm = 0;
    for (int i = 0; i < b->n; i++) { Inst *in = initInst(o, b->files[i]); if (in) made[nm++] = in; }
    for (int i = 0; i < nm; i++) if (!made[i]->tk->dead) startInst(o, made[i]);
    free(made);
    spawnDepth--;
}
static void runSceneCode(const char *name) {
    if (!name || !*name) return;
    Belong *b = NULL; for (int i = 0; i < P.nsceneBelongs; i++) if (!strcmp(P.sceneBelongs[i].id, name)) b = &P.sceneBelongs[i];
    if (!b) return;
    for (int i = 0; i < b->n; i++) {
        Module *m = task_by_path(b->files[i]);
        Tk *tk = jb_calloc(sizeof(Tk)); Env *env = env_new(NULL); env->tk = tk;
        sceneInsts = jb_realloc(sceneInsts, sizeof(Tk*) * (nSceneInsts + 1)); sceneInsts[nSceneInsts++] = tk;
        rt_sys("⊕ %s → 장면 %s", b->files[i], name);
        const char *prev = C.selfNow;
        for (int q = 0; q < m->ast.n; q++) if (m->ast.v[q]->k == N_VAR) exec(m->ast.v[q], env);
        for (int q = 0; q < m->ast.n; q++) {
            Node *r = m->ast.v[q]; if (r->k != N_RUN) continue;
            Env *renv = env_new(env);
            IR *a = jb_alloc(sizeof *a); a->body = &r->body; a->env = renv;
            rt_spawn_now(instRun, a);
        }
        C.selfNow = prev;
        if (tk->dead) break;
    }
}
static void leaveScene(void) { for (int i = 0; i < nSceneInsts; i++) sceneInsts[i]->dead = 1; nSceneInsts = 0; }
int rt_frozen_env(Tk *tk, Env *env) {
    if (!nPop) return 0;
    if (tk && tk->paused) return 1;
    const char *s = env ? env_self(env) : NULL;
    if (s) { Obj *o = obj_get(s); if (o && !live(o)) return 1; }
    return 0;
}
static int liveOrGone(void *p) { Obj *o = obj_get((const char *)p); return !o || live(o); }
int gfx_seq_gate(Env *env) {
    if (!(offOn || sceneOn)) return S_OK;
    const char *s = env_self(env); if (!s) return S_OK;
    Obj *so = obj_get(s);
    if (so && !live(so)) {
        while (!liveOrGone((void *)s)) { if (env->tk && env->tk->dead) return S_DEAD; rt_wait_frame(); }
        C.sliceT = perf_now();
    }
    return S_OK;
}
Slot *gfx_pslot(const char *name, Env *env, Node *n) {
    const char *self = env_self(env);
    if (!self) RT(n, "부모 변수 '%s' 은(는) 오브젝트에 귀속되어 실행될 때만 쓸 수 있습니다", name);
    Obj *me = obj_get(self);
    int k = 0;
    for (const char *pid = me ? me->parent : NULL; pid && k < 256; k++) {
        Obj *po = obj_get(pid); if (!po) break;
        Slot *sl = instSlot(po, name); if (sl) return sl;
        pid = po->parent;
    }
    RT(n, "'%s' 을(를) 찾을 수 없습니다 — 부모 오브젝트들에 귀속된 파일에 이 변수가 없습니다 (지금 부모: %s)", name, me && me->parent ? me->parent : "없음");
    return NULL;
}
Slot *gfx_ovslot(Node *e, Env *env) {
    (void)env;
    Obj *o = objOf(e->id, e);
    Slot *sl = instSlot(o, e->name); if (sl) return sl;
    RT(e, "오브젝트 '%s' 에 변수 '%s' 이(가) 아직 없습니다 (귀속된 파일이 실행되지 않았거나, run{ } 안의 그 var 줄까지 아직 오지 않았습니다)", o->id, e->name);
    return NULL;
}
int gfx_belong(Node *c, Env *env) {
    if (strcmp(c->name, "BelongObj") || env_self(env)) return S_OK;
    if (!objOn) RT(c, "%s", NOOBJ);
    Obj *o = objOf(c->args.v[0]->str, c);
    Env *root = env; while (root->p) root = root->p;
    root->hasSelf = 1; root->self = o->id; C.selfNow = o->id;
    int has = 0; for (int i = 0; i < o->ninsts; i++) if (o->insts[i]->env == root) has = 1;
    if (!has) {
        Inst *in = jb_calloc(sizeof(Inst)); in->path = c->file; in->env = root; in->tk = jb_calloc(sizeof(Tk));
        o->insts = jb_realloc(o->insts, sizeof(Inst*) * (o->ninsts + 1)); o->insts[o->ninsts++] = in;
    }
    rt_sys("⊕ %s → obj.%s (실행 중 귀속)", c->file, o->id);
    return S_OK;
}

/* ---------- 팝업 장면 ---------- */
static void camSave(Pop *f) { Obj *o = obj_get(ID_CAM_DEFAULT); f->hasCam = o != NULL; if (o) { f->camPos[0] = o->pos[0]; f->camPos[1] = o->pos[1]; f->camZoom = o->zoom; f->camFollow = o->follow; } }
static void camLoad(Pop *f) { Obj *o = obj_get(ID_CAM_DEFAULT); if (!o || !f->hasCam) return; movePos(o, f->camPos[0], f->camPos[1]); o->zoom = f->camZoom; o->follow = f->camFollow; }
static void camDefault(void) {
    if (!camOn) return;
    Obj *o = obj_get(ID_CAM_DEFAULT);
    if (!o) { o = obj_new(ID_CAM_DEFAULT, OK_NONE); o->cam = 1; o->zoom = 1; obj_set(o); camOrder = jb_realloc(camOrder, sizeof(char*) * (nCamOrder + 1)); memmove(camOrder + 1, camOrder, sizeof(char*) * nCamOrder); camOrder[0] = o->id; nCamOrder++; }
    An *a = animOf(o->id); if (a) animEnd(a);
    o->zoom = 1; o->follow = NULL; scenesSet1(o, -1); movePos(o, 0, 0);
}
static Pop *popOpen(int k, const char *nm) {
    Pop *f = jb_calloc(sizeof(Pop)); f->scene = curScene; f->insts = sceneInsts; f->n = nSceneInsts; f->at = perf_now(); camSave(f);
    for (int i = 0; i < f->n; i++) f->insts[i]->paused = 1;
    popStack = jb_realloc(popStack, sizeof(Pop*) * (nPop + 1)); popStack[nPop++] = f;
    sceneInsts = NULL; nSceneInsts = 0; curScene = k; camDefault();
    rt_sys("⧉ 팝업 장면 %s", nm);
    redraw(); physStart(); runSceneCode(nm);
    return f;
}
static void popClose(Pop *f, int toScene) {
    double dur = perf_now() - f->at;
    leaveScene();
    for (int i = 0; i < nAn; i++) if (anims[i]->active) { Obj *o = obj_get(anims[i]->id); if (o && !live(o)) anims[i]->start += dur; }
    for (int i = 0; i < nTw; i++) if (tweens[i]->active && !live(tweens[i]->o)) tweens[i]->start += dur;
    for (int i = 0; i < nAnimObjs; i++) if (animObjs[i]->an && !live(animObjs[i])) animObjs[i]->an->start += dur;
    if (toScene) { curScene = f->scene; sceneInsts = f->insts; nSceneInsts = f->n; camLoad(f); }
    for (int i = 0; i < f->n; i++) { f->insts[i]->paused = 0; if (!toScene) f->insts[i]->dead = 1; }
    f->closed = 1;
}
static void popAll(void) { while (nPop) popClose(popStack[--nPop], 0); }

/* ---------- 키 · 버튼 · 마우스 ---------- */
static IdSet vHeld; static HM vTap;   /* 키 → 프레임 + 1 */
void in_virtual_key(const char *k, int mode) {
    const char *ik = jb_in(k);
    if (mode == 0) hm_put(&vTap, ik, (void *)(intptr_t)(RT_FRAME + 1));
    else if (mode == 1) is_add(&vHeld, ik);
    else { is_del(&vHeld, ik); hm_put(&vTap, ik, NULL); }
}
static int inObj(Obj *o, double x, double y);
int key_on(const char *n0) {
    char kb[64]; key_canon(n0, kb, sizeof kb); const char *n = jb_in(kb);
    intptr_t t = (intptr_t)hm_get(&vTap, n);
    if (is_has(&vHeld, n) || (t && RT_FRAME <= t - 1 + 1)) return 1;
    if (in_key(n)) return 1;
    IdSet *bs = setOf(&keyBinds, n, 0);
    if (bs) for (int i = 0; i < bs->n; i++) {
        Obj *o = obj_get(bs->v[i]); if (!o || !live(o)) continue;
        if (o->kind == OK_TEXT) fitText(o);
        double (*pp)[3]; int np = in_pointers(&pp);
        for (int q = 0; q < np; q++) { double wx, wy; toW(pp[q][1], pp[q][2], &wx, &wy); if (inObj(o, wx, wy)) return 1; }
        Click *ck; int nc = in_clicks(&ck);
        for (int q = 0; q < nc; q++) { if (RT_FRAME > ck[q].f + 1) continue; double wx, wy; toW(ck[q].dx, ck[q].dy, &wx, &wy); if (inObj(o, wx, wy)) return 1; }
    }
    return 0;
}
static int inObj(Obj *o, double x, double y) {
    if (o->ui) { double cx, cy, cz; if (camNow(&cx, &cy, &cz)) { x = (x - cx) * cz; y = (y - cy) * cz; } }
    double dx = x - o->pos[0], dy = y - o->pos[1], hw = fabs(o->scale[0]) / 2, hh = (o->kind == OK_TEXT ? textH(o) : fabs(o->scale[1])) / 2;
    dx -= nudX(o); dy -= nudY(o);
    if (o->angle) { double r = o->angle * D2R, c = cos(r), s = sin(r), rx = dx * c + dy * s; dy = -dx * s + dy * c; dx = rx; }
    if (o->kind == OK_CIRCLE) return hw > 0 && hh > 0 && (dx * dx) / (hw * hw) + (dy * dy) / (hh * hh) <= 1;
    if (o->kind == OK_POLY) {
        double fx = o->scale[0] / o->pbase[0], fy = o->scale[1] / o->pbase[1]; int inn = 0;
        for (int i = 0, j = o->npts - 1; i < o->npts; j = i++) {
            double ax = o->pts[i][0] * fx, ay = o->pts[i][1] * fy, bx = o->pts[j][0] * fx, by = o->pts[j][1] * fy;
            if ((ay > dy) != (by > dy) && dx < (bx - ax) * (dy - ay) / (by - ay) + ax) inn = !inn;
        }
        return inn;
    }
    return fabs(dx) <= hw && fabs(dy) <= hh;
}
static void btnTrack(Obj *o) {
    if (!o->bst) o->bst = jb_calloc(sizeof(BtnSt));
    BtnSt *st = o->bst; int lv = live(o);
    PtrEv *L; int nl = in_ptrlog(&L);
    int i0 = nl; while (i0 > 0 && L[i0 - 1].s > st->s) i0--;
    for (int i = i0; i < nl; i++) {
        PtrEv *e = &L[i]; st->s = e->s;
        if (e->t <= o->relSeen) continue;
        int hi = -1; for (int q = 0; q < st->nheld; q++) if (st->held[q].id == e->id) hi = q;
        int was = hi >= 0 ? st->held[hi].on : 0, now = 0;
        if (lv) { double wx, wy; toW(e->x, e->y, &wx, &wy); now = inObj(o, wx, wy); }
        if (e->k == 'd' || e->k == 'm') {
            if (e->k == 'm' && was && !now && st->nrel < 64) st->rel[st->nrel++] = e->t;
            if (hi < 0 && st->nheld < 16) { hi = st->nheld++; st->held[hi].id = e->id; }
            if (hi >= 0) st->held[hi].on = now;
        } else {
            if (was && st->nrel < 64) st->rel[st->nrel++] = e->t;
            if (hi >= 0) { st->held[hi] = st->held[--st->nheld]; }
        }
    }
    double (*pp)[3]; int np = in_pointers(&pp);
    for (int q = 0; q < np; q++) {
        int id = (int)pp[q][0], now = 0;
        if (lv) { double wx, wy; toW(pp[q][1], pp[q][2], &wx, &wy); now = inObj(o, wx, wy); }
        int hi = -1; for (int z = 0; z < st->nheld; z++) if (st->held[z].id == id) hi = z;
        if (hi >= 0 && st->held[hi].on && !now && st->nrel < 64) st->rel[st->nrel++] = perf_now();
        if (hi < 0 && st->nheld < 16) { hi = st->nheld++; st->held[hi].id = id; }
        if (hi >= 0) st->held[hi].on = now;
    }
    for (int z = 0; z < st->nheld; z++) { int keep = 0; for (int q = 0; q < np; q++) if ((int)pp[q][0] == st->held[z].id) keep = 1; if (!keep) { st->held[z--] = st->held[--st->nheld]; } }
}
static Obj *selfObj(Env *env, Node *n) {
    const char *s = env_self(env);
    if (!s) RT(n, "id 없는 obj.속성 은 오브젝트에 귀속되어 실행될 때만 쓸 수 있습니다 — 아직 귀속되지 않았습니다 (dis.BelongObj(\"id\"); 줄을 먼저 실행하세요)");
    return objOf(s, n);
}
static Obj *objT(Node *tg, Env *env) { return tg->id ? objOf(tg->id, tg) : selfObj(env, tg); }
static const char *textOf(Obj *o, Node *n) { if (o->kind != OK_TEXT) RT(n, "'%s' 은(는) 글자 오브젝트가 아니라 Text 가 없습니다 (dis.setText 로 만든 오브젝트만)", o->id); return o->text; }
static Value matv(const double *d) { Value m = v_mat(1, 2); m.u.m->d[0] = d[0]; m.u.m->d[1] = d[1]; return m; }
static double *propMat(Obj *o, int key) { return key == K_POS ? o->pos : key == K_SCALE ? o->scale : o->vel; }
double *gfx_objmat(Node *e, Env *env, int *r, int *c) {
    Obj *o = objT(e, env); if (o->kind == OK_TEXT) fitText(o);
    *r = 1; *c = 2;
    if (e->key != K_POS && e->key != K_SCALE && e->key != K_VEL) RT(e, "[행, 열] 은 행렬에만 쓸 수 있습니다");
    return propMat(o, e->key);
}
void gfx_eassign_obj(Node *target, Env *env, int k, double v, double old) {
    Obj *o = objT(target, env);
    if (target->key == K_VEL) physKick(o);
    else { if (target->key == K_POS) shiftKids(o->id, k == 0 ? v - old : 0, k == 1 ? v - old : 0, NULL); redraw(); }
}
Value gfx_value(Node *e, Env *env) {
    switch (e->k) {
    case N_OBJPROP: {
        Obj *o = objT(e, env); if (o->kind == OK_TEXT) fitText(o);
        switch (e->key) {
        case K_ISTRIG: return v_num(trigOf(o, e)->isTrig);
        case K_TRIGID: return v_str(trigOf(o, e)->trigId ? o->trigId : "");
        case K_ANIFR: return v_num(o->an ? o->an->k + 1 : -1);
        case K_ISCOL: return v_num(o->hasCol && o->colN == physN ? 1 : 0);
        case K_COLID: return v_str(o->hasCol && o->colWith ? o->colWith : "");
        case K_COLOR: return v_str(o->color);
        case K_TEXT: return v_str(textOf(o, e));
        default: return matv(propMat(o, e->key));
        }
    }
    case N_OBJKEEP: {
        Obj *o = C.curObj ? C.curObj : env_self(env) ? selfObj(env, e) : NULL;
        if (!o) {
            Module *m = task_by_path(e->file);
            if (m && m->bound) RT(e, "obj.%s 처럼 id 없이 쓰려면 이 실행이 오브젝트에 귀속되어 있어야 합니다 — 아직 귀속되지 않았습니다 (dis.BelongObj(\"id\"); 줄을 먼저 실행하세요)", e->name);
            RT(e, "obj.%s 은(는) 이미 있는 오브젝트를 바꿀 때만 쓸 수 있습니다", e->name);
        }
        if (o->kind == OK_TEXT) fitText(o);
        return v_num(propMat(o, e->keepK)[e->keepI]);
    }
    case N_CANOBJ: {
        Obj *o = e->id ? objOf(e->id, e) : selfObj(env, e);
        if (e->btn) {
            if (!o->btn) RT(e, "'%s' 은(는) 버튼이 아닙니다 (먼저 can.setasButton(\"%s\");)", o->id, o->id);
            if (!rd_is_open() || !live(o)) return v_num(0);
            if (o->kind == OK_TEXT) fitText(o);
            double now = perf_now();
            if (e->btn == 'p') {
                btnTrack(o); for (int q = 0; q < o->bst->nheld; q++) if (o->bst->held[q].on) return v_num(1);
                Click *ck; int nc = in_clicks(&ck);
                for (int q = 0; q < nc; q++) if (RT_FRAME <= ck[q].f + 1 && (ck[q].df < 0 || ck[q].f - ck[q].df <= 1)) { double wx, wy; toW(ck[q].dx, ck[q].dy, &wx, &wy); if (inObj(o, wx, wy)) return v_num(1); }
                return v_num(0);
            }
            if (e->btn == 'r') {
                btnTrack(o); BtnSt *b = o->bst;
                while (b->nrel && now - b->rel[0] > 500) { memmove(b->rel, b->rel + 1, sizeof(double) * --b->nrel); }
                if (b->nrel) { memmove(b->rel, b->rel + 1, sizeof(double) * --b->nrel); return v_num(1); }
                return v_num(0);
            }
            Click *ck; int nc = in_clicks(&ck);
            for (int q = 0; q < nc; q++) {
                if (ck[q].t <= o->btnSeen || now - ck[q].t > 500) continue;
                double dx, dy, ux, uy; toW(ck[q].dx, ck[q].dy, &dx, &dy); toW(ck[q].ux, ck[q].uy, &ux, &uy);
                if (inObj(o, dx, dy) && inObj(o, ux, uy)) { o->btnSeen = ck[q].t; return v_num(1); }
            }
            return v_num(0);
        }
        if (!in_mouse_in() || !live(o)) return v_num(0);
        if (o->kind == OK_TEXT) fitText(o);
        double mx, my, wx, wy; in_mouse(&mx, &my); toW(mx, my, &wx, &wy);
        return v_num(inObj(o, wx, wy));
    }
    case N_MEMBER:
        if (e->target == M_CAN) {
            if (!strcmp(e->name, "SceneName")) { if (!sceneOn) return v_str(""); return v_str(curScene < nSceneNames && sceneNames[curScene] ? sceneNames[curScene] : ""); }
            if (!strcmp(e->name, "ScreenClicked")) return v_num(in_clicked());
            double mx, my, wx, wy; in_mouse(&mx, &my); toW(mx, my, &wx, &wy);
            return v_num(!strcmp(e->name, "MouseX") ? wx : wy);
        }
        if (e->target == M_DIS) {
            if (!disOn) return v_num(0);
            double w, h; rd_size(&w, &h);
            if (!strcmp(e->name, "idWidthmode")) return v_num(w >= h ? 1 : 0);
            return v_num(!strcmp(e->name, "displayX") ? w : h);
        }
    }
    return v_num(0);
}
int gfx_oassign(Node *s, Env *env) {
    Node *tg = s->a;
    if (tg->k == N_OBJVAR) { Slot *sl = gfx_ovslot(tg, env); Value v = co(ev(s->expr, env), sl->ty); sl = gfx_ovslot(tg, env); v_free(&sl->v); sl->v = v; return S_OK; }
    Obj *o = objT(tg, env); Value v = ev(s->expr, env);
    if (tg->key == K_ISTRIG) { trigOf(o, s); o->isTrig = v.u.n != 0 ? 1 : 0; if (!o->isTrig) o->trigId = ""; return S_OK; }
    if (tg->key == K_COLOR) { char cl[8]; parseColor(v, s->expr, cl); v_free(&v); if (!strcmp(cl, o->color)) return S_OK; strcpy(o->color, cl); }
    else if (tg->key == K_TEXT) { textOf(o, s); char *t = v.k == V_STR ? v.u.s : fmt_any(v); if (!strcmp(o->text, t)) { if (t != v.u.s) free(t); v_free(&v); return S_OK; } free(o->text); o->text = jb_strdup(t); if (t != v.u.s) free(t); v_free(&v); }
    else if (tg->key == K_VEL) { o->vel[0] = v.u.m->d[0]; o->vel[1] = v.u.m->d[1]; v_free(&v); physKick(o); return S_OK; }
    else { double m[2] = {v.u.m->d[0], v.u.m->d[1]}; v_free(&v); if (tg->key == K_POS) setPos(o, m); else setScale(o, m); return S_OK; }
    redraw(); return S_OK;
}

/* ---------- 타일맵 (.tl) ---------- */
typedef struct { int w, h; double ts; int ntex; char **texData; int *texCol, *texClimb; char **texName; int nl; int *lvis; int **ltiles; int hasZero, zero[2]; Img **layerImg; } TL;
static HM tlCache, tlPlaced;   /* 경로 → TL · 타일맵 id → IdSet */
extern TL *tl_parse(const char *path, const char *text, char *err, size_t en);
extern Img *rd_tilemap_layer(TL *t, int li, const char *key);
static void tlSolid(TL *t, int **outR, int *nout) {
    int w = t->w, h = t->h; unsigned char *solid = jb_calloc(w * h), *climb = jb_calloc(w * h);
    for (int l = 0; l < t->nl; l++) { if (!t->lvis[l]) continue; for (int i = 0; i < w * h; i++) { int v = t->ltiles[l][i]; if (v < 0) continue; if (t->texClimb[v]) climb[i] = 1; else if (t->texCol[v]) solid[i] = 1; } }
    for (int i = 0; i < w * h; i++) if (climb[i]) solid[i] = 0;
    /* 줄마다 이어진 칸 → 아래로 같은 칸이 이어지면 합침 */
    typedef struct { int x, len, y, rows; int open; } R;
    R *open = NULL; int nopen = 0; int *out = NULL; int no = 0;
    for (int y = 0; y <= h; y++) {
        int *runs = NULL; int nr = 0;
        if (y < h) for (int x = 0; x < w;) { if (!solid[y * w + x]) { x++; continue; } int e = x; while (e < w && solid[y * w + e]) e++; runs = jb_realloc(runs, sizeof(int) * 2 * (nr + 1)); runs[nr * 2] = x; runs[nr * 2 + 1] = e - x; nr++; x = e; }
        for (int i = 0; i < nopen; i++) {
            if (!open[i].open) continue;
            int has = 0; for (int q = 0; q < nr; q++) if (runs[q * 2] == open[i].x && runs[q * 2 + 1] == open[i].len) has = 1;
            if (!has) { out = jb_realloc(out, sizeof(int) * 4 * (no + 1)); out[no * 4] = open[i].x; out[no * 4 + 1] = open[i].y; out[no * 4 + 2] = open[i].len; out[no * 4 + 3] = open[i].rows; no++; open[i].open = 0; }
        }
        for (int q = 0; q < nr; q++) {
            int f = -1; for (int i = 0; i < nopen; i++) if (open[i].open && open[i].x == runs[q * 2] && open[i].len == runs[q * 2 + 1]) f = i;
            if (f >= 0) open[f].rows++;
            else { open = jb_realloc(open, sizeof(R) * (nopen + 1)); open[nopen].x = runs[q * 2]; open[nopen].len = runs[q * 2 + 1]; open[nopen].y = y; open[nopen].rows = 1; open[nopen].open = 1; nopen++; }
        }
        free(runs);
    }
    free(open); free(solid); free(climb);
    *outR = out; *nout = no;
}
static int tilemapPlace(const char *id, const char *path, double x, double y, Node *n) {
    TL *t = hm_get(&tlCache, path);
    if (!t) { char err[512]; t = tl_parse(path, proj_text(path), err, sizeof err); if (!t) RT(n, "%s", err); hm_put(&tlCache, path, t); }
    double W = t->w * t->ts, H = t->h * t->ts;
    double cx = t->hasZero ? x - ((t->zero[0] + 0.5) * t->ts - W / 2) : x, cy = t->hasZero ? y - (H / 2 - (t->zero[1] + 0.5) * t->ts) : y;
    const char *iid = jb_in(id);
    IdSet *old = setOf(&tlPlaced, iid, 0);
    if (old) for (int i = 0; i < old->n; i++) { if (obj_has(old->v[i])) { obj_delete(old->v[i]); unlinkAll(old->v[i]); } }
    IdSet *made = jb_calloc(sizeof(IdSet)); int nLayer = 0, nCol = 0;
    for (int li = 0; li < t->nl; li++) {
        if (!t->lvis[li]) continue;
        Obj *o = obj_new(jb_in(jb_fmt("%s.layer%d", id, li)), OK_RECT);
        o->pos[0] = cx; o->pos[1] = cy; o->scale[0] = W; o->scale[1] = H; strcpy(o->color, "#ffffff"); o->alpha = 0; o->layer = -1000 + li; o->hasLayer = 1;
        obj_set(o); born(o, n); is_add(made, o->id); nLayer++;
    }
    int *rs, nr; tlSolid(t, &rs, &nr);
    for (int i = 0; i < nr; i++) {
        int rx = rs[i * 4], ry = rs[i * 4 + 1], cw = rs[i * 4 + 2], ch = rs[i * 4 + 3];
        Obj *o = obj_new(jb_in(jb_fmt("%s.col%d", id, made->n)), OK_NONE);
        o->scale[0] = o->colSize[0] = cw * t->ts; o->scale[1] = o->colSize[1] = ch * t->ts;
        o->pos[0] = cx - W / 2 + (rx + cw / 2.0) * t->ts; o->pos[1] = cy + H / 2 - (ry + ch / 2.0) * t->ts;
        o->hasCol = 1;
        obj_set(o); born(o, n); is_add(made, o->id); colOn = 1; nCol++;
    }
    free(rs);
    hm_put(&tlPlaced, iid, made); layerOn = 1;
    rt_sys("🗺 타일맵 '%s' (%d × %d 칸 · 타일 %g) — 레이어 %d장 · 콜라이더 %d개", id, t->w, t->h, t->ts, nLayer, nCol);
    if (colOn) physStart(); redraw();
    /* 레이어 이미지 (한 장씩 미리 그림) */
    for (int li = 0; li < t->nl; li++) {
        if (!t->lvis[li]) continue;
        Obj *o = obj_get(jb_in(jb_fmt("%s.layer%d", id, li))); if (!o) continue;
        char *key = jb_fmt("tilemap:%s#%d", path, li);
        Img *im = rd_tilemap_layer(t, li, key);
        if (im) { o->hasTex = 1; o->tex.ref = im; o->tex.path = jb_in(key); o->tex.tid = NULL; o->tex.x1 = 0; o->tex.y1 = 0; o->tex.x2 = round(W) < 1 ? 1 : round(W); o->tex.y2 = round(H) < 1 ? 1 : round(H); o->alpha = 255; }
        free(key);
    }
    redraw();
    return S_OK;
}

/* ---------- 스피커 ---------- */
typedef struct SpkE { const char *id, *oid; Spk *k; const char *obj; } SpkE;
static HM spks; static SpkE **spkList; static int nSpk;
static void spatial(void) {
    double cx = 0, cy = 0, cz = 1; int k = camNow(&cx, &cy, &cz); if (!k) { cx = cy = 0; cz = 1; }
    double w, h; rd_size(&w, &h); if (!w) w = 800;
    double hw = fmax(1, w / 2 / cz);
    for (int i = 0; i < nSpk; i++) {
        SpkE *s = spkList[i]; double g = 1, pn = 0;
        Obj *so = obj_get(s->oid);
        if (!so || !live(so) || (s->obj && !obj_has(s->obj))) g = 0;
        else if (s->obj) { double dx = so->pos[0] - cx, dy = so->pos[1] - cy, d = hypot(dx, dy) / hw; g = 1 / (1 + d * d); pn = fmax(-1, fmin(1, dx / hw)); }
        au_spatial(s->k, g, pn);
    }
}
static SpkE *spkOf(const char *id, Node *n) { SpkE *s = hm_get(&spks, jb_in(id)); if (!s) RT(n, "스피커 '%s' 이(가) 없습니다 (sou.setSpeaker(\"%s\"); 로 먼저 만드세요)", id, id); return s; }
static const char *sndPath(const char *fid, Node *n) {
    const char *path = hm_get(&imgAlias, jb_in(fid));
    if (!path || !jb_is_aud(path)) RT(n, "'%s' 은(는) 소리 이름이 아닙니다 (mag.importFile(\"소리.wav\") save as (\"%s\"); 로 먼저 이름을 붙이세요)", fid, fid);
    return path;
}

/* ---------- id 인자 ("<태그>" · "" = 자기 자신) ---------- */
static int tagArg(const char *name) {   /* TAG_ARG — id 가 몇 번째 값인지 (-1 = 없음) */
    static const struct { const char *n; int k; } T_[] = {{"setTextNudge",0},{"setObjNudge",0},{"setObject",1},{"setText",2},{"setObjScale",0},{"setObjPos",0},{"moveObj",0},{"nudgeObj",0},{"removeObj",0},{"escParent",0},{"splitParent",0},
        {"setObjLayer",0},{"ascendObjLayer",0},{"tagObj",0},{"getVar",0},{"setObjCyl",1},{"setObjAlpha",0},{"setObjColor",0},{"setObjOverlay",0},{"setGlow",0},{"rotateObj",0},{"setObjTexture",0},{"wearTexture",0},{"applyAnime",0},{"runAnime",0},{"breakAnime",0},
        {"setTxtContent",0},{"disableObj",0},{"enableObj",0},{"setExistableScene",1},{"setasButton",0},{"setUi",0},{"bindInput",0},{"setLink",0},{"setVelocity",0},{"setVelocityX",0},{"setVelocityY",0},{"setAnchor",0},{"setCollider",2},{"setColider",2},{"setTrigger",2},{NULL,0}};
    for (int i = 0; T_[i].n; i++) if (!strcmp(T_[i].n, name)) return T_[i].k;
    return -1;
}
static const char *rid(const char *v) { return !*v && C.selfNow ? C.selfNow : jb_in(v); }
static const char *idArg(Node *c, int k, Env *env) {
    if (C.idOvNode == c->args.v[k]) return C.idOv;
    char *s = evs(c->args.v[k], env); const char *r = rid(s); free(s); return r;
}
static int isTagId(const char *v) { size_t L = strlen(v); return L > 2 && v[0] == '<' && v[L - 1] == '>'; }
static int gfxCallInner(Node *c, Env *env);
static int forTag(Node *c, Env *env, int k, const char *v) {
    if (!objOn) RT(c, "%s", NOOBJ);
    if (!strcmp(c->name, "getVar")) RT(c, "getVar 는 오브젝트 하나만 읽습니다 — 태그 %s 는 쓸 수 없습니다", v);
    const char *tag = jb_intern(v + 1, strlen(v) - 2);
    Obj **list = jb_alloc(sizeof(Obj*) * (NORD + 1)); int n = 0;
    FOR_OBJ(o) for (int q = 0; q < o->ntags; q++) if (o->tags[q] == tag) { list[n++] = o; break; }
    Node *pn = C.idOvNode; const char *pv = C.idOv; int r = S_OK;
    for (int i = 0; i < n; i++) {
        if (obj_get(list[i]->id) != list[i]) continue;
        C.idOvNode = c->args.v[k]; C.idOv = list[i]->id;
        r = gfxCallInner(c, env);
        C.idOvNode = pn; C.idOv = pv;
        if (r != S_OK) break;
    }
    free(list);
    return r;
}
static void needObj(Node *c) { if (!objOn) RT(c, "%s", NOOBJ); }
static void mat12(Node *a, Env *env, double *out) {
    Value v = ev(a, env);
    if (a->chk == TY_MATRIX && (v.k != V_MAT || v.u.m->r != 1 || v.u.m->c != 2)) { char *k = kind_txt(v); RT(a, "matrix[1, 2] 행렬이 필요합니다 (항목: %s)", k); }
    if (v.k != V_MAT) RT(a, "matrix[1, 2] 행렬이 필요합니다");
    out[0] = v.u.m->d[0]; out[1] = v.u.m->d[1]; v_free(&v);
}
static double numArg(Node *a, Env *env) { Value v = ev(a, env); if (a->chk && v.k != V_NUM) { char *k = kind_txt(v); RT(a, "리스트 항목이 숫자가 아닙니다 (값: %s)", k); } double r = v.k == V_NUM ? v.u.n : NAN; if (a->chk == TY_INT) r = trunc(r); v_free(&v); return r; }
static char *strArg(Node *a, Env *env) { return evs(a, env); }
static double tweenMs(Node *c, Env *env) { if (!c->forExpr) return 0; double ms = numArg(c->forExpr, env); if (ms < 0) { char *s = fmt_any(v_num(ms)); RT(c->forExpr, "바뀌는 시간은 0 이상이어야 합니다 (받은 값: %s)", s); } return ms; }
static int easeOf(Node *c, Env *env) {
    if (!c->easeExpr) return 0;
    char *v = strArg(c->easeExpr, env); int id = jb_ease_id(v);
    if (id < 0) RT(c->easeExpr, "ease 이름은 \"linear\" · \"sin\" · \"sinleft\" · \"sinright\" 중 하나입니다 (받은 값: \"%s\")", v);
    free(v); return id;
}
static void setLink(Node *c, Env *env) {
    Obj *a = objOf(idArg(c, 0, env), c); char *bid = strArg(c->args.v[1], env);
    if (!*bid) { unlinkAll(a->id); free(bid); return; }
    Obj *b = objOf(bid, c->args.v[1]); free(bid);
    if (a == b) RT(c, "오브젝트를 자기 자신과 묶을 수는 없습니다 ('%s')", a->id);
    is_add(setOf(&links, a->id, 1), b->id); is_add(setOf(&links, b->id, 1), a->id);
}
static int waitFrames(double n0, Env *env, Node *c) {
    int n = i32(n0); if (n <= 0) return S_OK;
    rt_need_async(c);
    while (n > 0) {
        rt_wait_frame();
        if (env->tk && env->tk->dead) return S_DEAD;
        if (rt_frozen_env(env->tk, env)) continue;
        n--;
    }
    C.sliceT = perf_now();
    return S_OK;
}
static int popClosed(void *p) { return ((Pop *)p)->closed; }

int gfx_call(Node *c, Env *env) {
    C.selfNow = env_self(env);
    if (c->target == M_DIS || c->target == M_COL || c->target == M_MAG || c->target == M_CAN) {
        int k = tagArg(c->name);
        if (k >= 0 && k < c->args.n && C.idOvNode != c->args.v[k]) {
            char *v = evs(c->args.v[k], env);
            if (isTagId(v)) { int r = forTag(c, env, k, v); free(v); return r; }
            Node *pn = C.idOvNode; const char *pv = C.idOv;
            C.idOvNode = c->args.v[k]; C.idOv = rid(v); free(v);
            int r = gfxCallInner(c, env);
            C.idOvNode = pn; C.idOv = pv;
            return r;
        }
    }
    return gfxCallInner(c, env);
}
#define IS(nm) (!strcmp(n, nm))
static int gfxCallInner(Node *c, Env *env) {
    const char *n = c->name; int T_ = c->target;
    if (T_ == M_MAG) {
        if (IS("importFolder")) {
            for (int i = 0; i < c->nfolder; i++) {
                hm_put(&imgAlias, c->folderNames[i], (void *)c->folderPaths[i]);
                if (jb_is_aud(c->folderPaths[i])) au_load(c->folderPaths[i]); else if (jb_is_img(c->folderPaths[i])) imgOf(c->folderPaths[i], c);
            }
            rt_sys("📁 importFolder: %d개", c->nfolder);
            return S_OK;
        }
        if (IS("importFile")) {
            char *id = strArg(c->saveAs, env);
            if (!*id) RT(c->saveAs, "이름(id)이 비어 있습니다");
            hm_put(&imgAlias, jb_in(id), (void *)c->imgPath); free(id);
            if (jb_is_aud(c->imgPath)) au_load(c->imgPath); else if (jb_is_img(c->imgPath)) imgOf(c->imgPath, c);
            return S_OK;
        }
        if (IS("setasTexture")) {
            char *img = strArg(c->args.v[0], env); double a[2], b[2]; mat12(c->args.v[1], env, a); mat12(c->args.v[2], env, b);
            char *id = strArg(c->saveAs, env);
            if (!*id) RT(c->saveAs, "이름(id)이 비어 있습니다");
            const char *path = hm_get(&imgAlias, jb_in(img));
            if (!path || !jb_is_img(path)) RT(c->args.v[0], "'%s' 은(는) 이미지 이름이 아닙니다 (mag.importFile(\"경로\") save as (\"%s\"); 로 먼저 이름을 붙이세요)", img, img);
            ImgE *tx = imgOf(path, c);
            if (tx->w && (fmin(a[0], b[0]) < 0 || fmin(a[1], b[1]) < 0 || fmax(a[0], b[0]) > tx->w || fmax(a[1], b[1]) > tx->h)) {
                char *s1 = fmt_any(v_num(a[0])), *s2 = fmt_any(v_num(a[1])), *s3 = fmt_any(v_num(b[0])), *s4 = fmt_any(v_num(b[1]));
                RT(c, "텍스쳐 범위 [%s, %s] ~ [%s, %s] 이(가) 이미지 크기 %d × %d 를 벗어났습니다", s1, s2, s3, s4, tx->w, tx->h);
            }
            if (a[0] == b[0] || a[1] == b[1]) RT(c, "텍스쳐 범위의 가로나 세로가 0 입니다");
            TexDef *d = jb_calloc(sizeof(TexDef)); d->img = jb_in(img); d->path = path; d->x1 = a[0]; d->y1 = a[1]; d->x2 = b[0]; d->y2 = b[1]; d->n = c;
            hm_put(&texDefs, jb_in(id), d); free(img); free(id); return S_OK;
        }
        if (IS("setasAnime")) {
            Value L = ev(c->args.v[0], env); double ms = numArg(c->args.v[1], env); char *id = strArg(c->args.v[2], env);
            if (!*id) RT(c->args.v[2], "이름(id)이 비어 있습니다");
            if (L.k != V_LIST || !L.u.l->n) RT(c->args.v[0], "애니메이션에 넣을 텍스쳐가 없습니다 (리스트가 비어 있음)");
            if (!(ms >= 1)) { char *s = fmt_any(v_num(ms)); RT(c->args.v[1], "프레임마다 기다리는 시간은 1ms 이상이어야 합니다 (받은 값: %s)", s); }
            AnimeDef *A = jb_calloc(sizeof(AnimeDef)); A->frames = jb_alloc(sizeof(char*) * L.u.l->n); A->n = L.u.l->n; A->ms = ms;
            for (int i = 0; i < L.u.l->n; i++) {
                Value f = L.u.l->items[i];
                if (f.k != V_STR) { char *s = fmt_any(f); RT(c->args.v[0], "애니메이션의 프레임은 텍스쳐 id 문자열입니다 (받은 값: %s)", s); }
                if (!hm_get(&texDefs, jb_in(f.u.s))) RT(c->args.v[0], "텍스쳐 '%s' 이(가) 없습니다 (mag.setasTexture(\"이미지\", [x1, y1], [x2, y2]) save as (\"%s\"); 로 먼저 만드세요)", f.u.s, f.u.s);
                A->frames[i] = jb_in(f.u.s);
            }
            hm_put(&animes, jb_in(id), A); v_free(&L); free(id); return S_OK;
        }
        Obj *o = objOf(idArg(c, 0, env), c);
        if (o->kind == OK_TEXT || o->kind == OK_NONE || o->kind == OK_POLY)
            RT(c, "'%s' 은(는) %s 오브젝트라 텍스쳐를 입힐 수 없습니다 (사각형 · 원만)", o->id, o->kind == OK_TEXT ? "글자" : o->kind == OK_POLY ? "다각형" : "보이지 않는");
        if (IS("breakAnime")) { animeStop(o); return S_OK; }
        if (IS("wearTexture")) {
            Value tv = ev(c->args.v[1], env);
            if (tv.k == V_DATA) {   /* mag.wearTexture("id", 데이터): 카메라에 귀속된 data = 계속 바뀌는 영상 · 바이트 = 그 그림 */
                Data *d = tv.u.dt; int w = 0, h = 0; Img *im = NULL;
                if (d->dev == 1) { jb_permit_cam_size(&w, &h); if (!w) { w = 640; h = 480; } im = rd_cam_img(); }
                else if (!d->dev && data_frame(d->b, d->n, &w, &h)) { unsigned char *px = jb_alloc((size_t)w * h * 4); memcpy(px, d->b + 8, (size_t)w * h * 4); im = rd_image_rgba(px, w, h); }
                int bind = d->dev == 1 ? d->bind : 0; v_free(&tv);
                if (!im) RT(c->args.v[1], "이 data 는 텍스쳐로 쓸 수 없습니다 (카메라에 귀속된 data 나 카메라에서 나온 그림만)");
                animeStop(o); if (!o->hasTex) strcpy(o->color, "#ffffff");
                o->hasTex = 1; o->tex.ref = im; o->tex.tid = "@data"; o->camBind = bind;
                { static int seq; o->tex.path = bind ? "@cam" : jb_in(jb_fmt("@data#%d", ++seq)); }   /* 빛 번짐 캐시가 그림마다 다르게 */
                o->tex.x1 = 0; o->tex.y1 = 0; o->tex.x2 = w; o->tex.y2 = h;
                redraw(); return S_OK;
            }
            animeStop(o); char *tid = tv.k == V_STR ? tv.u.s : fmt_any(tv); if (tv.k != V_STR) v_free(&tv);
            if (!*tid) { if (o->hasTex) { o->hasTex = 0; redraw(); } free(tid); return S_OK; }
            if (!hm_get(&texDefs, jb_in(tid))) RT(c->args.v[1], "텍스쳐 '%s' 이(가) 없습니다 (mag.setasTexture(\"이미지\", [x1, y1], [x2, y2]) save as (\"%s\"); 로 먼저 만드세요)", tid, tid);
            showTex(o, jb_in(tid), c); free(tid); return S_OK;
        }
        if (IS("applyAnime") || IS("runAnime")) {
            char *aid = strArg(c->args.v[1], env); double sp = IS("runAnime") ? numArg(c->args.v[2], env) : 1;
            if (!hm_get(&animes, jb_in(aid))) RT(c->args.v[1], "애니메이션 '%s' 이(가) 없습니다 (mag.setasAnime([...], ms, \"%s\"); 로 먼저 만드세요)", aid, aid);
            if (!(sp > 0)) { char *s = fmt_any(v_num(sp)); RT(c->args.v[2], "배속은 0 보다 커야 합니다 (받은 값: %s)", s); }
            if (o->an) { o->an->hasNext = 1; o->an->nextId = jb_in(aid); o->an->nextSp = sp; }
            else animeStart(o, jb_in(aid), sp, perf_now(), c);
            free(aid); return S_OK;
        }
        /* setObjTexture */
        char *tid = strArg(c->args.v[1], env); double a[2], b[2]; mat12(c->args.v[2], env, a); mat12(c->args.v[3], env, b);
        if (!*tid) { if (o->hasTex) { o->hasTex = 0; redraw(); } free(tid); return S_OK; }
        const char *path = hm_get(&imgAlias, jb_in(tid));
        if (!path || !jb_is_img(path)) RT(c->args.v[1], "'%s' 은(는) 이미지 이름이 아닙니다 (mag.importFile(\"경로\") save as (\"%s\"); 로 먼저 이름을 붙이세요)", tid, tid);
        ImgE *tx = imgOf(path, c);
        if (tx->w && (fmin(a[0], b[0]) < 0 || fmin(a[1], b[1]) < 0 || fmax(a[0], b[0]) > tx->w || fmax(a[1], b[1]) > tx->h)) {
            char *s1 = fmt_any(v_num(a[0])), *s2 = fmt_any(v_num(a[1])), *s3 = fmt_any(v_num(b[0])), *s4 = fmt_any(v_num(b[1]));
            RT(c, "텍스쳐 범위 [%s, %s] ~ [%s, %s] 이(가) 이미지 크기 %d × %d 를 벗어났습니다", s1, s2, s3, s4, tx->w, tx->h);
        }
        if (a[0] == b[0] || a[1] == b[1]) RT(c, "텍스쳐 범위의 가로나 세로가 0 입니다");
        o->hasTex = 1; o->tex.ref = tx->img; o->tex.tid = jb_in(tid); o->tex.path = path; o->tex.x1 = a[0]; o->tex.y1 = a[1]; o->tex.x2 = b[0]; o->tex.y2 = b[1];
        strcpy(o->color, "#ffffff"); redraw(); free(tid);
        return S_OK;
    }
    if (T_ == M_CAN) {
        if (IS("bindInput")) {
            Obj *o = objOf(idArg(c, 0, env), c); char *k0 = strArg(c->toExpr, env); char kb[64]; key_canon(k0, kb, sizeof kb); free(k0);
            if (!o->btn) { o->btn = 1; o->btnSeen = o->relSeen = perf_now(); }
            for (int i = 0; i < keyBinds.cap; i++) if (keyBinds.t[i].k && keyBinds.t[i].v) is_del(keyBinds.t[i].v, o->id);
            if (*kb) is_add(setOf(&keyBinds, jb_in(kb), 1), o->id);
            return S_OK;
        }
        if (IS("setUi")) { uiTree(objOf(idArg(c, 0, env), c)); redraw(); return S_OK; }
        if (IS("setasButton")) { Obj *o = objOf(idArg(c, 0, env), c); if (!o->btn) { o->btn = 1; o->btnSeen = o->relSeen = perf_now(); } return S_OK; }
        if (IS("setUpScene")) {
            if (sceneOn) return S_OK;
            sceneOn = 1; curScene = 0; sceneN = 0; nSceneNames = 1; sceneNames = jb_realloc(sceneNames, sizeof(char*)); sceneNames[0] = "";
            FOR_OBJ(o) if (!o->hasScenes) scenesSet1(o, o->spk ? -1 : 0);
            rt_sys("Canvas: 장면 기능 활성화"); return S_OK;
        }
        if (!sceneOn) RT(c, "%s", NOSCENE);
        if (IS("closeScene")) {
            if (!nPop) RT(c, "열려 있는 팝업 장면이 없습니다 (can.popupScene(\"장면\"); 으로 연 장면만 닫을 수 있습니다)");
            Pop *f = popStack[--nPop];
            rt_sys("⧉ 팝업 장면 닫음 → %s", f->scene < nSceneNames && sceneNames[f->scene] && *sceneNames[f->scene] ? sceneNames[f->scene] : "처음 장면");
            popClose(f, 1); redraw(); physStart(); return S_OK;
        }
        char *nm = c->canObj ? NULL : strArg(c->args.v[0], env);
        if (nm && (!*nm || !strcmp(nm, "*"))) RT(c->args.v[0], "장면 이름이 올바르지 않습니다 (비어 있거나 '*'): \"%s\"", nm);
        const char *inm = nm ? jb_in(nm) : NULL;
        if (IS("nameScene")) {
            const char *old = curScene < nSceneNames ? sceneNames[curScene] : NULL; if (old && old == inm) return S_OK;
            if (hm_get(&sceneByName, inm)) RT(c->args.v[0], "장면 이름 '%s' 은(는) 이미 다른 장면이 쓰고 있습니다", nm);
            leaveScene();
            if (old && *old) hm_del(&sceneByName, old);
            hm_put(&sceneByName, inm, (void *)(intptr_t)(curScene + 1));
            if (curScene >= nSceneNames) { sceneNames = jb_realloc(sceneNames, sizeof(char*) * (curScene + 1)); for (int i = nSceneNames; i <= curScene; i++) sceneNames[i] = ""; nSceneNames = curScene + 1; }
            sceneNames[curScene] = inm;
            runSceneCode(inm); return S_OK;
        }
        if (IS("popupScene")) {
            intptr_t k = (intptr_t)hm_get(&sceneByName, inm);
            if (!k) RT(c->args.v[0], "장면 '%s' 이(가) 없습니다 (can.setScene 으로 먼저 만들거나 can.nameScene 으로 이름을 붙이세요)", nm);
            if (k - 1 == curScene) RT(c->args.v[0], "장면 '%s' 은(는) 지금 장면입니다 — 다른 장면을 팝업으로 여세요", nm);
            for (int i = 0; i < nPop; i++) if (popStack[i]->scene == k - 1) RT(c->args.v[0], "장면 '%s' 은(는) 이미 팝업 아래에 멈춰 있습니다", nm);
            rt_need_async(c);
            Pop *f = popOpen((int)k - 1, inm);
            return rt_wait_until(popClosed, f, env, 0);
        }
        if (IS("setScene")) {
            if (hm_get(&sceneByName, inm)) RT(c->args.v[0], "장면 '%s' 은(는) 이미 있습니다", nm);
            int k = ++sceneN; hm_put(&sceneByName, inm, (void *)(intptr_t)(k + 1));
            if (k >= nSceneNames) { sceneNames = jb_realloc(sceneNames, sizeof(char*) * (k + 1)); for (int i = nSceneNames; i <= k; i++) sceneNames[i] = ""; nSceneNames = k + 1; }
            sceneNames[k] = inm; return S_OK;
        }
        if (IS("sendtoScene")) {
            intptr_t k = (intptr_t)hm_get(&sceneByName, inm);
            if (!k) RT(c->args.v[0], "장면 '%s' 이(가) 없습니다 (can.setScene 으로 먼저 만들거나 can.nameScene 으로 이름을 붙이세요)", nm);
            if (k - 1 == curScene) return S_OK;
            popAll(); leaveScene(); curScene = (int)k - 1; camDefault();
            rt_sys("→ 장면 %s", nm);
            redraw(); physStart(); runSceneCode(inm); return S_OK;
        }
        if (IS("setExistableScene")) {
            char *sn = strArg(c->args.v[0], env); Obj *o = objOf(idArg(c, 1, env), c);
            if (!*sn) RT(c->args.v[0], "장면 이름이 비어 있습니다");
            intptr_t k = strcmp(sn, "*") ? (intptr_t)hm_get(&sceneByName, jb_in(sn)) : 0;
            if (strcmp(sn, "*") && !k) RT(c->args.v[0], "장면 '%s' 이(가) 없습니다 (can.setScene 으로 먼저 만들거나 can.nameScene 으로 이름을 붙이세요)", sn);
            if (!o->hasScenes) scenesSet1(o, curScene);
            scenesAdd(o, !strcmp(sn, "*") ? -1 : (int)k - 1);
            free(sn); redraw(); physStart(); return S_OK;
        }
        return S_OK;
    }
    if (T_ == M_SND) {
        char *sid = strArg(c->args.v[0], env);
        if (!*sid) RT(c->args.v[0], "스피커 id 가 비어 있습니다");
        if (IS("setSpeaker")) {
            if (strpbrk(sid, "<>.")) RT(c->args.v[0], "스피커 이름에는 < > . 를 쓸 수 없습니다: \"%s\"", sid);
            const char *isid = jb_in(sid), *oid = jb_in(jb_fmt("spk.%s", sid));
            if (hm_get(&spks, isid) && obj_has(oid)) { free(sid); return S_OK; }
            if (!obj_has(oid)) { Obj *o = obj_new(oid, OK_NONE); o->spk = 1; obj_set(o); born(o, c); if (sceneOn) scenesSet1(o, -1); }
            if (!hm_get(&spks, isid)) { SpkE *s = jb_calloc(sizeof(SpkE)); s->id = isid; s->oid = oid; s->k = au_speaker(isid); hm_put(&spks, isid, s); spkList = jb_realloc(spkList, sizeof(SpkE*) * (nSpk + 1)); spkList[nSpk++] = s; }
            spatial(); free(sid); return S_OK;
        }
        SpkE *k = spkOf(sid, c->args.v[0]); free(sid);
        if (IS("playSfx")) { char *f = strArg(c->args.v[1], env); const char *pa = sndPath(f, c->args.v[1]); double ms = numArg(c->args.v[2], env); au_play(k->k, pa, ms, -1, 0, 0, 0, 0); free(f); return S_OK; }
        if (IS("repeatingPlay")) {
            char *f = strArg(c->args.v[1], env); const char *pa = sndPath(f, c->args.v[1]); int cnt = i32(numArg(c->args.v[2], env));
            if (cnt < 0) RT(c->args.v[2], "반복 횟수는 0 (무한) 이상이어야 합니다 (받은 값: %d)", cnt);
            au_play(k->k, pa, 0, cnt, 0, 0, 0, 0); free(f); return S_OK;
        }
        if (IS("repeatPart") || IS("repatPart") || IS("introPart")) {
            char *f = strArg(c->args.v[1], env); const char *pa = sndPath(f, c->args.v[1]);
            int a = i32(numArg(c->args.v[2], env)), e = i32(numArg(c->args.v[3], env));
            if (a < 0) RT(c->args.v[2], "시작 지점은 0 이상이어야 합니다 (받은 값: %d)", a);
            if (e >= 0 && e <= a) RT(c->args.v[3], "끝 지점은 시작 지점보다 뒤여야 합니다 (받은 값: %d ~ %d) — 끝을 음수로 하면 소리 끝까지", a, e);
            au_play(k->k, pa, 0, 0, 1, a, e, IS("introPart")); free(f); return S_OK;
        }
        if (IS("setVol")) { int v = i32(numArg(c->args.v[1], env)); if (v < 0) v = 0; if (v > 255) v = 255; au_set_vol(k->k, v); return S_OK; }
        if (IS("break")) { au_break(k->k, c->args.n > 1 && numArg(c->args.v[1], env) != 0); return S_OK; }
        if (IS("setFadein") || IS("setFadeout")) {
            int ms = i32(numArg(c->args.v[1], env));
            if (ms < 0) RT(c->args.v[1], "페이드 시간은 0 이상이어야 합니다 (받은 값: %d)", ms);
            au_set_fade(k->k, IS("setFadein"), ms); return S_OK;
        }
        if (IS("belongtoScene")) {
            if (!sceneOn) RT(c, "%s", NOSCENE);
            char *sn = strArg(c->args.v[1], env); Obj *so = obj_get(k->oid);
            if (!*sn) RT(c->args.v[1], "장면 이름이 비어 있습니다");
            intptr_t sk = strcmp(sn, "*") ? (intptr_t)hm_get(&sceneByName, jb_in(sn)) : 0;
            if (strcmp(sn, "*") && !sk) RT(c->args.v[1], "장면 '%s' 이(가) 없습니다 (can.setScene 으로 먼저 만들거나 can.nameScene 으로 이름을 붙이세요)", sn);
            if (so) scenesSet1(so, !strcmp(sn, "*") ? -1 : (int)sk - 1);
            free(sn); spatial(); return S_OK;
        }
        if (IS("belongto")) {
            char *tid = strArg(c->args.v[1], env); Obj *so = obj_get(k->oid);
            if (so && so->parent) { IdSet *ks = setOf(&kids, so->parent, 0); if (ks) is_del(ks, so->id); }
            if (so) so->parent = NULL;
            if (*tid) {
                Obj *g = objOf(tid, c->args.v[1]);
                if (g->spk || g->cam) RT(c->args.v[1], "스피커는 오브젝트에만 귀속됩니다 ('%s')", g->id);
                if (so) { movePos(so, g->pos[0], g->pos[1]); so->parent = g->id; is_add(setOf(&kids, g->id, 1), so->id); }
            }
            k->obj = *tid ? jb_in(tid) : NULL; free(tid); spatial(); return S_OK;
        }
        return S_OK;
    }
    if (T_ == M_CAM) {
        if (IS("setUpCam")) { if (!camOn) { camOn = 1; camDefault(); rt_sys("SeongBin: 카메라 기능 활성화 (cam.default)"); redraw(); } return S_OK; }
        if (!camOn) RT(c, "카메라 기능이 꺼져 있습니다 — 먼저 seo.setUpCam(); 을 실행하세요");
        needObj(c);
        char *nm = strArg(c->args.v[0], env);
        if (!*nm || strpbrk(nm, "<>.")) RT(c->args.v[0], "카메라 이름이 올바르지 않습니다 (비어 있거나 < > . 이 들어감): \"%s\"", nm);
        const char *id = jb_in(jb_fmt("cam.%s", nm));
        if (IS("setCam")) {
            if (obj_has(id)) RT(c->args.v[0], "카메라 '%s' 은(는) 이미 있습니다", nm);
            Obj *o = obj_new(id, OK_NONE); o->cam = 1; o->zoom = 1; obj_set(o);
            camOrder = jb_realloc(camOrder, sizeof(char*) * (nCamOrder + 1)); camOrder[nCamOrder++] = id; born(o, c);
            redraw(); free(nm); return S_OK;
        }
        Obj *o = obj_get(id);
        if (!o || !o->cam) RT(c->args.v[0], "카메라 '%s' 이(가) 없습니다 (seo.setCam(\"%s\"); 으로 먼저 만드세요)", nm, nm);
        free(nm);
        if (IS("setMainCamas")) { hm_put(&camMainByScene, (void *)(intptr_t)((sceneOn ? curScene : 0) + 1), (void *)id); redraw(); return S_OK; }
        if (IS("setZoom")) {
            double z = numArg(c->args.v[1], env);
            if (!(z > 0)) { char *s = fmt_any(v_num(z)); RT(c->args.v[1], "배율은 0 보다 커야 합니다 (받은 값: %s)", s); }
            if (o->zoom != z) { o->zoom = z; redraw(); } return S_OK;
        }
        if (IS("moveCamto")) {
            double to[2]; C.curObj = o; mat12(c->args.v[1], env, to); C.curObj = NULL;
            double ms = c->forExpr ? numArg(c->forExpr, env) : 0;
            if (ms < 0) { char *s = fmt_any(v_num(ms)); RT(c->forExpr, "이동 시간은 0 이상이어야 합니다 (받은 값: %s)", s); }
            o->follow = NULL;
            if (ms == 0) { setPos(o, to); return S_OK; }
            return moveAnim(o, to, ms, 0, 0, env, c);
        }
        if (IS("follow")) {
            char *tid = strArg(c->args.v[1], env); Obj *g = objOf(tid, c->args.v[1]); free(tid);
            if (g->cam) RT(c->args.v[1], "카메라는 다른 카메라를 따라갈 수 없습니다 ('%s')", g->id);
            o->follow = g->id; physStart(); redraw(); return S_OK;
        }
        if (IS("followOut")) { o->follow = NULL; return S_OK; }
        return S_OK;
    }
    if (T_ == M_COL) {
        if (IS("setLink")) { setLink(c, env); return S_OK; }
        if (IS("setVelocity")) {
            Obj *o = objOf(idArg(c, 0, env), c); double v[2]; C.curObj = o; mat12(c->args.v[1], env, v); C.curObj = NULL;
            o->vel[0] = v[0]; o->vel[1] = v[1]; physKick(o); return S_OK;
        }
        if (IS("setVelocityX") || IS("setVelocityY")) {
            Obj *o = objOf(idArg(c, 0, env), c); int k = IS("setVelocityX") ? 0 : 1;
            C.curObj = o; double v = numArg(c->args.v[1], env); C.curObj = NULL;
            if (!isfinite(v)) { char *s = fmt_any(v_num(v)); RT(c->args.v[1], "속도가 숫자가 아닙니다 (받은 값: %s)", s); }
            o->vel[k] = v; physKick(o); return S_OK;
        }
        if (IS("setAnchor")) { Obj *o = objOf(idArg(c, 0, env), c); int on = numArg(c->args.v[1], env) != 0; o->anchored = on; if (on) { o->vel[0] = o->vel[1] = 0; } else physKick(o); return S_OK; }
        if (IS("setCollider") || IS("setColider")) {
            needObj(c);
            double pos[2], size[2]; mat12(c->args.v[0], env, pos); mat12(c->args.v[1], env, size);
            const char *id = idArg(c, 2, env); double e = numArg(c->args.v[3], env); int grav = numArg(c->args.v[4], env) != 0;
            if (!*id) RT(c->args.v[2], "콜라이더 id 가 비어 있습니다");
            if (!(e >= 0 && e <= 1)) { char *s = fmt_any(v_num(e)); RT(c->args.v[3], "탄성계수는 0 ~ 1 사이입니다 (받은 값: %s)", s); }
            Obj *o = obj_get(id);
            if (o) { o->hasCol = 1; o->colOff[0] = pos[0] - o->pos[0]; o->colOff[1] = pos[1] - o->pos[1]; o->colSize[0] = size[0]; o->colSize[1] = size[1]; o->colE = e; o->colGrav = grav; }
            else { o = obj_new(id, OK_NONE); o->pos[0] = pos[0]; o->pos[1] = pos[1]; o->scale[0] = size[0]; o->scale[1] = size[1]; o->hasCol = 1; o->colSize[0] = size[0]; o->colSize[1] = size[1]; o->colE = e; o->colGrav = grav; camName(id, c); obj_set(o); born(o, c); }
            colOn = 1; physKick(o); redraw(); return S_OK;
        }
        if (IS("setGravity")) {
            double f = numArg(c->args.v[0], env);
            if (!isfinite(f)) { char *s = fmt_any(v_num(f)); RT(c->args.v[0], "중력 값이 숫자가 아닙니다 (받은 값: %s)", s); }
            gravScale = f; FOR_OBJ(o) if (o->hasCol && o->colGrav) { physStart(); break; }
            return S_OK;
        }
        if (IS("setTrigger")) {
            needObj(c);
            double pos[2], size[2]; mat12(c->args.v[0], env, pos); mat12(c->args.v[1], env, size);
            const char *id = idArg(c, 2, env); int hold = numArg(c->args.v[3], env) != 0;
            if (!*id) RT(c->args.v[2], "트리거 id 가 비어 있습니다");
            Obj *o = obj_get(id); int nw = 0;
            if (o) { o->hasTrig = 1; o->trigOff[0] = pos[0] - o->pos[0]; o->trigOff[1] = pos[1] - o->pos[1]; o->trigSize[0] = size[0]; o->trigSize[1] = size[1]; o->trigHold = hold; }
            else { o = obj_new(id, OK_NONE); o->pos[0] = pos[0]; o->pos[1] = pos[1]; o->scale[0] = size[0]; o->scale[1] = size[1]; o->hasTrig = 1; o->trigSize[0] = size[0]; o->trigSize[1] = size[1]; o->trigHold = hold; camName(id, c); obj_set(o); nw = 1; }
            o->isTrig = 0; o->trigId = ""; trigOn = 1; checkTriggers(); redraw();
            if (nw) born(o, c);
            return S_OK;
        }
        if (IS("setColliderVisible")) { GFX_SHOW_COL = numArg(c->args.v[0], env) != 0; redraw(); return S_OK; }
        return S_OK;
    }
    /* ---------- DisPlayer ---------- */
    if (IS("setDisplay")) { disOn = 1; rd_open(); return S_OK; }
    if (IS("waitF")) return waitFrames(numArg(c->args.v[0], env), env, c);
    if (IS("setLink")) { setLink(c, env); return S_OK; }
    if (IS("escParent")) { Obj *o = objOf(idArg(c, 0, env), c); if (o->parent) { IdSet *ks = setOf(&kids, o->parent, 0); if (ks) is_del(ks, o->id); o->parent = NULL; } return S_OK; }
    if (IS("splitParent")) {
        Obj *o = objOf(idArg(c, 0, env), c); IdSet *ks = setOf(&kids, o->id, 0);
        if (ks) { for (int i = 0; i < ks->n; i++) { Obj *ch = obj_get(ks->v[i]); if (ch && ch->parent == o->id) ch->parent = NULL; } hm_del(&kids, o->id); }
        return S_OK;
    }
    if (IS("setObjPlg")) {
        needObj(c);
        int nv = c->args.n - 2; const char *id = idArg(c, nv, env);
        if (!*id) RT(c->args.v[nv], "오브젝트 id 가 비어 있습니다");
        C.curObj = obj_get(id);
        double vs[4][2]; for (int i = 0; i < nv; i++) mat12(c->args.v[i], env, vs[i]);
        char color[8]; colorArg(c->args.v[nv + 1], env, color);
        C.curObj = NULL;
        double x0 = INFINITY, x1 = -INFINITY, y0 = INFINITY, y1 = -INFINITY;
        for (int i = 0; i < nv; i++) { x0 = fmin(x0, vs[i][0]); x1 = fmax(x1, vs[i][0]); y0 = fmin(y0, vs[i][1]); y1 = fmax(y1, vs[i][1]); }
        double cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, w = x1 - x0, h = y1 - y0;
        Obj *old = obj_get(id);
        if (old && old->kind == OK_POLY && !strcmp(old->color, color) && old->pos[0] == cx && old->pos[1] == cy && old->scale[0] == w && old->scale[1] == h && old->npts == nv) {
            int same = 1; for (int i = 0; i < nv; i++) if (old->pts[i][0] != vs[i][0] - cx || old->pts[i][1] != vs[i][1] - cy) same = 0;
            if (same) return S_OK;
        }
        Obj *no = obj_new(id, OK_POLY); no->pos[0] = cx; no->pos[1] = cy; no->scale[0] = w; no->scale[1] = h; strcpy(no->color, color); velNew(no, old);
        no->pts = jb_alloc(sizeof(double) * 2 * nv); no->npts = nv; for (int i = 0; i < nv; i++) { no->pts[i][0] = vs[i][0] - cx; no->pts[i][1] = vs[i][1] - cy; }
        no->pbase[0] = w ? w : 1; no->pbase[1] = h ? h : 1;
        keepRel(old, no); camName(id, c); obj_set(no); redraw(); if (!old) born(no, c); return S_OK;
    }
    if (IS("setObjCyl")) {
        needObj(c);
        const char *id = idArg(c, 1, env);
        if (!*id) RT(c->args.v[1], "오브젝트 id 가 비어 있습니다");
        C.curObj = obj_get(id);
        double pos[2]; mat12(c->args.v[0], env, pos); double r = numArg(c->args.v[2], env); char color[8]; colorArg(c->args.v[3], env, color);
        C.curObj = NULL;
        if (!(r >= 0)) { char *s = fmt_any(v_num(r)); RT(c->args.v[2], "반지름은 0 이상이어야 합니다 (받은 값: %s)", s); }
        Obj *old = obj_get(id);
        if (old && old->kind == OK_CIRCLE && !strcmp(old->color, color) && old->pos[0] == pos[0] && old->pos[1] == pos[1] && old->scale[0] == r * 2 && old->scale[1] == r * 2) return S_OK;
        Obj *no = obj_new(id, OK_CIRCLE); no->pos[0] = pos[0]; no->pos[1] = pos[1]; no->scale[0] = no->scale[1] = r * 2; strcpy(no->color, color); velNew(no, old);
        keepRel(old, no); camName(id, c); obj_set(no); redraw(); if (!old) born(no, c); return S_OK;
    }
    if (IS("setObjAlpha")) {
        Obj *o = objOf(idArg(c, 0, env), c); double av = floor(numArg(c->args.v[1], env) + 0.5); if (!isfinite(av)) av = 0;
        int a = av < 0 ? 0 : av > 255 ? 255 : (int)av; double ms = tweenMs(c, env);
        if (ms > 0) { double to[1] = {a}; return tween(o, 2, to, 1, ms, env, c); }
        if ((o->alpha < 0 ? 255 : o->alpha) != a) { o->alpha = a; redraw(); }
        return S_OK;
    }
    if (IS("setObjColor")) {
        Obj *o = objOf(idArg(c, 0, env), c); char cl[8]; colorArg(c->args.v[1], env, cl); double ms = tweenMs(c, env);
        if (ms > 0) { double to[3]; hexRgb(cl, to); return tween(o, 5, to, 3, ms, env, c); }
        if (strcmp(o->color, cl)) { strcpy(o->color, cl); redraw(); }
        return S_OK;
    }
    if (IS("setObjOverlay")) {
        Obj *o = objOf(idArg(c, 0, env), c); char cl[8]; colorArg(c->args.v[1], env, cl);
        double av = floor(numArg(c->args.v[2], env) + 0.5); if (!isfinite(av)) av = 0; int a = av < 0 ? 0 : av > 255 ? 255 : (int)av;
        double ms = tweenMs(c, env);
        if (ms > 0) { if (!o->hasOv || !o->ovA) { o->hasOv = 1; strcpy(o->ovColor, cl); o->ovA = 0; } double to[4]; hexRgb(cl, to); to[3] = a; return tween(o, 4, to, 4, ms, env, c); }
        Tw *pw = twOf(o, 4); if (pw) twEnd(pw);
        if (!o->hasOv || strcmp(o->ovColor, cl) || o->ovA != a) { o->hasOv = 1; strcpy(o->ovColor, cl); o->ovA = a; redraw(); }
        return S_OK;
    }
    if (IS("setGlow")) {
        Obj *o = objOf(idArg(c, 0, env), c); char cl[8]; colorArg(c->args.v[1], env, cl); double st = numArg(c->args.v[2], env);
        if (!(st >= 0)) { char *s = fmt_any(v_num(st)); RT(c->args.v[2], "빛 번짐 세기는 0 이상이어야 합니다 (받은 값: %s)", s); }
        int ng = st > 0;
        if (o->hasGlow != ng || (ng && (strcmp(o->glowColor, cl) || o->glowS != st))) { o->hasGlow = ng; if (ng) { strcpy(o->glowColor, cl); o->glowS = st; } redraw(); }
        return S_OK;
    }
    if (IS("rotateObj")) {
        Obj *o = objOf(idArg(c, 0, env), c); double a = numArg(c->args.v[1], env); if (!isfinite(a)) a = 0; double ms = numArg(c->args.v[2], env);
        if (!(ms >= 0)) { char *s = fmt_any(v_num(ms)); RT(c->args.v[2], "돌리는 시간은 0 이상이어야 합니다 (받은 값: %s)", s); }
        Tw *p = twOf(o, 3); double to = (p ? p->to[0] : o->angle) + a;
        if (ms > 0) { double t1[1] = {to}; return tween(o, 3, t1, 1, ms, env, c); }
        if (p) twEnd(p);
        if (o->angle != to) { setAng(o, to); redraw(); }
        return S_OK;
    }
    if (IS("setRenderLimit")) {
        double r = numArg(c->args.v[0], env);
        if (!(r >= 0)) { char *s = fmt_any(v_num(r)); RT(c->args.v[0], "그리기 반지름은 0 이상이어야 합니다 (받은 값: %s) — 0 = 제한 없음", s); }
        renderLim = r; redraw(); return S_OK;
    }
    if (IS("disableObj") || IS("enableObj")) {
        Obj *o = objOf(idArg(c, 0, env), c); int off = IS("disableObj");
        if (o->cam && off) RT(c, "카메라 '%s' 는 비활성화할 수 없습니다", o->id);
        if (!!o->off == off) return S_OK;
        o->off = off; if (off) offOn = 1;
        redraw(); if (!off) physStart(); return S_OK;
    }
    if (IS("setTxtContent")) {
        Obj *o = objOf(idArg(c, 0, env), c); char *v = strArg(c->args.v[1], env);
        textOf(o, c); if (strcmp(o->text, v)) { free(o->text); o->text = v; redraw(); } else free(v);
        return S_OK;
    }
    if (IS("setTilemap")) {
        needObj(c);
        double pos[2]; mat12(c->args.v[0], env, pos); char *raw = strArg(c->args.v[1], env);
        const char *path = hm_get(&imgAlias, jb_in(raw));
        if (!path) { char *nn = path_resolve(raw, c->file); if (nn && proj_file(nn)) path = proj_file(nn)->path; free(nn); }
        if (!path || !jb_is_tl(path)) RT(c->args.v[1], "'%s' 은(는) 타일맵(.tl)이 아닙니다 (mag.importFile(\"경로.tl\") save as (\"%s\"); 로 이름을 붙이거나 .tl 경로를 쓰세요)", raw, raw);
        int r = tilemapPlace(raw, path, pos[0], pos[1], c); free(raw); return r;
    }
    if (IS("setObjLayer")) {
        Obj *o = objOf(idArg(c, 0, env), c); int k = i32(numArg(c->args.v[1], env));
        if (o->layer != k || !layerOn) { o->layer = k; o->hasLayer = 1; layerOn = 1; redraw(); }
        return S_OK;
    }
    if (IS("ascendObjLayer")) {
        Obj *o = objOf(idArg(c, 0, env), c); int k = i32(numArg(c->args.v[1], env));
        IdSet seen = {0}; Obj **st = jb_alloc(sizeof(Obj*) * 16); int ns = 0, cs_ = 16; st[ns++] = o;
        while (ns) {
            Obj *x = st[--ns]; if (is_has(&seen, x->id)) continue; is_add(&seen, x->id); x->layer += k; x->hasLayer = 1;
            IdSet *ks = setOf(&kids, x->id, 0);
            if (ks) for (int i = ks->n - 1; i >= 0; i--) { Obj *m = obj_get(ks->v[i]); if (m) { if (ns == cs_) { cs_ *= 2; st = jb_realloc(st, sizeof(Obj*) * cs_); } st[ns++] = m; } }
        }
        free(st); free(seen.v); layerOn = 1; redraw(); return S_OK;
    }
    if (IS("tagObj")) {
        Obj *o = objOf(idArg(c, 0, env), c); char *tg = strArg(c->args.v[1], env);
        if (!*tg || strpbrk(tg, "<>")) RT(c->args.v[1], "태그 이름은 비어 있거나 < > 가 들어갈 수 없습니다 — 받은 값: \"%s\"", tg);
        const char *it = jb_in(tg); free(tg);
        for (int i = 0; i < o->ntags; i++) if (o->tags[i] == it) return S_OK;
        o->tags = jb_realloc((void *)o->tags, sizeof(char*) * (o->ntags + 1)); o->tags[o->ntags++] = it;
        return S_OK;
    }
    if (IS("removeObj")) {
        Obj *o = objOf(idArg(c, 0, env), c);
        An *a = animOf(o->id); if (a) animEnd(a);
        for (int i = 0; i < nTw; i++) if (tweens[i]->active && tweens[i]->o == o) twEnd(tweens[i]);
        if (o->parent) { IdSet *ks = setOf(&kids, o->parent, 0); if (ks) is_del(ks, o->id); }
        IdSet *ks = setOf(&kids, o->id, 0); if (ks) for (int i = 0; i < ks->n; i++) { Obj *ch = obj_get(ks->v[i]); if (ch) ch->parent = NULL; }
        hm_del(&kids, o->id); obj_delete(o->id);
        unlinkAll(o->id);
        for (int i = 0; i < o->ninsts; i++) o->insts[i]->tk->dead = 1;
        FOR_OBJ(t) if (t->trigId == o->id) { if (!t->trigHold) { t->isTrig = 0; t->trigId = ""; } }
        redraw(); return S_OK;
    }
    if (IS("enableScreenScale") || IS("disableScreenScale")) { rd_screen_scale(IS("enableScreenScale")); return S_OK; }
    if (IS("setParent")) {
        char *a = strArg(c->relChild, env), *b = strArg(c->relParent, env);
        Obj *ch = objOf(rid(a), c->relChild), *pa = objOf(rid(b), c->relParent); free(a); free(b);
        if (ch == pa) RT(c, "오브젝트 '%s' 을(를) 자기 자신의 부모로 할 수 없습니다", ch->id);
        if (isAncestor(ch->id, pa->id)) RT(c, "'%s' 은(는) 이미 '%s' 의 자식이라 부모로 할 수 없습니다 (순환)", pa->id, ch->id);
        if (ch->parent) { IdSet *ks = setOf(&kids, ch->parent, 0); if (ks) is_del(ks, ch->id); }
        ch->parent = pa->id; is_add(setOf(&kids, pa->id, 1), ch->id);
        if (pa->ui && !ch->ui) { uiTree(ch); redraw(); }
        return S_OK;
    }
    if (IS("setFont")) {
        char *v = strArg(c->args.v[0], env);
        const char *path = c->fontPath ? c->fontPath : hm_get(&imgAlias, jb_in(v));
        if (!path) { char *nn = path_resolve(v, c->file); if (nn && proj_file(nn)) path = proj_file(nn)->path; free(nn); }
        if (!path) RT(c->args.v[0], "'%s' 은(는) 글꼴 경로나 이름이 아닙니다 (프로젝트 안 .ttf 경로 · mag.importFile 로 붙인 이름)", v);
        if (!jb_is_ttf(path)) RT(c->args.v[0], "setFont 은 .ttf · .otf 글꼴 파일만 씁니다: %s", path);
        if (!rd_set_font(path)) RT(c->args.v[0], "글꼴 '%s' 을(를) 읽지 못했습니다 (손상되었거나 .ttf 가 아님)", path);
        gfx_font_changed(); redraw(); free(v); return S_OK;
    }
    if (IS("setDisplayColor")) {
        char *h = strArg(c->args.v[0], env);
        if (!jb_hex_ok(h)) RT(c, "색은 헥스코드여야 합니다 (예: \"#1e90ff\") — 받은 값: \"%s\"", h);
        snprintf(bgColor, sizeof bgColor, "#%s", h[0] == '#' ? h + 1 : h); rd_set_bg(bgColor); free(h);
        return S_OK;
    }
    if (IS("setupObject")) { objOn = 1; rt_sys("DisPlayer: 오브젝트 기능 활성화"); return S_OK; }
    if (IS("setObject")) {
        needObj(c);
        const char *id = idArg(c, 1, env);
        if (!*id) RT(c->args.v[1], "오브젝트 id 가 비어 있습니다");
        C.curObj = obj_get(id);
        double pos[2], scale[2]; char color[8];
        mat12(c->args.v[0], env, pos); mat12(c->args.v[2], env, scale); colorArg(c->args.v[3], env, color);
        C.curObj = NULL;
        Obj *old = obj_get(id);
        if (old && old->kind == OK_RECT && !strcmp(old->color, color) && old->pos[0] == pos[0] && old->pos[1] == pos[1] && old->scale[0] == scale[0] && old->scale[1] == scale[1]) return S_OK;
        Obj *no = obj_new(id, OK_RECT); no->pos[0] = pos[0]; no->pos[1] = pos[1]; no->scale[0] = scale[0]; no->scale[1] = scale[1]; strcpy(no->color, color); velNew(no, old);
        keepRel(old, no); camName(id, c); obj_set(no); redraw(); if (!old) born(no, c); return S_OK;
    }
    if (IS("setText")) {
        needObj(c);
        const char *id = idArg(c, 2, env);
        if (!*id) RT(c->args.v[2], "글자 오브젝트 id 가 비어 있습니다");
        Obj *old = obj_get(id); C.curObj = old;
        double pos[2]; mat12(c->args.v[0], env, pos); char *text = strArg(c->args.v[1], env); double size = numArg(c->args.v[3], env);
        int outline = numArg(c->args.v[4], env) != 0; char color[8]; colorArg(c->args.v[5], env, color);
        C.curObj = NULL;
        if (!(size > 0)) { char *s = fmt_any(v_num(size)); RT(c->args.v[3], "폰트 크기는 0 보다 커야 합니다 (받은 값: %s)", s); }
        old = obj_get(id);
        if (old && old->kind == OK_TEXT && !strcmp(old->text, text) && old->size == size && old->outline == outline && !strcmp(old->color, color) && old->pos[0] == pos[0] && old->pos[1] == pos[1]) { free(text); return S_OK; }
        Obj *no = obj_new(id, OK_TEXT); no->text = text; no->size = size; no->outline = outline; strcpy(no->color, color); no->pos[0] = pos[0]; no->pos[1] = pos[1]; no->scale[0] = 0; no->scale[1] = size; velNew(no, old);
        keepRel(old, no); camName(id, c); obj_set(no); redraw(); if (!old) born(no, c); return S_OK;
    }
    if (IS("setTextNudge") || IS("setObjNudge")) {
        Obj *o = objOf(idArg(c, 0, env), c); char *v = strArg(c->args.v[1], env); int nx, ny, ok = jb_parse_nudge(v, &nx, &ny);
        if (IS("setTextNudge") && o->kind != OK_TEXT) RT(c, "'%s' 은(는) 글자 오브젝트가 아니라 기준점을 바꿀 수 없습니다 (dis.setText 로 만든 오브젝트만)", o->id);
        if (!ok) RT(c->args.v[1], "기준점은 \"center\" · \"left\" · \"right\" · \"top\" · \"bottom\" 또는 \"top left\" 처럼 둘을 합친 것입니다 (받은 값: \"%s\")", v);
        free(v);
        if (o->hasNud && o->nud[0] == nx && o->nud[1] == ny) return S_OK;
        if (nx || ny) { o->hasNud = 1; o->nud[0] = nx; o->nud[1] = ny; } else o->hasNud = 0;
        if (o->hasCol) physKick(o); redraw(); return S_OK;
    }
    if (IS("nudgeObj")) {
        Obj *o = objOf(idArg(c, 0, env), c); double side[2]; mat12(c->args.v[1], env, side); double gx = numArg(c->args.v[2], env), gy = numArg(c->args.v[3], env);
        if (!disOn) RT(c, "화면이 없습니다 — 먼저 dis.setDisplay(); 를 실행하세요");
        if (o->kind == OK_TEXT) fitText(o);
        double W, H; rd_size(&W, &H); double w = fabs(o->scale[0]), h = fabs(o->scale[1]);
        double ox = nudX(o), oy = nudY(o);
        double m[2] = {(side[0] != 0 ? -W / 2 + gx + w / 2 : W / 2 - gx - w / 2) - ox, (side[1] != 0 ? H / 2 - gy - h / 2 : -H / 2 + gy + h / 2) - oy};
        setPos(o, m); return S_OK;
    }
    if (IS("setObjScale") || IS("setObjPos")) {
        Obj *o = objOf(idArg(c, 0, env), c); double m[2];
        C.curObj = o; mat12(c->args.v[1], env, m); C.curObj = NULL;
        double ms = tweenMs(c, env);
        if (ms > 0) { if (IS("setObjPos")) return moveAnim(o, m, ms, 1, easeOf(c, env), env, c); return tween(o, 1, m, 2, ms, env, c); }
        if (IS("setObjPos")) setPos(o, m); else setScale(o, m);
        return S_OK;
    }
    if (IS("moveObj")) {
        Obj *o = objOf(idArg(c, 0, env), c); double to[2];
        C.curObj = o; mat12(c->toExpr, env, to); C.curObj = NULL;
        double ms = c->forExpr ? numArg(c->forExpr, env) : 0;
        if (ms < 0) { char *s = fmt_any(v_num(ms)); RT(c->forExpr, "이동 시간은 0 이상이어야 합니다 (받은 값: %s)", s); }
        if (ms == 0) { if (!pathBlocked(o, to)) setPos(o, to); return S_OK; }
        return moveAnim(o, to, ms, 0, easeOf(c, env), env, c);
    }
    if (IS("getVar")) {
        Obj *o = objOf(idArg(c, 0, env), c); char *nm = strArg(c->args.v[1], env); int key, idx[2];
        if (!jb_parse_objvar(nm, &key, idx)) RT(c, "getVar 이름은 coord(position) · scale · color · text 중 하나입니다 — 받은 값: \"%s\"", nm);
        Value val; int ty;
        if (o->kind == OK_TEXT) fitText(o);
        if (idx[0]) {
            if (key == K_COLOR || key == K_TEXT || key == K_TRIGID) RT(c, "%s 에는 [행, 열] 성분이 없습니다", key == K_COLOR ? "color" : key == K_TEXT ? "text" : "trigid");
            if (key == K_ISTRIG) RT(c, "istrig 에는 [행, 열] 성분이 없습니다");
            double *m = propMat(o, key);
            if (idx[0] != 1) RT(c, "행렬의 행 번호 %d 이(가) 범위(1~1)를 벗어났습니다", idx[0]);
            if (idx[1] < 1 || idx[1] > 2) RT(c, "행렬의 열 번호 %d 이(가) 범위(1~2)를 벗어났습니다", idx[1]);
            val = v_num(m[idx[1] - 1]); ty = TY_FLOAT;
        } else if (key == K_COLOR) { val = v_str(o->color); ty = TY_STRING; }
        else if (key == K_ISTRIG) { val = v_num(trigOf(o, c)->isTrig); ty = TY_BIT; }
        else if (key == K_TRIGID) { val = v_str(trigOf(o, c)->trigId ? o->trigId : ""); ty = TY_STRING; }
        else if (key == K_TEXT) { val = v_str(textOf(o, c)); ty = TY_STRING; }
        else { val = matv(propMat(o, key)); ty = TY_MATRIX; }
        Slot *sl = slot_of(c->asInfo, c->asName, env, c);
        if (ty == TY_MATRIX ? (sl->ty != TY_MATRIX || sl->v.k != V_MAT || sl->v.u.m->r != 1 || sl->v.u.m->c != 2) : !jb_assignable(sl->ty, ty))
            RT(c, "%s 값을 변수 '%s' 에 넣을 수 없습니다", ty == TY_MATRIX ? "matrix[1, 2]" : ty_name(ty), c->asName);
        Value nv = co(val, sl->ty); v_free(&val); v_free(&sl->v); sl->v = nv; free(nm);
        return S_OK;
    }
    return S_OK;
}
#undef IS

/* ---------- 사용자 라이브러리 (JDH) 의 오브젝트 접근 — "objects.read" · "objects.write" (에디터의 JBEXT 와 같은 뜻) ---------- */
extern void jdh__fail(const char *fmt, ...);
static Obj *xobj(const char *id) { if (!objOn || !id) return NULL; Obj *o = obj_get(jb_in(id)); if (o && o->kind == OK_TEXT) fitText(o); return o; }
static Obj *xobjW(const char *id) { Obj *o = xobj(id); if (!o) jdh__fail("오브젝트 '%s' 이(가) 없습니다", id ? id : ""); return o; }
static int xout(const double *m, double *a, double *b) { if (a) *a = m[0]; if (b) *b = m[1]; return 1; }
static int xstr(const char *v, char *out, int n) { if (out && n > 0) snprintf(out, (size_t)n, "%s", v); return (int)strlen(v); }
int jdh_obj_exists(const char *id) { return xobj(id) != NULL; }
int jdh_obj_pos(const char *id, double *x, double *y) { Obj *o = xobj(id); return o ? xout(o->pos, x, y) : 0; }
int jdh_obj_scale(const char *id, double *w, double *h) { Obj *o = xobj(id); return o ? xout(o->scale, w, h) : 0; }
int jdh_obj_vel(const char *id, double *x, double *y) { Obj *o = xobj(id); return o ? xout(o->vel, x, y) : 0; }
int jdh_obj_color(const char *id, char *out, int n) { Obj *o = xobj(id); return o ? xstr(o->color, out, n) : -1; }
int jdh_obj_text(const char *id, char *out, int n) { Obj *o = xobj(id); return o && o->kind == OK_TEXT ? xstr(o->text ? o->text : "", out, n) : -1; }
void jdh_obj_set_pos(const char *id, double x, double y) { Obj *o = xobjW(id); double m[2] = {x, y}; if (isfinite(x) && isfinite(y)) setPos(o, m); }
void jdh_obj_set_scale(const char *id, double w, double h) { Obj *o = xobjW(id); double m[2] = {w, h}; if (isfinite(w) && isfinite(h)) setScale(o, m); }
void jdh_obj_set_vel(const char *id, double x, double y) { Obj *o = xobjW(id); if (!isfinite(x) || !isfinite(y)) return; o->vel[0] = x; o->vel[1] = y; physKick(o); }
void jdh_obj_set_color(const char *id, const char *c) {
    Obj *o = xobjW(id);
    if (!c || !jb_hex_ok(c)) jdh__fail("색은 헥스코드여야 합니다 (예: \"#ff8800\") — 받은 값: \"%s\"", c ? c : "");
    const char *h = c[0] == '#' ? c + 1 : c; char cl[8];
    if (strlen(h) == 3) snprintf(cl, 8, "#%c%c%c%c%c%c", h[0], h[0], h[1], h[1], h[2], h[2]); else snprintf(cl, 8, "#%s", h);
    if (strcmp(cl, o->color)) { strcpy(o->color, cl); redraw(); }
}
void jdh_obj_set_text(const char *id, const char *t) {
    Obj *o = xobjW(id);
    if (o->kind != OK_TEXT) jdh__fail("'%s' 은(는) 글자 오브젝트가 아니라 Text 가 없습니다 (dis.setText 로 만든 오브젝트만)", o->id);
    if (!t) t = "";
    if (strcmp(o->text, t)) { free(o->text); o->text = jb_strdup(t); redraw(); }
}
void jdh__redraw(void) { redraw(); }

void gfx_init(void) { ID_CAM_DEFAULT = jb_in("cam.default"); }
void gfx_end(void) { au_stop_all(); }
