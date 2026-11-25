/* 화면 (SDL2) — 편집기 disUI 의 paint · textImg · texDraw · glowO 와 같은 모양으로 그림 · 입력 (키 · 마우스 · 터치)
 *   좌표: 화면 가운데 (0, 0) · 위쪽 + · 짧은 쪽이 720 단위
 */
#define _GNU_SOURCE
#include "rt.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "stb_image.h"
#include "stb_truetype.h"
#include "stb_image_write.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define D2R (M_PI / 180.0)
extern const unsigned char *proj_bytes(const char *path, size_t *len);

/* ---------- 이미지 ---------- */
struct Img { const char *path; unsigned char *px; int w, h; int loaded, failed; SDL_Texture *tex, *sil; const unsigned char *src; size_t srcLen; int owned; };
static int imgLoad(Img *im) {
    if (im->loaded) return !im->failed;
    im->loaded = 1;
    if (!im->px) {
        int n; im->px = stbi_load_from_memory(im->src, (int)im->srcLen, &im->w, &im->h, &n, 4);
        if (!im->px) { im->failed = 1; return 0; }
    }
    return 1;
}
Img *rd_image(const char *path, int *w, int *h, int *failed) {
    Img *im = jb_calloc(sizeof(Img)); im->path = path;
    size_t len; const unsigned char *b = proj_bytes(path, &len);
    if (!b) { *failed = 1; return im; }
    im->src = b; im->srcLen = len;
    int n;
    if (!stbi_info_from_memory(b, (int)len, &im->w, &im->h, &n)) { im->failed = 1; im->loaded = 1; *failed = 1; return im; }
    *w = im->w; *h = im->h; *failed = 0;
    return im;
}

/* ---------- 글꼴 ---------- */
typedef struct { stbtt_fontinfo info; unsigned char *data; int ok; int synthBold; } Font;
static Font userF, defF; static int defTried; static char userKey[512];
static int fontFromBytes(Font *f, const unsigned char *b, size_t len, int idx, int own) {
    (void)len;
    unsigned char *d = own ? (unsigned char *)b : jb_alloc(len); if (!own) memcpy(d, b, len);
    int off = stbtt_GetFontOffsetForIndex(d, idx);
    if (off < 0 || !stbtt_InitFont(&f->info, d, off)) return 0;
    f->data = d; f->ok = 1; return 1;
}
static unsigned char *readFile(const char *p, size_t *len) {
    FILE *f = fopen(p, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = jb_alloc(n + 1); *len = fread(b, 1, n, f); fclose(f); return b;
}
/* ttc 안에서 한글(가)을 가진 글꼴 (Noto CJK 는 KR 을 고름) */
static int ttcPickKR(unsigned char *d) {
    int n = stbtt_GetNumberOfFonts(d); int best = 0;
    for (int i = 0; i < n && i < 16; i++) {
        stbtt_fontinfo fi; int off = stbtt_GetFontOffsetForIndex(d, i); if (off < 0 || !stbtt_InitFont(&fi, d, off)) continue;
        int len; const char *nm = stbtt_GetFontNameString(&fi, &len, STBTT_PLATFORM_ID_MICROSOFT, STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, 1);
        if (nm) { char b[256] = ""; int k = 0; for (int q = 1; q < len && k < 255; q += 2) b[k++] = nm[q]; b[k] = 0; if (strstr(b, " KR") || strstr(b, "Korean")) return i; }
        if (stbtt_FindGlyphIndex(&fi, 0xAC00) && !best) best = i;
    }
    return best;
}
static void loadDefault(void) {
    if (defTried) return; defTried = 1;
    const char *envp = getenv("JB_FONT");
    char exe[1024]; snprintf(exe, sizeof exe, "%s/fonts/default.ttf", JB_EXE_DIR);
    const char *cands[] = {envp, exe,
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc", "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf", "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
        "/System/Library/Fonts/AppleSDGothicNeo.ttc", "/Library/Fonts/AppleGothic.ttf", "C:/Windows/Fonts/malgunbd.ttf", "C:/Windows/Fonts/malgun.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", NULL};
    for (int i = 0; i < (int)(sizeof cands / sizeof *cands); i++) {
        if (!cands[i]) continue;
        size_t len; unsigned char *b = readFile(cands[i], &len); if (!b) continue;
        int idx = stbtt_GetNumberOfFonts(b) > 1 ? ttcPickKR(b) : 0;
        if (fontFromBytes(&defF, b, len, idx, 1)) return;
        free(b);
    }
    fprintf(stderr, "⚠ 기본 글꼴을 찾지 못했습니다 — 글자는 dis.setFont 로 정한 글꼴로만 그립니다 (JB_FONT=경로.ttf 로 정할 수 있음)\n");
}
static int weightOf(Font *f) {
    unsigned int os2 = 0; const unsigned char *d = f->info.data; int nt = (d[f->info.fontstart + 4] << 8) | d[f->info.fontstart + 5];
    for (int i = 0; i < nt; i++) { const unsigned char *e = d + f->info.fontstart + 12 + 16 * i; if (!memcmp(e, "OS/2", 4)) os2 = (e[8] << 24) | (e[9] << 16) | (e[10] << 8) | e[11]; }
    if (!os2) return 400;
    return (d[os2 + 4] << 8) | d[os2 + 5];
}
int rd_set_font(const char *path) {
    if (!strcmp(userKey, path)) return 1;
    size_t len; const unsigned char *b = proj_bytes(path, &len); if (!b) return 0;
    Font f; memset(&f, 0, sizeof f);
    if (!fontFromBytes(&f, b, len, 0, 0)) return 0;
    f.synthBold = weightOf(&f) < 600;
    userF = f; snprintf(userKey, sizeof userKey, "%s", path);
    extern void rd_font_changed(void); rd_font_changed();
    return 1;
}
static Font *fontFor(int cp) {
    loadDefault();
    if (userF.ok && stbtt_FindGlyphIndex(&userF.info, cp)) return &userF;
    if (defF.ok && stbtt_FindGlyphIndex(&defF.info, cp)) return &defF;
    return userF.ok ? &userF : defF.ok ? &defF : NULL;
}
static int u8next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s; int c = *p;
    if (c < 0x80) { (*s)++; return c; }
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1; c &= 0x3F >> n;
    for (int i = 1; i <= n && p[i]; i++) c = (c << 6) | (p[i] & 0x3F);
    *s += n + 1; return c;
}
/* 한 줄의 폭 · 잉크 범위 (px 크기 = CSS font-size px) */
typedef struct { double width, inkL, inkR, asc, desc; } LineM;
static double emScale(Font *f, double px) { return stbtt_ScaleForMappingEmToPixels(&f->info, (float)px); }
static double boldExtra(Font *f, double px) {   /* 가짜 굵게 (Chrome · FreeType): 굵기 = 글꼴 크기 / 24 */
    if (!f->synthBold) return 0;
    return px / 24;
}
static LineM lineMeasure(const char *t, double px) {
    LineM m = {0, 1e9, -1e9, 0, 0}; double x = 0; int prev = 0; Font *pf = NULL;
    for (const char *s = t; *s;) {
        int cp = u8next(&s); Font *f = fontFor(cp); if (!f) continue;
        double sc = emScale(f, px);
        if (pf == f && prev) x += stbtt_GetCodepointKernAdvance(&f->info, prev, cp) * sc;
        int adv, lsb; stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
        int x0, y0, x1, y1;
        if (stbtt_GetCodepointBox(&f->info, cp, &x0, &y0, &x1, &y1)) {
            double eb = boldExtra(f, px) / 2;
            m.inkL = fmin(m.inkL, x + x0 * sc - eb); m.inkR = fmax(m.inkR, x + x1 * sc + eb);
            m.asc = fmax(m.asc, y1 * sc + eb); m.desc = fmax(m.desc, -y0 * sc + eb);
        }
        x += adv * sc; prev = cp; pf = f;
    }
    m.width = x; if (m.inkL > m.inkR) { m.inkL = 0; m.inkR = 0; }
    return m;
}
typedef struct { double k, asc, desc; } FK;
static FK fkCache; static int fkOk;
void rd_font_changed(void) { fkOk = 0; }
static FK fontK(void) {
    if (fkOk) return fkCache;
    LineM a = lineMeasure("H가", 100), d = lineMeasure("gjpy", 100);
    double asc = a.asc ? a.asc : 72, desc = d.desc ? d.desc : 20, h = fmax(1, asc + desc);
    fkCache.k = h / 100; fkCache.asc = asc / h; fkCache.desc = desc / h; fkOk = 1;
    return fkCache;
}
double rd_measure(const char *t, double size) { FK k = fontK(); return lineMeasure(t, size / k.k).width; }

