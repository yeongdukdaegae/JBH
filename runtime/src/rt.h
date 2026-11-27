/* 실행기 내부 — run.c (언어) · gfx.c (오브젝트 · 물리 · 장면 · 카메라) · render.c (SDL 화면 · 입력) · audio.c (소리) 가 함께 씀 */
#ifndef RT_H
#define RT_H
#include "jb.h"
#include <pthread.h>

/* ---------- 옵션 ---------- */
typedef struct { int winW, winH, fullscreen, checkOnly, mute, sysLog; const char *shot; double shotAt; const char *keys; int box; } RtOpt;
extern RtOpt RT_OPT;
void rt_show_error(const char *file, int line, const char *msg);
void rt_sys(const char *fmt, ...);   /* 시스템 메시지 (--log 일 때만) */
int jb_win_pick(int save, const char *defName, char *out, size_t outn);   /* Windows 저장 · 열기 창 (winpick.c) */

/* ---------- 차례 (한 번에 하나의 실행만 · 메인 스레드도 같이 줄 섬) ---------- */
void gil_lock(void);
void gil_unlock(void);
int gil_waiters(void);
void gil_yield(void);
void rt_wait_tick(void);             /* 다음 내부 틱 (4ms) 까지 쉼 */
void rt_wait_frame(void);            /* 다음 화면 프레임까지 쉼 */
void rt_sleep_ms(double ms);         /* 최대 ms 쉼 */
void rt_tick_signal(void);
void rt_frame_signal(void);
extern volatile long RT_FRAME;       /* 화면 프레임 번호 (fN) */

/* ---------- 실행 ---------- */
typedef struct Tk { volatile int dead, paused; } Tk;
typedef struct Slot { const char *name; int ty; int dims[2]; Value v; } Slot;
typedef struct Dc { int n; } Dc;
typedef struct Env {
    struct Env *p;
    Slot **s; int n, cap;
    int hasSelf; const char *self;   /* 귀속된 오브젝트 id */
    Tk *tk;
    Dc *dc;
    struct Env **runEnvs; int nrun, caprun;
} Env;
Env *env_new(Env *p);
Slot *env_get(Env *e, const char *n);
Slot *env_def(Env *e, const char *n, int ty, const int *dims, Value v);
const char *env_self(Env *e);         /* NULL = 귀속 안 됨 */
void env_add_run(Env *e, Env *r);

/* 실행 신호 */
enum { S_OK, S_BREAK, S_CONT, S_RET, S_GOTO, S_SEND, S_DEAD };
typedef struct {
    int sig; Node *jump; int gotoLine, gotoAt; const char *gotoFile; Value ret; const char *send;
    int sync; Node *syncNode; double syncStart;
    long ops; double sliceT;
    const char *selfNow;
    Node *idOvNode; const char *idOv;
    struct Obj *curObj;
} Ctl;
extern __thread Ctl C;

#define RT(n, ...) jb_fail((n)->file, (n)->line, __VA_ARGS__)
Value ev(Node *e, Env *env);
Value *ev_ref(Node *e, Env *env);         /* 복사하지 않고 읽음 (없으면 NULL) */
double evn(Node *e, Env *env);            /* 숫자 */
char *evs(Node *e, Env *env);             /* String(값) — malloc */
int exec(Node *s, Env *env);
int seq(NodeList *l, int i, Env *env);
int block(NodeList *l, Env *env);
Value co(Value v, int ty);
Value dflt(int ty, const int *dims);
Slot *slot_of(Info *info, const char *name, Env *env, Node *n);
void rt_spawn(void (*fn)(void *), void *ud);   /* 새 실행 (스레드) — 차례 맨 뒤 */
void rt_spawn_now(void (*fn)(void *), void *ud);   /* 새 실행을 바로 시작 (첫 기다림까지) 한 뒤 돌아옴 — 편집기의 동기 시작 */
int rt_need_async(Node *c);
int rt_alive(void);
int rt_run_task_here(const char *path);   /* 이 스레드에서: 최상위 var · 첫 run · 나머지 run 은 새 실행 */
void rt_setup_task(const char *path);
int rt_run_body_thread(NodeList *body, Env *env);
int rt_wait_until(int (*pred)(void *), void *ud, Env *env, int countFrozen);
int rt_frozen_env(Tk *tk, Env *env);
extern Env **LIB_ENVS; extern Module **LIB_MODS; extern int NLIB_ENVS;
Env *lib_env(const char *mod);
extern Env *PUB;
double perf_now(void);                     /* ms */
extern double RT_T0;

