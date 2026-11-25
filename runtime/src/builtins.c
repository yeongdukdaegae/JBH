/* 수학 · 문자열 함수 (편집기의 MATH · STRF) · 키 이름 · ease · 기준점 · 파일 종류 */
#include "jb.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define D2R (M_PI / 180.0)
#define R2D (180.0 / M_PI)

static const struct { const char *n; int a; const char *r; } MATH[] = {
    {"sin",1,"float"},{"cos",1,"float"},{"tan",1,"float"},{"asin",1,"float"},{"acos",1,"float"},{"atan",1,"float"},{"atan2",2,"float"},
    {"sinRad",1,"float"},{"cosRad",1,"float"},{"tanRad",1,"float"},{"asinRad",1,"float"},{"acosRad",1,"float"},{"atanRad",1,"float"},{"atan2Rad",2,"float"},
    {"sqrt",1,"float"},{"pow",2,"float"},{"ln",1,"float"},{"log10",1,"float"},{"abs",1,"same"},{"floor",1,"int"},{"ceil",1,"int"},{"round",1,"int"},
    {"min",2,"num"},{"max",2,"num"},{"random",2,"float"},{"pi",0,"float"},{NULL,0,NULL}};
static int mi(const char *n) { for (int i = 0; MATH[i].n; i++) if (!strcmp(MATH[i].n, n)) return i; return -1; }
int math_n(const char *n) { int i = mi(n); return i < 0 ? -1 : MATH[i].a; }
const char *math_r(const char *n) { int i = mi(n); return i < 0 ? "float" : MATH[i].r; }
static double js_round(double x) { return floor(x + 0.5); }
double math_call(const char *n, double *x) {
    switch (mi(n)) {
    case 0: return sin(x[0] * D2R); case 1: return cos(x[0] * D2R); case 2: return tan(x[0] * D2R);
    case 3: return asin(x[0]) * R2D; case 4: return acos(x[0]) * R2D; case 5: return atan(x[0]) * R2D; case 6: return atan2(x[0], x[1]) * R2D;
    case 7: return sin(x[0]); case 8: return cos(x[0]); case 9: return tan(x[0]); case 10: return asin(x[0]); case 11: return acos(x[0]);
    case 12: return atan(x[0]); case 13: return atan2(x[0], x[1]);
    case 14: return sqrt(x[0]); case 15: return pow(x[0], x[1]); case 16: return log(x[0]); case 17: return log10(x[0]);
    case 18: return fabs(x[0]); case 19: return floor(x[0]); case 20: return ceil(x[0]); case 21: return js_round(x[0]);
    case 22: return isnan(x[0]) || isnan(x[1]) ? NAN : x[0] < x[1] ? x[0] : x[1];
    case 23: return isnan(x[0]) || isnan(x[1]) ? NAN : x[0] > x[1] ? x[0] : x[1];
    case 24: return x[0] + (x[1] - x[0]) * ((double)rand() / ((double)RAND_MAX + 1.0));
    case 25: return M_PI;
    }
    return 0;
}

/* ---------- 문자열 (UTF-8 글자 단위) ---------- */
static const struct { const char *n; const char *a; int r; } STRF[] = {
    {"strLen","s",TY_INT},{"subStr","sii",TY_STRING},{"charAt","si",TY_STRING},{"findStr","ss",TY_INT},{"replaceStr","sss",TY_STRING},
    {"upperStr","s",TY_STRING},{"lowerStr","s",TY_STRING},{"trimStr","s",TY_STRING},{"startsWith","ss",TY_BIT},{"endsWith","ss",TY_BIT},
    {"toInt","s",TY_INT},{"toFloat","s",TY_FLOAT},{"splitStr","ss",TY_LIST},{NULL,NULL,0}};