/* 글자 그림: RGBA (위에서 아래) — 편집기 textImg 와 같은 크기 · 자리 */
typedef struct { SDL_Texture *tex; int W, H; double w, h; char *key; } TextImg;
static void dilate(float *m, int W, int H, double r) {
    if (r <= 0) return;
    int R = (int)ceil(r); float *o = jb_alloc(sizeof(float) * W * H);
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
        float best = 0;
        for (int dy = -R; dy <= R; dy++) { int yy = y + dy; if (yy < 0 || yy >= H) continue;
            for (int dx = -R; dx <= R; dx++) { int xx = x + dx; if (xx < 0 || xx >= W) continue;
                double d = sqrt((double)dx * dx + (double)dy * dy); if (d > r + 0.5) continue;
                float v = m[yy * W + xx]; if (d > r - 0.5) v *= (float)(r + 0.5 - d);
                if (v > best) best = v; } }
        o[y * W + x] = best;
    }
    memcpy(m, o, sizeof(float) * W * H); free(o);
}
static void drawLineMask(float *m, int W, int H, const char *t, double px, double cx, double baseline) {
    LineM lm = lineMeasure(t, px);
    double x = cx - lm.width / 2; int prev = 0; Font *pf = NULL;
    for (const char *s = t; *s;) {
        int cp = u8next(&s); Font *f = fontFor(cp); if (!f) continue;
        double sc = emScale(f, px);
        if (pf == f && prev) x += stbtt_GetCodepointKernAdvance(&f->info, prev, cp) * sc;
        int adv, lsb; stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
        int bw, bh, xo, yo; float fx = (float)(x - floor(x));
        unsigned char *bm = stbtt_GetCodepointBitmapSubpixel(&f->info, (float)sc, (float)sc, fx, 0, cp, &bw, &bh, &xo, &yo);
        if (bm) {
            int gx = (int)floor(x) + xo, gy = (int)floor(baseline + 0.5) + yo;
            double eb = boldExtra(f, px) / 2;
            if (eb > 0) {   /* 가짜 굵게: 이 글자만 조금 두껍게 */
                int pad = (int)ceil(eb) + 1, W2 = bw + pad * 2, H2 = bh + pad * 2;
                float *g = jb_calloc(sizeof(float) * W2 * H2);
                for (int yy = 0; yy < bh; yy++) for (int xx = 0; xx < bw; xx++) g[(yy + pad) * W2 + xx + pad] = bm[yy * bw + xx] / 255.0f;
                dilate(g, W2, H2, eb);
                for (int yy = 0; yy < H2; yy++) for (int xx = 0; xx < W2; xx++) { int X = gx - pad + xx, Y = gy - pad + yy; if (X < 0 || Y < 0 || X >= W || Y >= H) continue; float v = g[yy * W2 + xx]; float *d = &m[Y * W + X]; *d = *d + v - *d * v; }
                free(g);
            } else for (int yy = 0; yy < bh; yy++) for (int xx = 0; xx < bw; xx++) {
                int X = gx + xx, Y = gy + yy; if (X < 0 || Y < 0 || X >= W || Y >= H) continue;
                float v = bm[yy * bw + xx] / 255.0f; float *d = &m[Y * W + X]; *d = *d + v - *d * v;
            }
            stbtt_FreeBitmap(bm, NULL);
        }
        x += adv * sc; prev = cp; pf = f;
    }
}
static void hexRGB(const char *h, int *r, int *g, int *b) {
    const char *x = h[0] == '#' ? h + 1 : h; char t[7];
    if (strlen(x) == 3) { t[0] = t[1] = x[0]; t[2] = t[3] = x[1]; t[4] = t[5] = x[2]; t[6] = 0; } else snprintf(t, sizeof t, "%s", x);
    long n = strtol(t, NULL, 16); *r = n >> 16 & 255; *g = n >> 8 & 255; *b = n & 255;
}
/* 글자 가림 (mask) 을 만듦 — 크기는 편집기와 같은 W × H */
static float *textMask(const char *text, double sz, int outline, double s, int *pW, int *pH, float **outlineMask, double *bw, double *bh) {
    FK fk = fontK();
    int n = 1; for (const char *p = text; *p; p++) if (*p == '\n') n++;
    double hp = sz * s, px = hp / fk.k, lh = hp * 1.2, ol = outline ? fmax(2, sz / 8) * s : 0;
    double bl = hp * (fk.asc - fk.desc) / 2;
    double hw = 1, ext = hp / 2;
    char **ls = jb_alloc(sizeof(char*) * n); { const char *p = text; for (int i = 0; i < n; i++) { const char *e = strchr(p, '\n'); ls[i] = jb_strndup(p, e ? (size_t)(e - p) : strlen(p)); p = e ? e + 1 : p + strlen(p); } }
    for (int i = 0; i < n; i++) {
        LineM m = lineMeasure(ls[i], px);
        double abL = m.width / 2 - m.inkL, abR = m.inkR - m.width / 2;
        hw = fmax(hw, fmax(m.width / 2, fmax(abL, abR)));
        ext = fmax(ext, fmax(m.asc - bl, m.desc + bl));
    }
    double padY = ext + ol + 2;
    int W = (int)fmax(1, ceil(hw * 2 + ol * 2 + 4)), H = (int)fmax(1, ceil(lh * (n - 1) + padY * 2));
    if ((double)W * H > 16e6) { for (int i = 0; i < n; i++) free(ls[i]); free(ls); return NULL; }
    float *m = jb_calloc(sizeof(float) * W * H);
    double y0 = H / 2.0 - (n - 1) * lh / 2 + bl;
    for (int i = 0; i < n; i++) drawLineMask(m, W, H, ls[i], px, W / 2.0, y0 + i * lh);
    if (outlineMask) {
        if (ol > 0) { float *o = jb_alloc(sizeof(float) * W * H); memcpy(o, m, sizeof(float) * W * H); dilate(o, W, H, ol / 2); *outlineMask = o; }
        else *outlineMask = NULL;
    }
    for (int i = 0; i < n; i++) free(ls[i]); free(ls);
    *pW = W; *pH = H; *bw = W / s; *bh = H / s;
    return m;
}

/* ---------- 창 · 그리기 ---------- */
static SDL_Window *win; static SDL_Renderer *ren; static int opened, everOpened, sdlInit;
static double lastSize[2];
static char bgNow[8] = "#000";
static int screenScale = 1;
static void sdlStart(void) {
    if (sdlInit) return; sdlInit = 1;
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { fprintf(stderr, "화면을 열 수 없습니다: %s\n", SDL_GetError()); _exit(1); }
}
/* 창은 메인 스레드에서만 만들고 그림 (OpenGL 등은 스레드에 묶임) — rd_open 은 표시만 하고 다음 rd_poll 에서 만듦 */
static volatile int wantOpen;
int rd_open(void) { if (!opened) { opened = 1; wantOpen = 1; } return 1; }
static void reallyOpen(void) {
    wantOpen = 0;
    sdlStart();
    Uint32 fl = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | (RT_OPT.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    win = SDL_CreateWindow(P.name && *P.name ? P.name : "Jawbreaker", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, RT_OPT.winW, RT_OPT.winH, fl);
    if (!win) { fprintf(stderr, "창을 만들 수 없습니다: %s\n", SDL_GetError()); _exit(1); }
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) { fprintf(stderr, "그리기를 시작할 수 없습니다: %s\n", SDL_GetError()); _exit(1); }
    SDL_SetRenderDrawBlendMode(ren, SDL_BLENDMODE_BLEND);
    everOpened = 1;
}
int rd_is_open(void) { return opened; }
void rd_set_bg(const char *hex) { snprintf(bgNow, sizeof bgNow, "%s", hex); }
static void pixSize(int *w, int *h) { if (ren) SDL_GetRendererOutputSize(ren, w, h); else { *w = RT_OPT.winW; *h = RT_OPT.winH; } }
static double unitPx(void) { int w, h; pixSize(&w, &h); double m = w < h ? w : h; return m > 0 ? m / 720 : 1; }
void rd_size(double *w, double *h) {
    if (opened) { int pw, ph; pixSize(&pw, &ph); double u = unitPx(); lastSize[0] = floor(pw / u + 0.5); lastSize[1] = floor(ph / u + 0.5); }
    *w = lastSize[0]; *h = lastSize[1];
}
void rd_screen_scale(int on) { screenScale = on; }

