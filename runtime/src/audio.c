/* soundMaster — 편집기의 Web Audio 동작 (스피커 볼륨 → 공간(거리 · 좌우) → 출력 · 페이드 · 반복 구간) 을 SDL 오디오로
 *   .wav (dr_wav) · .mp3 (dr_mp3) · .ogg (stb_vorbis) — .m4a 는 읽지 못함
 */
#define _GNU_SOURCE
#include "rt.h"
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "dr_wav.h"
#include "dr_mp3.h"
extern int stb_vorbis_decode_memory(const unsigned char *mem, int len, int *channels, int *sample_rate, short **output);
extern const unsigned char *proj_bytes(const char *path, size_t *len);

#define DEV_RATE 48000
typedef struct Buf { const char *path; short *pcm; long frames; int ch, rate; volatile int state; /* 0 대기 · 1 됨 · 2 실패 */ int live; double lastUse; struct Buf *next; } Buf;
typedef struct Voice {
    Buf *b; double pos;                 /* 소리 프레임 */
    int loop; double ls, le;            /* 반복 구간 (프레임) */
    double t0, len;                     /* 시작 (장치 프레임) · 길이 (장치 프레임, <0 = 끝없음) */
    double fi, fo;                      /* 페이드 (장치 프레임) */
    double brkAt, brkLen, brkFrom;      /* break 의 페이드 아웃 */
    int stopped; int pending; double off; struct Voice *next; struct Spk *k; int gen;
} Voice;
struct Spk { const char *id; double v, vTarget, vRate; int fin, fout; double space, pan; int gen; Voice *voices; };
static SDL_AudioDeviceID dev; static int auOk, auTried;
static SDL_mutex *mx; static double devT;   /* 지금까지 낸 프레임 수 */
static Buf *bufs; static Spk **spks; static int nspk;
static SDL_Thread *decT; static SDL_cond *decC; static int decQuit;