static int si(const char *n) { for (int i = 0; STRF[i].n; i++) if (!strcmp(STRF[i].n, n)) return i; return -1; }
int strf_n(const char *n, const char **sig, int *rty) { int i = si(n); if (i < 0) return -1; *sig = STRF[i].a; *rty = STRF[i].r; return (int)strlen(STRF[i].a); }
/* JS 문자열은 UTF-16 — Array.from 은 코드 포인트. 글자 = 코드 포인트 */
static int u8len(const char *s) { int n = 0; for (; *s; s++) if (((unsigned char)*s & 0xC0) != 0x80) n++; return n; }
static const char *u8at(const char *s, long k) { while (*s && k > 0) { s++; while (((unsigned char)*s & 0xC0) == 0x80) s++; k--; } return s; }
static char *substr8(const char *s, double f0, double n0) {
    long f = (long)trunc(f0), n = (long)trunc(n0);
    if (f < 1) { n += f - 1; f = 1; }
    if (n <= 0) return jb_strdup("");
    const char *a = u8at(s, f - 1), *b = u8at(a, n);
    return jb_strndup(a, b - a);
}
static int isws(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
static char *trim(const char *s) { while (isws(*s)) s++; size_t L = strlen(s); while (L && isws(s[L-1])) L--; return jb_strndup(s, L); }
static double strnum(const char *s) {   /* JS Number(trim(s)) — 숫자가 아니면 0 */
    char *t = trim(s); double v = 0;
    if (*t) {
        char *e; const char *p = t;
        if ((p[0] == '0') && (p[1] == 'x' || p[1] == 'X')) v = (double)strtoll(p + 2, &e, 16);
        else if (!strcmp(p, "Infinity") || !strcmp(p, "+Infinity")) v = INFINITY;
        else if (!strcmp(p, "-Infinity")) v = -INFINITY;
        else { v = strtod(p, &e); if (*e || isalpha((unsigned char)p[0]) || (p[0] && strchr("+-", p[0]) && isalpha((unsigned char)p[1]))) v = NAN; }
        if (!isfinite(v)) v = 0;
    }
    free(t); return v;
}
static int32_t toi32(double x) { if (!isfinite(x)) return 0; double t = fmod(trunc(x), 4294967296.0); if (t < 0) t += 4294967296.0; return (int32_t)(uint32_t)t; }
Value strf_call(const char *n, Value *a) {
    const char *s0 = a[0].k == V_STR ? a[0].u.s : "";
    switch (si(n)) {
    case 0: return v_num(u8len(s0));
    case 1: return v_strown(substr8(s0, a[1].u.n, a[2].u.n));
    case 2: return v_strown(substr8(s0, a[1].u.n, 1));
    case 3: { const char *p = strstr(s0, a[1].u.s); if (!p) return v_num(0); int k = 0; for (const char *q = s0; q < p; q++) if (((unsigned char)*q & 0xC0) != 0x80) k++; return v_num(k + 1); }
    case 4: {
        const char *o = a[1].u.s, *nw = a[2].u.s; if (!*o) return v_str(s0);
        size_t lo = strlen(o), ln = strlen(nw), cap = strlen(s0) + 1, len = 0; char *r = jb_alloc(cap);
        for (const char *p = s0; *p;) {
            if (!strncmp(p, o, lo)) { if (len + ln + 1 > cap) { cap = (len + ln + 1) * 2; r = jb_realloc(r, cap); } memcpy(r + len, nw, ln); len += ln; p += lo; }
            else { if (len + 2 > cap) { cap *= 2; r = jb_realloc(r, cap); } r[len++] = *p++; }
        }
        r[len] = 0; return v_strown(r);
    }
    case 5: case 6: { char *r = jb_strdup(s0); for (char *p = r; *p; p++) if ((unsigned char)*p < 0x80) *p = si(n) == 5 ? (char)toupper(*p) : (char)tolower(*p); return v_strown(r); }
    case 7: return v_strown(trim(s0));
    case 8: return v_num(!strncmp(s0, a[1].u.s, strlen(a[1].u.s)));
    case 9: { size_t lh = strlen(s0), lt = strlen(a[1].u.s); return v_num(lt <= lh && !strcmp(s0 + lh - lt, a[1].u.s)); }
    case 10: return v_num(toi32(trunc(strnum(s0))));
    case 11: return v_num(strnum(s0));
    case 12: {
        const char *sep = a[1].u.s; Value l = v_list(0);
        if (!*sep) { for (const char *p = s0; *p;) { const char *q = u8at(p, 1); list_push(l.u.l, v_strown(jb_strndup(p, q - p))); p = q; } }
        else { size_t ls = strlen(sep); const char *p = s0; for (;;) { const char *q = strstr(p, sep); if (!q) { list_push(l.u.l, v_str(p)); break; } list_push(l.u.l, v_strown(jb_strndup(p, q - p))); p = q + ls; } }
        return l;
    }
    }
    return v_num(0);
}

/* ---------- 키 이름 ---------- */
const char *key_canon(const char *n, char *buf, size_t bn) {
    size_t i = 0; for (; n[i] && i + 1 < bn; i++) buf[i] = (char)tolower((unsigned char)n[i]); buf[i] = 0;
    static const char *AL[][2] = {{" ","space"},{"spacebar","space"},{"return","enter"},{"escape","esc"},{"control","ctrl"},{"arrowup","up"},{"arrowdown","down"},
        {"arrowleft","left"},{"arrowright","right"},{"shiftleft","shift"},{"shiftright","shift"},{"controlleft","ctrl"},{"controlright","ctrl"},{"altleft","alt"},
        {"altright","alt"},{"metaleft","meta"},{"metaright","meta"},{"numpadenter","enter"},{NULL,NULL}};
    for (int k = 0; AL[k][0]; k++) if (!strcmp(buf, AL[k][0])) { snprintf(buf, bn, "%s", AL[k][1]); break; }
    return buf;
}

/* ---------- ease · 기준점 ---------- */
int jb_ease_id(const char *v) {
    char b[32]; size_t i = 0; for (; v[i] && i < 31; i++) b[i] = (char)tolower((unsigned char)v[i]); b[i] = 0;
    if (!strcmp(b, "linear")) return 0; if (!strcmp(b, "sin")) return 1; if (!strcmp(b, "sinright")) return 2; if (!strcmp(b, "sinleft")) return 3;
    return -1;
}
int jb_ease_ok(const char *v) { return jb_ease_id(v) >= 0; }
double jb_ease(int id, double k) {
    const double S45 = sqrt(0.5);
    switch (id) {
    case 1: return sin(k * M_PI / 2);
    case 2: return sin(k * M_PI / 4) / S45;
    case 3: return (sin(M_PI / 4 + k * M_PI / 4) - S45) / (1 - S45);
    }
    return k;
}
int jb_parse_nudge(const char *v, int *nx, int *ny) {
    char t[64]; int n = 0;
    for (const char *p = v; *p && n < 63; p++) { char c = (char)tolower((unsigned char)*p); if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '-' || c == '_' || c == ',') continue; t[n++] = c; }
    t[n] = 0;
    if (!strcmp(t, "center") || !strcmp(t, "middle") || !strcmp(t, "centre")) { *nx = *ny = 0; return 1; }
    if (!n) return 0;
    const char *A[] = {"top","bottom"}, *B[] = {"left","right"};
    int v1 = -1, h1 = -1;
    /* (top|bottom)?(left|right)? 또는 (left|right)?(top|bottom)? */
    for (int order = 0; order < 2; order++) {
        const char *p = t; int vv = -1, hh = -1;
        for (int step = 0; step < 2; step++) {
            int vert = (order == 0) ? step == 0 : step == 1;
            const char **set = vert ? A : B;
            for (int q = 0; q < 2; q++) if (!strncmp(p, set[q], strlen(set[q]))) { if (vert) vv = q; else hh = q; p += strlen(set[q]); break; }
        }
        if (!*p) { v1 = vv; h1 = hh; goto ok; }
    }
    return 0;
ok:
    *nx = h1 == 0 ? 1 : h1 == 1 ? -1 : 0;
    *ny = v1 == 0 ? -1 : v1 == 1 ? 1 : 0;
    return 1;
}

/* ---------- 파일 종류 ---------- */
static int extIn(const char *p, const char *const *ex) {
    const char *d = strrchr(p, '.'); if (!d) return 0;
    char e[16]; size_t i = 0; for (d++; *d && i < 15; d++) e[i++] = (char)tolower((unsigned char)*d); e[i] = 0;
    for (; *ex; ex++) if (!strcmp(e, *ex)) return 1;
    return 0;
}
int jb_is_img(const char *p) { static const char *const X[] = {"png","jpg","jpeg","gif","webp","bmp",NULL}; return extIn(p, X); }
int jb_is_aud(const char *p) { static const char *const X[] = {"wav","mp3","ogg","m4a",NULL}; return extIn(p, X); }
int jb_is_ttf(const char *p) { static const char *const X[] = {"ttf","otf",NULL}; return extIn(p, X); }
int jb_is_tl(const char *p) { static const char *const X[] = {"tl",NULL}; return extIn(p, X); }