/* 그리기 변환 */
static double TX_d, TX_z, TX_cx, TX_cy, TX_CX, TX_CY;
static inline void w2s(double x, double y, float *ox, float *oy) { *ox = (float)(TX_CX + (x - TX_cx) * TX_z * TX_d); *oy = (float)(TX_CY - (y - TX_cy) * TX_z * TX_d); }
static void setView(int ui, DrawList *dl) {
    int w, h; pixSize(&w, &h); TX_d = unitPx(); TX_CX = w / 2.0; TX_CY = h / 2.0;
    if (ui || !dl->hasCam) { TX_z = 1; TX_cx = 0; TX_cy = 0; } else { TX_z = dl->camZ; TX_cx = dl->camX; TX_cy = dl->camY; }
}
static SDL_Color col(const char *hex, double a) { int r, g, b; hexRGB(hex, &r, &g, &b); SDL_Color c = {(Uint8)r, (Uint8)g, (Uint8)b, (Uint8)fmax(0, fmin(255, floor(a * 255 + 0.5)))}; return c; }
static void fillPoly(const double (*pts)[2], int n, SDL_Color c) {   /* 볼록 · 오목 모두 — 부채꼴 삼각형 (편집기는 짝홀 규칙) */
    if (n < 3) return;
    SDL_Vertex *v = jb_alloc(sizeof(SDL_Vertex) * n); int *idx = jb_alloc(sizeof(int) * 3 * (n - 2));
    for (int i = 0; i < n; i++) { w2s(pts[i][0], pts[i][1], &v[i].position.x, &v[i].position.y); v[i].color = c; v[i].tex_coord.x = v[i].tex_coord.y = 0; }
    for (int i = 0; i < n - 2; i++) { idx[i * 3] = 0; idx[i * 3 + 1] = i + 1; idx[i * 3 + 2] = i + 2; }
    SDL_RenderGeometry(ren, NULL, v, n, idx, 3 * (n - 2));
    free(v); free(idx);
}
static void rotPt(double px, double py, double rot, double x, double y, double *ox, double *oy) { double c = cos(rot), s = sin(rot), dx = x - px, dy = y - py; *ox = px + dx * c - dy * s; *oy = py + dx * s + dy * c; }
static void fillRectW(double px, double py, double sw, double sh, double rot, SDL_Color c) {
    if (!rot) {
        float x0, y0, x1, y1; w2s(px - sw / 2, py + sh / 2, &x0, &y0); w2s(px + sw / 2, py - sh / 2, &x1, &y1);
        SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a); SDL_FRect r = {x0, y0, x1 - x0, y1 - y0}; SDL_RenderFillRectF(ren, &r); return;
    }
    double p[4][2]; double cs[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (int i = 0; i < 4; i++) rotPt(px, py, rot, px + cs[i][0] * sw / 2, py + cs[i][1] * sh / 2, &p[i][0], &p[i][1]);
    fillPoly((const double (*)[2])p, 4, c);
}
static void fillEllipse(double px, double py, double sw, double sh, double rot, SDL_Color c) {
    int seg = (int)fmin(256, fmax(24, (sw + sh) * TX_d * TX_z / 4));
    double (*p)[2] = jb_alloc(sizeof(double) * 2 * seg);
    for (int i = 0; i < seg; i++) { double a = 2 * M_PI * i / seg; rotPt(px, py, rot, px + cos(a) * sw / 2, py + sin(a) * sh / 2, &p[i][0], &p[i][1]); }
    fillPoly((const double (*)[2])p, seg, c); free(p);
}
static SDL_Texture *imgTex(Img *im) {
    if (im->tex) return im->tex;
    if (!imgLoad(im)) return NULL;
    im->tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, im->w, im->h);
    if (!im->tex) return NULL;
    SDL_UpdateTexture(im->tex, NULL, im->px, im->w * 4);
    SDL_SetTextureBlendMode(im->tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(im->tex, SDL_ScaleModeNearest);
    return im->tex;
}
static SDL_Texture *imgSil(Img *im) {   /* 모양 (불투명한 곳) 만 흰색 — 오버레이 · 빛 번짐 */
    if (im->sil) return im->sil;
    if (!imgLoad(im)) return NULL;
    unsigned char *p = jb_alloc((size_t)im->w * im->h * 4);
    for (int i = 0; i < im->w * im->h; i++) { p[i * 4] = p[i * 4 + 1] = p[i * 4 + 2] = 255; p[i * 4 + 3] = im->px[i * 4 + 3]; }
    im->sil = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, im->w, im->h);
    if (im->sil) { SDL_UpdateTexture(im->sil, NULL, p, im->w * 4); SDL_SetTextureBlendMode(im->sil, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(im->sil, SDL_ScaleModeNearest); }
    free(p); return im->sil;
}
/* 텍스쳐: 이미지의 [x1, y1]~[x2, y2] 를 오브젝트 크기로 (뒤집기 · 원은 타원으로 자름) */
static void texDraw(Obj *o, double px, double py, double sw, double sh, double rot, SDL_Texture *t, Img *im, SDL_Color mod) {
    double sx = fmin(o->tex.x1, o->tex.x2), sy = fmin(o->tex.y1, o->tex.y2), rw = fabs(o->tex.x2 - o->tex.x1), rh = fabs(o->tex.y2 - o->tex.y1);
    int fx = o->tex.x2 < o->tex.x1, fy = o->tex.y2 < o->tex.y1;
    SDL_SetTextureColorMod(t, mod.r, mod.g, mod.b); SDL_SetTextureAlphaMod(t, mod.a);
    /* 그림 가운데 = o->pos (편집기는 기준점 없이 오브젝트 위치에 그림) · 돌리는 축 = 기준점 자리 (px, py) */
    double cx = o->pos[0], cy = o->pos[1];
    int iw = im->w, ih = im->h;
    if (o->kind == OK_CIRCLE) {
        int seg = 64; SDL_Vertex *v = jb_alloc(sizeof(SDL_Vertex) * (seg + 1)); int *idx = jb_alloc(sizeof(int) * seg * 3);
        for (int i = 0; i <= seg; i++) {
            double a = i == seg ? 0 : 2 * M_PI * i / seg, ux = i == seg ? 0 : cos(a), uy = i == seg ? 0 : sin(a);
            double wx, wy; rotPt(px, py, rot, cx + ux * sw / 2, cy + uy * sh / 2, &wx, &wy);
            w2s(wx, wy, &v[i].position.x, &v[i].position.y); v[i].color.r = v[i].color.g = v[i].color.b = 255; v[i].color.a = 255;
            double tu = (ux + 1) / 2, tv = (1 - uy) / 2; if (fx) tu = 1 - tu; if (fy) tv = 1 - tv;
            v[i].tex_coord.x = (float)((sx + tu * rw) / iw); v[i].tex_coord.y = (float)((sy + tv * rh) / ih);
        }
        for (int i = 0; i < seg; i++) { idx[i * 3] = seg; idx[i * 3 + 1] = i; idx[i * 3 + 2] = (i + 1) % seg; }
        SDL_RenderGeometry(ren, t, v, seg + 1, idx, seg * 3); free(v); free(idx); return;
    }
    double cs[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}};   /* 왼쪽 위 → 시계 방향 */
    double uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    SDL_Vertex v[4]; int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 4; i++) {
        double wx, wy; rotPt(px, py, rot, cx + cs[i][0] * sw / 2, cy + cs[i][1] * sh / 2, &wx, &wy);
        w2s(wx, wy, &v[i].position.x, &v[i].position.y); v[i].color.r = v[i].color.g = v[i].color.b = 255; v[i].color.a = 255;
        double tu = uv[i][0], tv = uv[i][1]; if (fx) tu = 1 - tu; if (fy) tv = 1 - tv;
        v[i].tex_coord.x = (float)((sx + tu * rw) / iw); v[i].tex_coord.y = (float)((sy + tv * rh) / ih);
    }
    if (!rot && !fx && !fy) {   /* 가장 흔한 경우: 픽셀이 칸에 맞게 */
        SDL_FRect dst = {v[0].position.x, v[0].position.y, v[2].position.x - v[0].position.x, v[2].position.y - v[0].position.y};
        SDL_Rect src = {(int)sx, (int)sy, (int)ceil(rw), (int)ceil(rh)};
        if (src.x == sx && src.y == sy && src.w == rw && src.h == rh) { SDL_RenderCopyF(ren, t, &src, &dst); return; }
    }
    SDL_RenderGeometry(ren, t, v, 4, idx, 6);
}
/* 오브젝트 그리기 캐시 */
typedef struct { TextImg ti, tov; struct { SDL_Texture *t; char *key; double w, h, ox, oy; } glow; } RC;
static RC *rcOf(Obj *o) { if (!o->rc) o->rc = jb_calloc(sizeof(RC)); return o->rc; }
static void textImgMake(TextImg *T, const char *text, double sz, const char *color, int outline, double s) {
    char *key = jb_fmt("%s\x01%.6g\x01%s\x01%d\x01%s\x01%.6g", text, sz, color, outline, userKey, s);
    if (T->tex && T->key && !strcmp(T->key, key)) { free(key); return; }
    if (T->tex) { SDL_DestroyTexture(T->tex); T->tex = NULL; }
    free(T->key); T->key = key;
    int W, H; float *om = NULL; double bw, bh;
    float *m = textMask(text, sz, outline, s, &W, &H, &om, &bw, &bh);
    if (!m) return;
    int r, g, b; hexRGB(color, &r, &g, &b);
    double lum = (0.299 * r + 0.587 * g + 0.114 * b) / 255; int orr = lum > 0.55 ? 0 : 255;
    unsigned char *px = jb_alloc((size_t)W * H * 4);
    for (int i = 0; i < W * H; i++) {
        float fa = m[i], oa = om ? om[i] : 0;
        /* 외곽선 위에 글자 (source-over) */
        float A = fa + oa * (1 - fa);
        float R = A > 0 ? (r * fa + orr * oa * (1 - fa)) / A : 0, G = A > 0 ? (g * fa + orr * oa * (1 - fa)) / A : 0, B = A > 0 ? (b * fa + orr * oa * (1 - fa)) / A : 0;
        px[i * 4] = (unsigned char)R; px[i * 4 + 1] = (unsigned char)G; px[i * 4 + 2] = (unsigned char)B; px[i * 4 + 3] = (unsigned char)(A * 255 + 0.5f);
    }
    T->tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, W, H);
    if (T->tex) { SDL_UpdateTexture(T->tex, NULL, px, W * 4); SDL_SetTextureBlendMode(T->tex, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(T->tex, SDL_ScaleModeLinear); }
    T->W = W; T->H = H; T->w = bw; T->h = bh;
    free(px); free(m); free(om);
}
static void drawTexQuad(SDL_Texture *t, double px, double py, double w, double h, double rot, Uint8 a) {
    SDL_SetTextureAlphaMod(t, a); SDL_SetTextureColorMod(t, 255, 255, 255);
    if (!rot) { float x0, y0, x1, y1; w2s(px - w / 2, py + h / 2, &x0, &y0); w2s(px + w / 2, py - h / 2, &x1, &y1); SDL_FRect d = {x0, y0, x1 - x0, y1 - y0}; SDL_RenderCopyF(ren, t, NULL, &d); return; }
    double cs[4][2] = {{-1, 1}, {1, 1}, {1, -1}, {-1, -1}}, uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    SDL_Vertex v[4]; int idx[6] = {0, 1, 2, 0, 2, 3};
    for (int i = 0; i < 4; i++) { double wx, wy; rotPt(px, py, rot, px + cs[i][0] * w / 2, py + cs[i][1] * h / 2, &wx, &wy); w2s(wx, wy, &v[i].position.x, &v[i].position.y); v[i].color.r = v[i].color.g = v[i].color.b = v[i].color.a = 255; v[i].tex_coord.x = (float)uv[i][0]; v[i].tex_coord.y = (float)uv[i][1]; }
    SDL_RenderGeometry(ren, t, v, 4, idx, 6);
}
/* 빛 번짐: 모양의 가림을 화면 픽셀로 만들어 흐리게 (σ = B / 2) 하고 그 색으로 두 번 */
static void boxBlur(float *a, float *tmp, int W, int H, int r) {
    if (r < 1) return;
    for (int y = 0; y < H; y++) { float acc = 0; float *row = a + y * W, *o = tmp + y * W;
        for (int x = -r; x < W + r; x++) { if (x + r < W && x + r >= 0) acc += row[x + r]; if (x - r - 1 >= 0 && x - r - 1 < W) acc -= row[x - r - 1]; if (x >= 0 && x < W) o[x] = acc / (2 * r + 1); } }
    for (int x = 0; x < W; x++) { float acc = 0;
        for (int y = -r; y < H + r; y++) { if (y + r < H && y + r >= 0) acc += tmp[(y + r) * W + x]; if (y - r - 1 >= 0 && y - r - 1 < H) acc -= tmp[(y - r - 1) * W + x]; if (y >= 0 && y < H) a[y * W + x] = acc / (2 * r + 1); } }
}
static void glowDraw(Obj *o, double px, double py, double sw, double sh, double rot, double al, double sc) {
    RC *rc = rcOf(o);
    double B = fmin(600, o->glowS * sc * 2 / 3), sigma = B / 2;
    char *key = jb_fmt("%d|%.4f|%.4f|%.4f|%.4f|%s|%.3f|%s|%s|%.3f,%.3f,%.3f,%.3f|%d|%s",
        o->kind, sw, sh, rot, sc, o->glowColor, o->glowS, o->text ? o->text : "", o->hasTex && o->tex.path ? o->tex.path : "", o->tex.x1, o->tex.y1, o->tex.x2, o->tex.y2, o->npts, o->color);
    if (!rc->glow.t || strcmp(rc->glow.key, key)) {
        if (rc->glow.t) SDL_DestroyTexture(rc->glow.t);
        rc->glow.t = NULL; free(rc->glow.key); rc->glow.key = key; key = NULL;
        /* 모양의 화면 픽셀 범위 */
        double ex = sw / 2, ey = sh / 2; double textW = 0, textH = 0; float *tm = NULL; int TW = 0, TH = 0;
        if (o->kind == OK_TEXT) { double bw, bh; tm = textMask(o->text, fabs(o->size), o->outline, sc, &TW, &TH, NULL, &bw, &bh); textW = bw; textH = bh; ex = bw / 2; ey = bh / 2; }
        double r = (rot || o->kind == OK_POLY) ? hypot(ex, ey) : 0; if (r) { ex = ey = r; }
        int pad = (int)ceil(sigma * 3) + 2;
        int W = (int)ceil(ex * 2 * sc) + pad * 2, H = (int)ceil(ey * 2 * sc) + pad * 2;
        if ((double)W * H > 8e6) { free(tm); return; }
        float *m = jb_calloc(sizeof(float) * W * H), *tmp = jb_alloc(sizeof(float) * W * H);
        double ox = W / 2.0, oy = H / 2.0;   /* 가운데 = 오브젝트 위치 */
        double c = cos(rot), s_ = sin(rot);
        Img *im = o->hasTex ? o->tex.ref : NULL; if (im) imgLoad(im);
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            double wx = (x + 0.5 - ox) / sc, wy = (oy - y - 0.5) / sc;   /* 오브젝트 기준 (위쪽 +) */
            double lx = wx * c + wy * s_, ly = -wx * s_ + wy * c;          /* 돌림 되돌림 */
            float v = 0;
            if (o->kind == OK_TEXT) {
                int X = (int)floor((lx + textW / 2) * sc), Y = (int)floor((textH / 2 - ly) * sc);
                if (X >= 0 && Y >= 0 && X < TW && Y < TH) v = tm[Y * TW + X];
            } else if (im && !im->failed) {
                if (fabs(lx) <= sw / 2 && fabs(ly) <= sh / 2 && (o->kind != OK_CIRCLE || (lx * lx) / (sw * sw / 4) + (ly * ly) / (sh * sh / 4) <= 1)) {
                    double tu = (lx + sw / 2) / sw, tv = (sh / 2 - ly) / sh;
                    if (o->tex.x2 < o->tex.x1) tu = 1 - tu; if (o->tex.y2 < o->tex.y1) tv = 1 - tv;
                    int X = (int)floor(fmin(o->tex.x1, o->tex.x2) + tu * fabs(o->tex.x2 - o->tex.x1)), Y = (int)floor(fmin(o->tex.y1, o->tex.y2) + tv * fabs(o->tex.y2 - o->tex.y1));
                    if (X >= 0 && Y >= 0 && X < im->w && Y < im->h) v = im->px[(Y * im->w + X) * 4 + 3] / 255.0f;
                }
            } else if (o->kind == OK_CIRCLE) { if (sw > 0 && sh > 0 && (lx * lx) / (sw * sw / 4) + (ly * ly) / (sh * sh / 4) <= 1) v = 1; }
            else if (o->kind == OK_POLY) {
                double fx = o->scale[0] / o->pbase[0], fy = o->scale[1] / o->pbase[1]; int inn = 0;
                for (int i = 0, j = o->npts - 1; i < o->npts; j = i++) { double ax = o->pts[i][0] * fx, ay = o->pts[i][1] * fy, bx = o->pts[j][0] * fx, by = o->pts[j][1] * fy; if ((ay > wy) != (by > wy) && wx < (bx - ax) * (wy - ay) / (by - ay) + ax) inn = !inn; }
                v = inn;
            } else if (fabs(lx) <= sw / 2 && fabs(ly) <= sh / 2) v = 1;
            m[y * W + x] = v;
        }
        /* 가우스 ≈ 상자 흐림 3 번 */
        int br = (int)floor(sqrt(12 * sigma * sigma / 3 + 1) / 2);
        for (int k = 0; k < 3; k++) boxBlur(m, tmp, W, H, br);
        int R, G, Bc; hexRGB(o->glowColor, &R, &G, &Bc);
        unsigned char *pxs = jb_alloc((size_t)W * H * 4);
        for (int i = 0; i < W * H; i++) { pxs[i * 4] = (unsigned char)R; pxs[i * 4 + 1] = (unsigned char)G; pxs[i * 4 + 2] = (unsigned char)Bc; float a = m[i]; pxs[i * 4 + 3] = (unsigned char)(fmin(1, a) * 255 + 0.5f); }
        rc->glow.t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, W, H);
        if (rc->glow.t) { SDL_UpdateTexture(rc->glow.t, NULL, pxs, W * 4); SDL_SetTextureBlendMode(rc->glow.t, SDL_BLENDMODE_BLEND); SDL_SetTextureScaleMode(rc->glow.t, SDL_ScaleModeLinear); }
        rc->glow.w = W / sc; rc->glow.h = H / sc;
        free(pxs); free(m); free(tmp); free(tm);
    }
    free(key);
    if (!rc->glow.t) return;
    double cx = o->kind == OK_TEXT ? px : o->hasTex ? o->pos[0] : px, cy = o->kind == OK_TEXT ? py : o->hasTex ? o->pos[1] : py;
    for (int k = 0; k < 2; k++) drawTexQuad(rc->glow.t, cx, cy, rc->glow.w, rc->glow.h, 0, (Uint8)fmax(0, fmin(255, al * 255 + 0.5)));
}
static void drawO(Obj *o, double vx, double vy, double vw, double vh, int ui) {
    if (o->kind == OK_NONE || o->alpha == 0) return;
    double px = o->pos[0], py = o->pos[1], sw = fabs(o->scale[0]), sh = fabs(o->scale[1]);
    int lines = 1; if (o->kind == OK_TEXT) for (const char *p = o->text; *p; p++) if (*p == '\n') lines++;
    if (o->hasNud) { px += o->nud[0] * sw / 2; py += o->nud[1] * (o->kind == OK_TEXT ? sh * (1 + 1.2 * (lines - 1)) : sh) / 2; }
    {
        double ex = sw / 2, ey = (o->kind == OK_TEXT ? (fabs(o->size) ? fabs(o->size) : sh) * (1 + 1.2 * (lines - 1)) : sh) / 2;
        if (o->kind == OK_TEXT) { ex += fabs(o->size) * 0.4; ey += fabs(o->size) * 0.4; }
        if (o->angle || o->kind == OK_POLY) { double r = hypot(ex, ey); ex = r; ey = r; }
        if (fabs(px - vx) - ex > vw || fabs(py - vy) - ey > vh) return;
    }
    double al = o->alpha < 0 ? 1 : o->alpha / 255.0;
    double rot = o->angle ? o->angle * D2R : 0;
    int ov = o->hasOv && o->ovA > 0; double oa = ov ? al * o->ovA / 255.0 : 0;
    double sc = TX_d * (ui ? 1 : TX_z);
    if (o->hasGlow && o->glowS > 0) glowDraw(o, px, py, sw, sh, rot, al, sc);
    if (o->kind == OK_TEXT) {
        RC *rc = rcOf(o);
        textImgMake(&rc->ti, o->text, fabs(o->size), o->color, o->outline, sc);
        if (!rc->ti.tex) return;
        drawTexQuad(rc->ti.tex, px, py, rc->ti.w, rc->ti.h, rot, (Uint8)(al * 255 + 0.5));
        if (ov) { textImgMake(&rc->tov, o->text, fabs(o->size), o->ovColor, o->outline, sc); if (rc->tov.tex) drawTexQuad(rc->tov.tex, px, py, rc->tov.w, rc->tov.h, rot, (Uint8)(oa * 255 + 0.5)); }
        return;
    }
    if (o->hasTex && o->tex.ref) {
        Img *im = o->tex.ref; SDL_Texture *t = imgTex(im);
        if (t) {
            SDL_Color m = col(o->color, al); texDraw(o, px, py, sw, sh, rot, t, im, m);
            if (ov) { SDL_Texture *s = imgSil(im); if (s) { SDL_Color m2 = col(o->ovColor, oa); texDraw(o, px, py, sw, sh, rot, s, im, m2); } }
        }
        return;
    }
    for (int pv = 0; pv < (ov ? 2 : 1); pv++) {
        SDL_Color c = pv ? col(o->ovColor, oa) : col(o->color, al);
        if (o->kind == OK_CIRCLE) fillEllipse(px, py, sw, sh, rot, c);
        else if (o->kind == OK_POLY) {
            double fx = o->scale[0] / o->pbase[0], fy = o->scale[1] / o->pbase[1];
            double (*p)[2] = jb_alloc(sizeof(double) * 2 * o->npts);
            for (int i = 0; i < o->npts; i++) { double x = px + o->pts[i][0] * fx, y = py + o->pts[i][1] * fy; if (rot) rotPt(px, py, rot, x, y, &x, &y); p[i][0] = x; p[i][1] = y; }
            fillPoly((const double (*)[2])p, o->npts, c); free(p);
        } else fillRectW(px, py, sw, sh, rot, c);
    }
}
static void strokeBox(Obj *o, const double *off, const double *size, SDL_Color c) {
    double w = fabs(size[0]), h = fabs(size[1]), r = o->angle * D2R;
    double cs[5][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}, {-1, -1}}; SDL_FPoint p[5];
    for (int i = 0; i < 5; i++) { double x = off[0] + cs[i][0] * w / 2, y = off[1] + cs[i][1] * h / 2, X = o->pos[0] + x * cos(r) - y * sin(r), Y = o->pos[1] + x * sin(r) + y * cos(r); w2s(X, Y, &p[i].x, &p[i].y); }
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a); SDL_RenderDrawLinesF(ren, p, 5);
}
static double errShown;
void rd_paint(void) {
    if (!opened || !ren) return;
    int r, g, b; hexRGB(bgNow, &r, &g, &b);
    SDL_SetRenderDrawColor(ren, (Uint8)r, (Uint8)g, (Uint8)b, 255); SDL_RenderClear(ren);
    DrawList dl; gfx_draw_list(&dl);
    int w, h; pixSize(&w, &h);
    setView(0, &dl);
    double vx = TX_cx, vy = TX_cy, vw = w / 2.0 / TX_d / TX_z, vh = h / 2.0 / TX_d / TX_z;
    int anyUi = 0;
    for (int i = 0; i < dl.n; i++) { if (dl.v[i]->ui) { anyUi = 1; continue; } drawO(dl.v[i], vx, vy, vw, vh, 0); }
    if (anyUi) {
        setView(1, &dl); vx = 0; vy = 0; vw = w / 2.0 / TX_d; vh = h / 2.0 / TX_d;
        for (int i = 0; i < dl.n; i++) if (dl.v[i]->ui) drawO(dl.v[i], vx, vy, vw, vh, 1);
        setView(0, &dl);
    }
    if (GFX_SHOW_COL) {
        for (int i = 0; i < dl.n; i++) { Obj *o = dl.v[i]; if (o->hasCol && !o->ui) { SDL_Color c = {0x4d, 0xe2, 0xb4, 255}; strokeBox(o, o->colOff, o->colSize, c); } }
        for (int i = 0; i < dl.n; i++) { Obj *o = dl.v[i]; if (o->hasTrig && !o->ui) { SDL_Color c = {0xff, 0xd1, 0x66, (Uint8)(o->isTrig ? 255 : 153)}; strokeBox(o, o->trigOff, o->trigSize, c); } }
    }
    (void)errShown;
}
void rd_present(void) { if (opened && ren) SDL_RenderPresent(ren); }
int rd_shot(const char *png) {
    if (!opened || !ren) return 0;
    int w, h; pixSize(&w, &h);
    unsigned char *px = jb_alloc((size_t)w * h * 4);
    if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ABGR8888, px, w * 4)) { free(px); return 0; }
    int ok = stbi_write_png(png, w, h, 4, px, w * 4); free(px); return ok;
}
void rd_quit(void) { if (sdlInit) SDL_Quit(); }
/* 오류: 창이 있거나 묶은 게임(EXE)이면 메시지 상자로도 보여 줌 */
void rt_show_error(const char *file, int line, const char *msg) {
    if (!RT_OPT.box && !win) return;
    char *t = file && *file ? jb_fmt("%s:%d\n%s", file, line, msg) : jb_strdup(msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, P.name && *P.name ? P.name : "Jawbreaker", t, win);
}