/* jaw 명령 (run.c) · 그래픽 명령 (gfx.c) */
int jaw_call(Node *c, Env *env);
int gfx_call(Node *c, Env *env);          /* DisPlayer · colliders · Magellan · Canvas · soundMaster · SeongBin */
Value gfx_value(Node *e, Env *env);       /* objprop · objkeep · objvar · canobj · member(Canvas/DisPlayer) · isKeyInput */
Slot *gfx_ovslot(Node *e, Env *env);
Slot *gfx_pslot(const char *name, Env *env, Node *n);
int gfx_oassign(Node *s, Env *env);
void gfx_eassign_obj(Node *target, Env *env, int k, double v, double old);
double *gfx_objmat(Node *e, Env *env, int *r, int *c);   /* obj.id.Position 등 — 그 행렬의 자리 */
int gfx_belong(Node *c, Env *env);
int gfx_seq_gate(Env *env);               /* 귀속 오브젝트가 꺼져 있으면 켜질 때까지 (S_OK · S_DEAD) */
int key_on(const char *n);
void gfx_init(void);
void gfx_tick_loop(double dt);            /* 메인 스레드: 밀린 시간만큼 내부 틱 */
int gfx_alive_ticker(void);
void gfx_end(void);

/* ---------- 오브젝트 ---------- */
enum { OK_RECT, OK_TEXT, OK_CIRCLE, OK_POLY, OK_NONE };
typedef struct Img Img;
typedef struct { Img *ref; const char *path; const char *tid; double x1, y1, x2, y2; } TexUse;
typedef struct Anime { const char *id; const char **frames; int nframes; double dur, start; int k; const char *nextId; double nextSp; int hasNext; Node *n; } Anime;
typedef struct BtnSt { long s; struct { int id; int on; } held[16]; int nheld; double rel[64]; int nrel; } BtnSt;
typedef struct Inst { const char *path; Env *env; Tk *tk; } Inst;
typedef struct Obj {
    const char *id;
    int kind;
    double pos[2], scale[2], vel[2];
    char color[8];
    int alpha;                 /* -1 = 지정 안 함 (255) */
    int hasOv; char ovColor[8]; int ovA;
    int hasGlow; char glowColor[8]; double glowS;
    double angle;
    char *text; double size; int outline;
    char *fitT; double fitS, fitW; int fitOk;
    double (*pts)[2]; int npts; double pbase[2];
    int hasCol; double colOff[2], colSize[2], colE; int colGrav;
    int hasTrig; double trigOff[2], trigSize[2]; int trigHold; int isTrig; const char *trigId;
    int anchored;
    const char *parent;
    int layer, hasLayer;
    const char **tags; int ntags;
    int off;
    int *scenes; int nscenes; int hasScenes;   /* -1 = '*' */
    int hasTex; TexUse tex;
    Anime *an;
    int ui;
    int hasNud; int nud[2];
    int btn; double btnSeen, relSeen; BtnSt *bst;
    int cam; double zoom; const char *follow;
    int spk;
    Inst **insts; int ninsts;
    long colN; const char *colWith;
    int dead;                  /* 지워졌거나 같은 id 로 다시 만들어짐 */
    void *rc;                  /* 그리기 캐시 (render.c) */
} Obj;
/* 그리기 목록 */
typedef struct { Obj **v; int n; int hasCam; double camX, camY, camZ; } DrawList;
void gfx_draw_list(DrawList *dl);
int gfx_live(Obj *o);
extern int GFX_SHOW_COL;
const char *gfx_bg_color(void);
int gfx_dis_on(void);

/* ---------- 화면 (render.c) ---------- */
int rd_open(void);
int rd_is_open(void);
void rd_set_bg(const char *hex);
void rd_size(double *w, double *h);        /* 화면 단위 (짧은 쪽 720) */
double rd_measure(const char *t, double size);
int rd_set_font(const char *path);
Img *rd_image(const char *path, int *w, int *h, int *failed);
Img *rd_cam_img(void);          /* 카메라 영상 (mag.wearCam) — 그릴 때마다 새 프레임을 올림 */
int rt_permit_ok(int k);         /* 0 = cam · 1 = mic · 2 = file — 허락됐으면 1 */
void jb_permit_cam_size(int *w, int *h);
void rd_poll(void);
void rd_paint(void);
void rd_present(void);
void rd_screen_scale(int on);
void rd_quit(void);
int rd_shot(const char *png);
/* 입력 */
typedef struct { double dx, dy, ux, uy, t; long f, df; } Click;
int in_key(const char *canon);
int in_clicked(void);
int in_mouse_in(void);
void in_mouse(double *x, double *y);
int in_clicks(Click **out);
typedef struct { long s; int id; char k; double x, y, t; } PtrEv;
int in_ptrlog(PtrEv **out);
int in_pointers(double (**xy)[3]);          /* [id, x, y] */

/* ---------- 소리 (audio.c) ---------- */
typedef struct Spk Spk;
void au_init(void);
int au_load(const char *path);              /* 미리 디코드 시작 · 실패하면 0 */
Spk *au_speaker(const char *id);
void au_play(Spk *k, const char *path, double ms, int loops, int hasPart, double a, double e, int intro);
void au_break(Spk *k, int noFade);
void au_set_vol(Spk *k, int v);
void au_set_fade(Spk *k, int in, int ms);
int au_vol(Spk *k);
void au_spatial(Spk *k, double gain, double pan);
void au_stop_all(void);
void au_quit(void);

#endif