static void decodeOne(Buf *b) {
    size_t len; const unsigned char *d = proj_bytes(b->path, &len);
    const char *ext = strrchr(b->path, '.'); char e[8] = ""; if (ext) { for (int i = 0; i < 7 && ext[i + 1]; i++) e[i] = (char)((ext[i + 1] >= 'A' && ext[i + 1] <= 'Z') ? ext[i + 1] + 32 : ext[i + 1]); }
    short *pcm = NULL; long frames = 0; int ch = 0, rate = 0;
    if (d) {
        if (!strcmp(e, "wav")) { unsigned int c, r; drwav_uint64 n; pcm = drwav_open_memory_and_read_pcm_frames_s16(d, len, &c, &r, &n, NULL); ch = (int)c; rate = (int)r; frames = (long)n; }
        else if (!strcmp(e, "mp3")) { drmp3_config cf; drmp3_uint64 n; pcm = drmp3_open_memory_and_read_pcm_frames_s16(d, len, &cf, &n, NULL); ch = (int)cf.channels; rate = (int)cf.sampleRate; frames = (long)n; }
        else if (!strcmp(e, "ogg")) { int c, r; short *o; int n = stb_vorbis_decode_memory(d, (int)len, &c, &r, &o); if (n > 0) { pcm = o; ch = c; rate = r; frames = n; } }
    }
    SDL_LockMutex(mx);
    if (pcm && ch > 0 && rate > 0) { b->pcm = pcm; b->frames = frames; b->ch = ch > 2 ? 2 : ch; b->rate = rate; b->state = 1;
        if (ch > 2) { short *p2 = malloc(sizeof(short) * 2 * frames); for (long i = 0; i < frames; i++) { p2[i * 2] = pcm[i * ch]; p2[i * 2 + 1] = pcm[i * ch + 1]; } free(pcm); b->pcm = p2; } }
    else { b->state = 2; fprintf(stderr, "✖ 소리 파일 '%s' 을(를) 읽지 못했습니다 (손상되었거나 지원하지 않는 형식)\n", b->path); }
    SDL_UnlockMutex(mx);
}
/* 긴 소리 (20 초 이상) 는 합쳐 300 초까지만 — 오래 안 쓴 것부터 버림 */
static void trimLong(Buf *keep) {
    double tot = 0; for (Buf *b = bufs; b; b = b->next) if (b->state == 1 && b->frames >= 20.0 * b->rate) tot += (double)b->frames / b->rate;
    while (tot > 300) {
        Buf *old = NULL;
        for (Buf *b = bufs; b; b = b->next) if (b != keep && b->state == 1 && b->frames >= 20.0 * b->rate && !b->live && (!old || b->lastUse < old->lastUse)) old = b;
        if (!old) break;
        tot -= (double)old->frames / old->rate; free(old->pcm); old->pcm = NULL; old->state = 0;
    }
}
static int decMain(void *u) {
    (void)u;
    SDL_LockMutex(mx);
    while (!decQuit) {
        Buf *todo = NULL; double best = -1;
        for (Buf *b = bufs; b; b = b->next) if (b->state == 0 && b->live >= 0 && b->lastUse > best) { todo = b; best = b->lastUse; }
        if (!todo) { SDL_CondWait(decC, mx); continue; }
        todo->state = 3;   /* 읽는 중 */
        SDL_UnlockMutex(mx);
        decodeOne(todo);
        SDL_LockMutex(mx);
        if (todo->state == 1) trimLong(todo);
    }
    SDL_UnlockMutex(mx);
    return 0;
}
static void settle(Voice *v);
static double voiceGain(Voice *v, double t) {   /* 소리 하나의 페이드 */
    double rel = t - v->t0, g = 1;
    if (v->fi > 0) { double fiEnd = v->len >= 0 ? fmin(v->fi, v->len) : v->fi; if (rel < fiEnd && fiEnd > 0) g = rel / fiEnd; }
    if (v->len >= 0 && v->fo > 0 && v->len > v->fo) { double s = fmax(v->fi, v->len - v->fo); if (rel >= s) g = fmax(0, 1 - (rel - s) / (v->len - s)); }
    if (v->brkAt >= 0) { double k = (t - v->brkAt) / v->brkLen; g = v->brkFrom * fmax(0, 1 - k); }
    return g;
}
static void mix(void *u, Uint8 *stream, int bytes) {
    (void)u;
    float *out = (float *)stream; int n = bytes / (int)(sizeof(float) * 2);
    memset(out, 0, bytes);
    SDL_LockMutex(mx);
    for (int si = 0; si < nspk; si++) {
        Spk *k = spks[si];
        for (Voice **pv = &k->voices; *pv;) {
            Voice *v = *pv; Buf *b = v->b;
            if (v->pending) {
                if (b->state == 2 || v->gen != k->gen) { v->stopped = 1; }
                else if (b->state == 1) { settle(v); v->pending = 0; v->t0 = devT; v->pos = v->off * b->rate; if (!v->stopped) b->live++; else v->pending = 1; }
            }
            if (!v->stopped && !v->pending && b->state == 1) {
                double step = (double)b->rate / DEV_RATE;
                for (int i = 0; i < n; i++) {
                    double t = devT + i;
                    if (v->len >= 0 && t - v->t0 >= v->len) { v->stopped = 1; break; }
                    if (v->brkAt >= 0 && t - v->brkAt >= v->brkLen) { v->stopped = 1; break; }
                    double p = v->pos;
                    if (v->loop) { if (p >= v->le) { p = v->ls + fmod(p - v->ls, v->le - v->ls); v->pos = p; } }
                    else if (p >= b->frames) { v->stopped = 1; break; }
                    long i0 = (long)p; double fr = p - i0; long i1 = i0 + 1;
                    if (v->loop && i1 >= (long)v->le) i1 = (long)v->ls;
                    if (i1 >= b->frames) i1 = b->frames - 1;
                    if (i0 >= b->frames) i0 = b->frames - 1;
                    float L, R;
                    if (b->ch == 1) { L = R = (float)((b->pcm[i0] * (1 - fr) + b->pcm[i1] * fr) / 32768.0); }
                    else { L = (float)((b->pcm[i0 * 2] * (1 - fr) + b->pcm[i1 * 2] * fr) / 32768.0); R = (float)((b->pcm[i0 * 2 + 1] * (1 - fr) + b->pcm[i1 * 2 + 1] * fr) / 32768.0); }
                    double g = voiceGain(v, t);
                    /* 스피커 볼륨 (페이드) */
                    double vol = k->v;
                    if (k->vRate != 0) { vol = k->v + k->vRate * i; if ((k->vRate > 0 && vol > k->vTarget) || (k->vRate < 0 && vol < k->vTarget)) vol = k->vTarget; }
                    double G = g * vol * k->space;
                    /* 좌우 (StereoPanner) */
                    double pan = k->pan, oL, oR;
                    if (b->ch == 1) { double x = (pan + 1) / 2; oL = L * cos(x * M_PI / 2); oR = L * sin(x * M_PI / 2); }
                    else if (pan <= 0) { double x = pan + 1; oL = L + R * cos(x * M_PI / 2); oR = R * sin(x * M_PI / 2); }
                    else { double x = pan; oL = L * cos(x * M_PI / 2); oR = R + L * sin(x * M_PI / 2); }
                    out[i * 2] += (float)(oL * G); out[i * 2 + 1] += (float)(oR * G);
                    v->pos += step;
                }
            }
            if (v->stopped) { if (!v->pending && b->live > 0) b->live--; *pv = v->next; free(v); continue; }
            pv = &v->next;
        }
        if (k->vRate != 0) { k->v += k->vRate * n; if ((k->vRate > 0 && k->v >= k->vTarget) || (k->vRate < 0 && k->v <= k->vTarget)) { k->v = k->vTarget; k->vRate = 0; } }
    }
    devT += n;
    SDL_UnlockMutex(mx);
    for (int i = 0; i < n * 2; i++) { if (out[i] > 1) out[i] = 1; else if (out[i] < -1) out[i] = -1; }
}
void au_init(void) {
    if (auTried) return; auTried = 1;
    mx = SDL_CreateMutex(); decC = SDL_CreateCond();
    if (RT_OPT.mute) return;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) { fprintf(stderr, "⚠ 소리를 낼 수 없습니다: %s\n", SDL_GetError()); return; }
    SDL_AudioSpec want, have; SDL_zero(want); want.freq = DEV_RATE; want.format = AUDIO_F32SYS; want.channels = 2; want.samples = 1024; want.callback = mix;
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!dev) { fprintf(stderr, "⚠ 소리를 낼 수 없습니다: %s\n", SDL_GetError()); return; }
    auOk = 1; SDL_PauseAudioDevice(dev, 0);
}
static void startDecoder(void) { if (!decT) decT = SDL_CreateThread(decMain, "jb-decode", NULL); }
static Buf *bufOf(const char *path) {
    au_init();
    for (Buf *b = bufs; b; b = b->next) if (b->path == path || !strcmp(b->path, path)) { b->lastUse = perf_now(); return b; }
    Buf *b = calloc(1, sizeof(Buf)); b->path = path; b->lastUse = perf_now(); b->next = bufs; bufs = b;
    return b;
}
int au_load(const char *path) {
    au_init(); if (!mx) return 0;
    SDL_LockMutex(mx); Buf *b = bufOf(path); b->lastUse = -1e9 + perf_now(); SDL_CondSignal(decC); SDL_UnlockMutex(mx);
    startDecoder(); return 1;
}
Spk *au_speaker(const char *id) {
    au_init();
    Spk *k = calloc(1, sizeof(Spk)); k->id = id; k->v = 1; k->vTarget = 1; k->space = 1;
    if (mx) SDL_LockMutex(mx);
    spks = realloc(spks, sizeof(Spk*) * (nspk + 1)); spks[nspk++] = k;
    if (mx) SDL_UnlockMutex(mx);
    return k;
}
void au_play(Spk *k, const char *path, double ms, int loops, int hasPart, double a, double e, int intro) {
    if (!auOk) return;
    SDL_LockMutex(mx);
    Buf *b = bufOf(path); b->lastUse = perf_now();
    if (b->state == 0) SDL_CondSignal(decC);
    Voice *v = calloc(1, sizeof(Voice)); v->b = b; v->k = k; v->gen = k->gen; v->pending = 1; v->brkAt = -1;
    /* 길이는 소리 길이를 알아야 해서 — 읽힌 뒤 정함 (아래 settle) */
    double dur = b->state == 1 ? (double)b->frames / b->rate : -1;
    (void)dur;
    v->loop = 0; v->len = -1; v->off = 0;
    v->fi = k->fin / 1000.0 * DEV_RATE; v->fo = k->fout / 1000.0 * DEV_RATE;
    /* 정보 저장: 읽힌 다음 계산 */
    v->ls = hasPart ? a : -1; v->le = hasPart ? e : -1; v->pos = loops; v->t0 = ms; v->off = intro;
    v->next = k->voices; k->voices = v;
    SDL_UnlockMutex(mx);
    startDecoder();
}
/* 읽힌 소리의 길이 · 반복 정하기 (mix 에서 pending 을 풀기 전에) */
static void settle(Voice *v) {
    Buf *b = v->b; double durS = (double)b->frames / b->rate;
    int loops = (int)v->pos; double ms = v->t0; int hasPart = v->ls >= 0; double a = v->ls, e = v->le; int intro = (int)v->off;
    double len = (loops > 0 || loops == -1) ? (loops == -1 ? durS : durS * loops) : INFINITY, off = 0;
    v->loop = 0;
    if (hasPart && a >= 0) {
        double as = a / 1000, es = e < 0 ? durS : fmin(durS, e / 1000);
        if (as >= es) { v->stopped = 1; return; }
        v->loop = 1; v->ls = as * b->rate; v->le = es * b->rate; off = intro ? 0 : as; len = INFINITY;
    } else if (loops != -1) { v->loop = 1; v->ls = 0; v->le = (double)b->frames; }
    if (ms > 0) len = fmin(len, ms / 1000);
    v->len = isinf(len) ? -1 : len * DEV_RATE; v->off = off; v->pos = 0; v->t0 = 0;
}
void au_break(Spk *k, int noFade) {
    if (!auOk) { k->gen++; return; }
    SDL_LockMutex(mx);
    k->gen++;
    for (Voice *v = k->voices; v; v = v->next) {
        if (v->pending) { v->stopped = 1; continue; }
        if (!noFade && k->fout > 0) { if (v->brkAt < 0) { v->brkFrom = voiceGain(v, devT); v->brkAt = devT; v->brkLen = k->fout / 1000.0 * DEV_RATE; } }
        else v->stopped = 1;
    }
    SDL_UnlockMutex(mx);
}
void au_set_vol(Spk *k, int vv) {
    double t = vv / 255.0;
    if (mx) SDL_LockMutex(mx);
    double cur = k->vRate ? k->v : k->v;
    int target255 = (int)floor(k->vTarget * 255 + 0.5);
    if (vv == target255) { if (mx) SDL_UnlockMutex(mx); return; }
    int up = t > k->vTarget; double f = (up ? k->fin : k->fout) / 1000.0;
    k->vTarget = t;
    if (f > 0) { k->vRate = (t - cur) / (f * DEV_RATE); if (k->vRate == 0) k->v = t; }
    else { k->v = t; k->vRate = 0; }
    if (mx) SDL_UnlockMutex(mx);
}
int au_vol(Spk *k) { return (int)floor(k->vTarget * 255 + 0.5); }
void au_set_fade(Spk *k, int in, int ms) { if (in) k->fin = ms; else k->fout = ms; }
void au_spatial(Spk *k, double gain, double pan) { if (!k) return; if (mx) SDL_LockMutex(mx); k->space = gain; k->pan = pan; if (mx) SDL_UnlockMutex(mx); }
void au_stop_all(void) { if (auOk) SDL_PauseAudioDevice(dev, 1); }
void au_quit(void) { if (auOk) { SDL_CloseAudioDevice(dev); auOk = 0; } }