/* ---------- 타일맵 (.tl JSON) ---------- */
typedef struct J { int t; double n; char *s; struct J **a; char **k; int na; } J;   /* t: 0 null 1 num 2 str 3 arr 4 obj 5 bool */
static const char *jp;
static void jws(void) { while (*jp == ' ' || *jp == '\t' || *jp == '\n' || *jp == '\r') jp++; }
static J *jv(void);
static char *jstr(void) {
    if (*jp != '"') return NULL; jp++;
    size_t cap = 64, n = 0; char *s = jb_alloc(cap);
    while (*jp && *jp != '"') {
        if (n + 8 > cap) { cap *= 2; s = jb_realloc(s, cap); }
        if (*jp == '\\') { jp++; char e = *jp++; if (e == 'n') s[n++] = '\n'; else if (e == 't') s[n++] = '\t'; else if (e == 'u') { jp += 4; s[n++] = '?'; } else s[n++] = e; }
        else s[n++] = *jp++;
    }
    if (*jp == '"') jp++;
    s[n] = 0; return s;
}
static J *jv(void) {
    jws(); J *j = jb_calloc(sizeof(J));
    if (*jp == '{') {
        jp++; j->t = 4; jws();
        if (*jp == '}') { jp++; return j; }
        for (;;) {
            jws(); char *k = jstr(); if (!k) return NULL; jws(); if (*jp != ':') return NULL; jp++;
            J *v = jv(); if (!v) return NULL;
            j->a = jb_realloc(j->a, sizeof(J*) * (j->na + 1)); j->k = jb_realloc(j->k, sizeof(char*) * (j->na + 1)); j->a[j->na] = v; j->k[j->na] = k; j->na++;
            jws(); if (*jp == ',') { jp++; continue; } if (*jp == '}') { jp++; break; } return NULL;
        }
        return j;
    }
    if (*jp == '[') {
        jp++; j->t = 3; jws();
        if (*jp == ']') { jp++; return j; }
        for (;;) { J *v = jv(); if (!v) return NULL; j->a = jb_realloc(j->a, sizeof(J*) * (j->na + 1)); j->a[j->na++] = v; jws(); if (*jp == ',') { jp++; continue; } if (*jp == ']') { jp++; break; } return NULL; }
        return j;
    }
    if (*jp == '"') { j->t = 2; j->s = jstr(); return j->s ? j : NULL; }
    if (!strncmp(jp, "true", 4)) { jp += 4; j->t = 5; j->n = 1; return j; }
    if (!strncmp(jp, "false", 5)) { jp += 5; j->t = 5; j->n = 0; return j; }
    if (!strncmp(jp, "null", 4)) { jp += 4; j->t = 0; return j; }
    char *e; j->n = strtod(jp, &e); if (e == jp) return NULL; jp = e; j->t = 1; return j;
}
static J *jget(J *o, const char *k) { if (!o || o->t != 4) return NULL; for (int i = 0; i < o->na; i++) if (!strcmp(o->k[i], k)) return o->a[i]; return NULL; }
static unsigned char *b64dec(const char *s, size_t *len) {
    static int T[256]; static int init;
    if (!init) { for (int i = 0; i < 256; i++) T[i] = -1; const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"; for (int i = 0; A[i]; i++) T[(unsigned char)A[i]] = i; T['-'] = 62; T['_'] = 63; init = 1; }
    size_t L = strlen(s); unsigned char *o = jb_alloc(L * 3 / 4 + 4); size_t n = 0; uint32_t acc = 0; int bits = 0;
    for (size_t i = 0; i < L; i++) { int v = T[(unsigned char)s[i]]; if (v < 0) continue; acc = acc << 6 | (uint32_t)v; bits += 6; if (bits >= 8) { bits -= 8; o[n++] = (unsigned char)(acc >> bits & 255); } }
    *len = n; return o;
}
typedef struct { int w, h; double ts; int ntex; char **texData; int *texCol, *texClimb; char **texName; int nl; int *lvis; int **ltiles; int hasZero, zero[2]; Img **layerImg; } TL;
TL *tl_parse(const char *path, const char *text, char *err, size_t en) {
    jp = text; J *j = jv();
    if (!j) { snprintf(err, en, "'%s' 은(는) 올바른 타일맵 파일이 아닙니다 (JSON 이 아님)", path); return NULL; }
    J *fmt = jget(j, "format"), *layers = jget(j, "layers"), *texs = jget(j, "textures");
    if (!fmt || fmt->t != 2 || strcmp(fmt->s, "jawbreaker-tilemap") || !layers || layers->t != 3 || !texs || texs->t != 3) { snprintf(err, en, "'%s' 은(는) 타일맵 에디터의 .tl 파일이 아닙니다", path); return NULL; }
    TL *t = jb_calloc(sizeof(TL));
    J *jw = jget(j, "width"), *jh = jget(j, "height"), *jt = jget(j, "tileSize");
    t->w = jw && jw->t == 1 ? (int)jw->n : 0; t->h = jh && jh->t == 1 ? (int)jh->n : 0; t->ts = jt && jt->t == 1 ? jt->n : jt && jt->t == 2 ? atof(jt->s) : 0;
    if (!(t->w > 0 && t->h > 0 && t->ts > 0)) { snprintf(err, en, "'%s' 의 크기가 올바르지 않습니다", path); return NULL; }
    t->ntex = texs->na; t->texData = jb_calloc(sizeof(char*) * (t->ntex + 1)); t->texCol = jb_calloc(sizeof(int) * (t->ntex + 1)); t->texClimb = jb_calloc(sizeof(int) * (t->ntex + 1)); t->texName = jb_calloc(sizeof(char*) * (t->ntex + 1));
    int miss = 0;
    for (int i = 0; i < t->ntex; i++) {
        J *x = texs->a[i], *d = jget(x, "data"), *c = jget(x, "collider"), *cl = jget(x, "climb"), *nm = jget(x, "name");
        t->texData[i] = d && d->t == 2 ? d->s : NULL; if (!t->texData[i]) miss++;
        t->texCol[i] = c && ((c->t == 5 || c->t == 1) ? c->n != 0 : c->t == 2 ? *c->s != 0 : c->t >= 3);
        t->texClimb[i] = cl && ((cl->t == 5 || cl->t == 1) ? cl->n != 0 : cl->t == 2 ? *cl->s != 0 : cl->t >= 3);
        t->texName[i] = nm && nm->t == 2 ? nm->s : "";
    }
    if (miss) fprintf(stderr, "⚠ %s: 이미지가 없는 텍스쳐 %d개 — 타일맵 에디터에서 다시 저장하세요\n", path, miss);
    t->nl = layers->na; t->lvis = jb_calloc(sizeof(int) * (t->nl + 1)); t->ltiles = jb_calloc(sizeof(int*) * (t->nl + 1)); t->layerImg = jb_calloc(sizeof(Img*) * (t->nl + 1));
    for (int li = 0; li < t->nl; li++) {
        J *l = layers->a[li], *v = jget(l, "visible"), *tl = jget(l, "tiles");
        t->lvis[li] = !(v && v->t == 5 && v->n == 0);
        if (!tl || tl->t != 3 || tl->na != t->w * t->h) { snprintf(err, en, "'%s' 레이어 %d 의 칸 수가 %d×%d 와 다릅니다", path, li, t->w, t->h); return NULL; }
        t->ltiles[li] = jb_alloc(sizeof(int) * t->w * t->h);
        for (int i = 0; i < t->w * t->h; i++) { J *x = tl->a[i]; double q = x->t == 1 ? x->n : -1; t->ltiles[li][i] = (q == floor(q) && q >= 0 && q < t->ntex) ? (int)q : -1; }
    }
    J *z = jget(j, "zero");
    if (z && z->t == 3 && z->na >= 2 && z->a[0]->t == 1 && z->a[1]->t == 1) {
        double a = z->a[0]->n, b = z->a[1]->n;
        if (a == floor(a) && b == floor(b) && a >= 0 && b >= 0 && a < t->w && b < t->h) { t->hasZero = 1; t->zero[0] = (int)a; t->zero[1] = (int)b; }
    }
    return t;
}
Img *rd_tilemap_layer(TL *t, int li, const char *key) {
    if (t->layerImg[li]) return t->layerImg[li];
    static unsigned char **texPx; static int *texW, *texH; static TL *texFor;
    if (texFor != t) {
        texPx = jb_calloc(sizeof(unsigned char*) * (t->ntex + 1)); texW = jb_calloc(sizeof(int) * (t->ntex + 1)); texH = jb_calloc(sizeof(int) * (t->ntex + 1)); texFor = t;
        for (int i = 0; i < t->ntex; i++) {
            const char *d = t->texData[i]; if (!d) continue;
            const char *comma = strchr(d, ','); if (!comma) continue;
            size_t len; unsigned char *b = b64dec(comma + 1, &len); int n;
            texPx[i] = stbi_load_from_memory(b, (int)len, &texW[i], &texH[i], &n, 4); free(b);
        }
    }
    int W = (int)fmax(1, floor(t->w * t->ts + 0.5)), H = (int)fmax(1, floor(t->h * t->ts + 0.5));
    unsigned char *px = jb_calloc((size_t)W * H * 4);
    for (int i = 0; i < t->w * t->h; i++) {
        int v = t->ltiles[li][i]; if (v < 0 || !texPx[v]) continue;
        double x0 = (i % t->w) * t->ts, y0 = (i / t->w) * t->ts;
        int X0 = (int)floor(x0 + 0.5), Y0 = (int)floor(y0 + 0.5), X1 = (int)floor(x0 + t->ts + 0.5), Y1 = (int)floor(y0 + t->ts + 0.5);
        for (int y = Y0; y < Y1 && y < H; y++) for (int x = X0; x < X1 && x < W; x++) {
            int sx = (int)((x - x0 + 0.5) / t->ts * texW[v]), sy = (int)((y - y0 + 0.5) / t->ts * texH[v]);
            if (sx >= texW[v]) sx = texW[v] - 1; if (sy >= texH[v]) sy = texH[v] - 1; if (sx < 0) sx = 0; if (sy < 0) sy = 0;
            unsigned char *s = texPx[v] + (sy * texW[v] + sx) * 4, *d = px + ((size_t)y * W + x) * 4;
            float a = s[3] / 255.0f, da = d[3] / 255.0f, oa = a + da * (1 - a);
            if (oa <= 0) continue;
            for (int k = 0; k < 3; k++) d[k] = (unsigned char)((s[k] * a + d[k] * da * (1 - a)) / oa + 0.5f);
            d[3] = (unsigned char)(oa * 255 + 0.5f);
        }
    }
    Img *im = jb_calloc(sizeof(Img)); im->path = jb_in(key); im->px = px; im->w = W; im->h = H; im->loaded = 1;
    t->layerImg[li] = im;
    return im;
}

/* ---------- 입력 ---------- */
static long fN;   /* 프레임 번호 (RT_FRAME 과 같음) */
static int pDown; static long tapF = -9; static double mx, my; static int mIn;
static Click clickLog[32]; static int nClick;
static PtrEv ptrLog[256]; static int nPtr; static long pSeq;
static double ptrs[16][3]; static int nPtrs;
static struct { int sc; char names[4][24]; int n; } held[64]; static int nHeld;
static struct { char name[24]; long f; } taps[64]; static int nTaps;
static double downAt[16][4]; static int nDown;   /* id, x, y, f */
static void toUnits(int x, int y, double *ux, double *uy) {
    int w = RT_OPT.winW, h = RT_OPT.winH; if (win) SDL_GetWindowSize(win, &w, &h); int pw, ph; pixSize(&pw, &ph);
    double sx = w ? (double)pw / w : 1, sy = h ? (double)ph / h : 1, u = unitPx();
    *ux = (x * sx - pw / 2.0) / u; *uy = (ph / 2.0 - y * sy) / u;
}
static void plog(int id, char k, double x, double y) {
    if (nPtr == 256) { memmove(ptrLog, ptrLog + 1, sizeof(PtrEv) * 255); nPtr--; }
    PtrEv *e = &ptrLog[nPtr++]; e->s = ++pSeq; e->id = id; e->k = k; e->x = x; e->y = y; e->t = perf_now();
}
static void keyNames(SDL_Keysym ks, char out[4][24], int *n) {
    *n = 0; const char *nm = NULL; char b[32];
    SDL_Keycode k = ks.sym;
    if (k >= 'a' && k <= 'z') { b[0] = (char)k; b[1] = 0; nm = b; }
    else if (k >= '0' && k <= '9') { b[0] = (char)k; b[1] = 0; nm = b; }
    else switch (k) {
        case SDLK_SPACE: nm = "space"; break; case SDLK_RETURN: case SDLK_KP_ENTER: nm = "enter"; break; case SDLK_ESCAPE: nm = "esc"; break;
        case SDLK_UP: nm = "up"; break; case SDLK_DOWN: nm = "down"; break; case SDLK_LEFT: nm = "left"; break; case SDLK_RIGHT: nm = "right"; break;
        case SDLK_LSHIFT: case SDLK_RSHIFT: nm = "shift"; break; case SDLK_LCTRL: case SDLK_RCTRL: nm = "ctrl"; break;
        case SDLK_LALT: case SDLK_RALT: nm = "alt"; break; case SDLK_LGUI: case SDLK_RGUI: nm = "meta"; break;
        case SDLK_TAB: nm = "tab"; break; case SDLK_BACKSPACE: nm = "backspace"; break; case SDLK_DELETE: nm = "delete"; break;
        case SDLK_HOME: nm = "home"; break; case SDLK_END: nm = "end"; break; case SDLK_PAGEUP: nm = "pageup"; break; case SDLK_PAGEDOWN: nm = "pagedown"; break;
        case SDLK_INSERT: nm = "insert"; break; case SDLK_CAPSLOCK: nm = "capslock"; break;
        default:
            if (k >= SDLK_F1 && k <= SDLK_F12) { snprintf(b, sizeof b, "f%d", (int)(k - SDLK_F1 + 1)); nm = b; }
            else if (k > 32 && k < 127) { b[0] = (char)k; b[1] = 0; nm = b; }
    }
    if (nm) snprintf(out[(*n)++], 24, "%s", nm);
    /* e.code 쪽 이름 (Comma → "comma" 등) */
    static const struct { SDL_Keycode k; const char *n; } CODE[] = {{',', "comma"},{'.', "period"},{'/', "slash"},{';', "semicolon"},{'\'', "quote"},{'[', "bracketleft"},{']', "bracketright"},{'\\', "backslash"},{'-', "minus"},{'=', "equal"},{'`', "backquote"},{0, NULL}};
    for (int i = 0; CODE[i].n; i++) if (CODE[i].k == k && *n < 4) snprintf(out[(*n)++], 24, "%s", CODE[i].n);
}
int in_key(const char *canon) {
    if (!opened) return 0;
    for (int i = 0; i < nHeld; i++) for (int q = 0; q < held[i].n; q++) if (!strcmp(held[i].names[q], canon)) return 1;
    for (int i = 0; i < nTaps; i++) if (!strcmp(taps[i].name, canon) && RT_FRAME <= taps[i].f + 1) return 1;
    return 0;
}
int in_clicked(void) { return opened && (pDown > 0 || RT_FRAME <= tapF + 1) ? 1 : 0; }
int in_mouse_in(void) { return opened && mIn; }
void in_mouse(double *x, double *y) { *x = mx; *y = my; }
int in_clicks(Click **out) { *out = clickLog; return opened ? nClick : 0; }
int in_ptrlog(PtrEv **out) { *out = ptrLog; return opened ? nPtr : 0; }
int in_pointers(double (**xy)[3]) { *xy = ptrs; return opened ? nPtrs : 0; }
static void keyDown(SDL_Keysym ks, int repeat) {
    char nm[4][24]; int n; keyNames(ks, nm, &n);
    int hi = -1; for (int i = 0; i < nHeld; i++) if (held[i].sc == ks.scancode) hi = i;
    if (hi < 0 && nHeld < 64) hi = nHeld++;
    if (hi >= 0) { held[hi].sc = ks.scancode; held[hi].n = n; memcpy(held[hi].names, nm, sizeof nm); }
    if (!repeat) for (int q = 0; q < n; q++) {
        int ti = -1; for (int i = 0; i < nTaps; i++) if (!strcmp(taps[i].name, nm[q])) ti = i;
        if (ti < 0 && nTaps < 64) ti = nTaps++;
        if (ti >= 0) { snprintf(taps[ti].name, 24, "%s", nm[q]); taps[ti].f = RT_FRAME; }
    }
}
static void keyUp(SDL_Keysym ks) { for (int i = 0; i < nHeld; i++) if (held[i].sc == ks.scancode) { held[i] = held[--nHeld]; return; } }
static void ptrDown(int id, double x, double y) {
    mIn = 1; mx = x; my = y; pDown++; tapF = RT_FRAME;
    if (nDown < 16) { downAt[nDown][0] = id; downAt[nDown][1] = x; downAt[nDown][2] = y; downAt[nDown][3] = (double)RT_FRAME; nDown++; }
    int pi = -1; for (int i = 0; i < nPtrs; i++) if ((int)ptrs[i][0] == id) pi = i;
    if (pi < 0 && nPtrs < 16) pi = nPtrs++;
    if (pi >= 0) { ptrs[pi][0] = id; ptrs[pi][1] = x; ptrs[pi][2] = y; }
    plog(id, 'd', x, y);
}
static void ptrMove(int id, double x, double y) {
    for (int i = 0; i < nPtrs; i++) if ((int)ptrs[i][0] == id) { ptrs[i][1] = x; ptrs[i][2] = y; plog(id, 'm', x, y); }
}
static void ptrUp(int id, double x, double y) {
    int had = 0; for (int i = 0; i < nPtrs; i++) if ((int)ptrs[i][0] == id) { had = 1; ptrs[i][0] = ptrs[nPtrs - 1][0]; ptrs[i][1] = ptrs[nPtrs - 1][1]; ptrs[i][2] = ptrs[nPtrs - 1][2]; nPtrs--; break; }
    if (had) plog(id, 'u', x, y);
    pDown = pDown > 0 ? pDown - 1 : 0;
    int di = -1; for (int i = 0; i < nDown; i++) if ((int)downAt[i][0] == id) di = i;
    if (di < 0) return;
    double dx = downAt[di][1], dy = downAt[di][2]; long df = (long)downAt[di][3];
    downAt[di][0] = downAt[nDown - 1][0]; downAt[di][1] = downAt[nDown - 1][1]; downAt[di][2] = downAt[nDown - 1][2]; downAt[di][3] = downAt[nDown - 1][3]; nDown--;
    mx = x; my = y;
    if (nClick == 32) { memmove(clickLog, clickLog + 1, sizeof(Click) * 31); nClick--; }
    Click *k = &clickLog[nClick++]; k->dx = dx; k->dy = dy; k->ux = x; k->uy = y; k->t = perf_now(); k->f = RT_FRAME; k->df = df;
}
/* 테스트용: --keys "ms:키:d|u,…" */
typedef struct { double t; char key[24]; int down; int done; } KS;
static KS *ks; static int nks, ksParsed;
static void keyScript(void) {
    if (!RT_OPT.keys) return;
    if (!ksParsed) {
        ksParsed = 1; char *s = jb_strdup(RT_OPT.keys), *save = NULL;
        for (char *tok = strtok_r(s, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
            ks = jb_realloc(ks, sizeof(KS) * (nks + 1)); KS *k = &ks[nks]; memset(k, 0, sizeof *k);
            char *a = strchr(tok, ':'); if (!a) continue; *a = 0; char *b = strchr(a + 1, ':'); if (!b) continue; *b = 0;
            k->t = atof(tok); snprintf(k->key, sizeof k->key, "%s", a + 1); k->down = b[1] != 'u'; nks++;
        }
    }
    double now = perf_now();
    for (int i = 0; i < nks; i++) {
        if (ks[i].done || now < ks[i].t) continue; ks[i].done = 1;
        if (!strncmp(ks[i].key, "click@", 6)) {   /* click@x;y (화면 단위) */
            double x = 0, y = 0; sscanf(ks[i].key + 6, "%lf;%lf", &x, &y);
            if (ks[i].down) ptrDown(1, x, y); else ptrUp(1, x, y);
            continue;
        }
        int hi = -1; for (int q = 0; q < nHeld; q++) if (held[q].sc == 1000 + i % 500 || (held[q].n && !strcmp(held[q].names[0], ks[i].key))) hi = q;
        if (ks[i].down) {
            if (hi < 0 && nHeld < 64) hi = nHeld++;
            if (hi >= 0) { held[hi].sc = 100000 + hi; held[hi].n = 1; snprintf(held[hi].names[0], 24, "%s", ks[i].key); }
            int ti = -1; for (int q = 0; q < nTaps; q++) if (!strcmp(taps[q].name, ks[i].key)) ti = q;
            if (ti < 0 && nTaps < 64) ti = nTaps++;
            if (ti >= 0) { snprintf(taps[ti].name, 24, "%s", ks[i].key); taps[ti].f = RT_FRAME; }
        } else if (hi >= 0) held[hi] = held[--nHeld];
    }
}
static int shotDone;
void rd_poll(void) {
    (void)fN;
    if (!opened) return;
    if (wantOpen) reallyOpen();
    keyScript();
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT: fflush(stdout); au_stop_all(); _exit(0);
        case SDL_KEYDOWN: keyDown(e.key.keysym, e.key.repeat); break;
        case SDL_KEYUP: keyUp(e.key.keysym); break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_LEAVE) mIn = 0;
            else if (e.window.event == SDL_WINDOWEVENT_ENTER) mIn = 1;
            else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) { nHeld = 0; nTaps = 0; }
            break;
        case SDL_MOUSEMOTION: { double x, y; toUnits(e.motion.x, e.motion.y, &x, &y); mIn = 1; mx = x; my = y; ptrMove(1, x, y); break; }
        case SDL_MOUSEBUTTONDOWN: if (e.button.button == SDL_BUTTON_LEFT) { double x, y; toUnits(e.button.x, e.button.y, &x, &y); ptrDown(1, x, y); } break;
        case SDL_MOUSEBUTTONUP: if (e.button.button == SDL_BUTTON_LEFT) { double x, y; toUnits(e.button.x, e.button.y, &x, &y); ptrUp(1, x, y); } break;
        case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP: {
            int pw, ph; pixSize(&pw, &ph); double u = unitPx();
            double x = (e.tfinger.x * pw - pw / 2.0) / u, y = (ph / 2.0 - e.tfinger.y * ph) / u; int id = 2 + (int)(e.tfinger.fingerId & 0xffff);
            if (e.type == SDL_FINGERDOWN) ptrDown(id, x, y); else if (e.type == SDL_FINGERMOTION) { mx = x; my = y; ptrMove(id, x, y); } else ptrUp(id, x, y);
            break;
        }
        }
    }
    if (RT_OPT.shot && !shotDone && perf_now() >= RT_OPT.shotAt) {
        shotDone = 1; rd_paint();
        if (!rd_shot(RT_OPT.shot)) fprintf(stderr, "화면을 저장하지 못했습니다\n");
        fflush(stdout); _exit(0);
    }
}
