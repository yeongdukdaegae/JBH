/* 값 · 출력 형식 · JSON — 편집기의 fmtFloat · fmtAny · fmtVal · jbJson · jsonToJb 와 같음 */
#include "jb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

Value v_num(double n) { Value v; v.k = V_NUM; v.u.n = n; return v; }
Value v_str(const char *s) { Value v; v.k = V_STR; v.u.s = jb_strdup(s ? s : ""); return v; }
Value v_strown(char *s) { Value v; v.k = V_STR; v.u.s = s; return v; }
Value v_list(int comp) { Value v; v.k = V_LIST; v.u.l = jb_calloc(sizeof(List)); v.u.l->comp = comp; return v; }
Value v_mat(int r, int c) { Value v; v.k = V_MAT; v.u.m = jb_alloc(sizeof(Mat)); v.u.m->r = r; v.u.m->c = c; v.u.m->d = jb_calloc(sizeof(double) * (r > 0 && c > 0 ? r * c : 1)); return v; }
void list_push(List *l, Value v) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 4; l->items = jb_realloc(l->items, sizeof(Value) * l->cap); }
    l->items[l->n++] = v;
}
Value v_copy(Value v) {
    switch (v.k) {
    case V_DATA: { Value d = v_data(v.u.dt->b, v.u.dt->n); d.u.dt->dev = v.u.dt->dev; d.u.dt->bind = v.u.dt->bind; return d; }
    case V_STR: return v_str(v.u.s);
    case V_MAT: { Value m = v_mat(v.u.m->r, v.u.m->c); memcpy(m.u.m->d, v.u.m->d, sizeof(double) * v.u.m->r * v.u.m->c); return m; }
    case V_LIST: {
        Value l = v_list(v.u.l->comp);
        l.u.l->cap = v.u.l->n ? v.u.l->n : 1; l.u.l->items = jb_alloc(sizeof(Value) * l.u.l->cap);
        for (int i = 0; i < v.u.l->n; i++) l.u.l->items[i] = v_copy(v.u.l->items[i]);
        l.u.l->n = v.u.l->n; return l;
    }
    }
    return v;
}
Value v_own(Value v) { return v_copy(v); }
void v_free(Value *v) {
    switch (v->k) {
    case V_STR: free(v->u.s); break;
    case V_DATA: free(v->u.dt->b); free(v->u.dt); break;
    case V_MAT: free(v->u.m->d); free(v->u.m); break;
    case V_LIST: for (int i = 0; i < v->u.l->n; i++) v_free(&v->u.l->items[i]); free(v->u.l->items); free(v->u.l); break;
    }
    v->k = V_NUM; v->u.n = 0;
}
int v_eq(Value a, Value b) {
    if (a.k != b.k) return 0;
    switch (a.k) {
    case V_NUM: return a.u.n == b.u.n;
    case V_STR: return !strcmp(a.u.s, b.u.s);
    case V_DATA: return !a.u.dt->dev && !b.u.dt->dev && a.u.dt->n == b.u.dt->n && !memcmp(a.u.dt->b, b.u.dt->b, a.u.dt->n);
    case V_MAT:
        if (a.u.m->r != b.u.m->r || a.u.m->c != b.u.m->c) return 0;
        for (int i = 0; i < a.u.m->r * a.u.m->c; i++) if (a.u.m->d[i] != b.u.m->d[i]) return 0;
        return 1;
    case V_LIST:
        if (!a.u.l->comp != !b.u.l->comp || a.u.l->n != b.u.l->n) return 0;
        for (int i = 0; i < a.u.l->n; i++) if (!v_eq(a.u.l->items[i], b.u.l->items[i])) return 0;
        return 1;
    }
    return 0;
}


/* ---------- data 형식 (편집기의 toData · fromData · dataHex 와 같은 규칙) ----------
 *   넣을 때: 숫자 → 8바이트 실수 (big-endian) · 문자열 → UTF-8 · 행렬 · 리스트 · compound → JSON 글자
 *   꺼낼 때: 받는 형식으로 억지로 읽고 안 맞으면 0 (대입을 건너뜀) · 장치에 귀속된 data 는 지금 순간의 바이트 */
unsigned char *rt_dev_snap(int dev, size_t *n);   /* run.c — 카메라 · 마이크의 지금 바이트 */
Value v_data(const unsigned char *b, size_t n) {
    Value v; v.k = V_DATA; v.u.dt = jb_calloc(sizeof(Data));
    v.u.dt->b = jb_alloc(n ? n : 1); if (n) memcpy(v.u.dt->b, b, n); v.u.dt->n = n; return v;
}
unsigned char *data_bytes(Value v, size_t *n, int *own) {
    if (v.u.dt->dev) { *own = 1; return rt_dev_snap(v.u.dt->dev, n); }
    *own = 0; *n = v.u.dt->n; return v.u.dt->b;
}
Value to_data(Value v) {
    if (v.k == V_DATA) { size_t n; int own; unsigned char *b = data_bytes(v, &n, &own); Value r = v_data(b, n); if (own) free(b); return r; }
    if (v.k == V_NUM) { unsigned char b[8]; uint64_t u; double x = v.u.n; memcpy(&u, &x, 8); for (int i = 0; i < 8; i++) b[i] = (unsigned char)(u >> (56 - 8 * i)); return v_data(b, 8); }
    if (v.k == V_STR) return v_data((const unsigned char *)v.u.s, strlen(v.u.s));
    char *j = json_of(v); Value r = v_data((const unsigned char *)j, strlen(j)); free(j); return r;
}
static int utf8_ok(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned c = s[i]; int k = c < 0x80 ? 0 : (c >> 5) == 6 ? 1 : (c >> 4) == 14 ? 2 : (c >> 3) == 30 ? 3 : -1;
        if (k < 0 || (k && i + k >= n)) return 0;
        for (int q = 1; q <= k; q++) if ((s[i + q] & 0xC0) != 0x80) return 0;
        if (k == 1 && c < 0xC2) return 0;
        i += k + 1;
    }
    return 1;
}
int from_data(Value d, int ty, const int *dims, Value *out) {
    size_t n; int own; unsigned char *b = data_bytes(d, &n, &own); int ok = 0;
    if (ty == TY_DATA) { *out = v_data(b, n); ok = 1; goto done; }
    if (ty == TY_INT || ty == TY_FLOAT || ty == TY_BIT) {
        if (n == 8) { uint64_t u = 0; for (int i = 0; i < 8; i++) u = u << 8 | b[i]; double x; memcpy(&x, &u, 8); *out = v_num(x); ok = 1; }
        goto done;
    }
    if (!utf8_ok(b, n) || memchr(b, 0, n)) goto done;
    char *s = jb_strndup((const char *)b, n);
    if (ty == TY_STRING) { *out = v_strown(s); ok = 1; goto done; }
    const char *p = s; while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    Value j;
    if (*p != '[' || json_parse(s, &j) != 1) { free(s); goto done; }
    free(s);
    if (ty == TY_COMPOUND) { *out = j; ok = 1; goto done; }
    if (ty == TY_LIST) {
        for (int i = 0; i < j.u.l->n; i++) if (j.u.l->items[i].k != V_NUM && j.u.l->items[i].k != V_STR) { v_free(&j); goto done; }
        j.u.l->comp = 0; *out = j; ok = 1; goto done;
    }
    if (ty == TY_MATRIX && dims) {   /* 한 줄 [x, y] · 여러 줄 [[…], […]] */
        int r = dims[0], c = dims[1], one = r == 1; List *L = j.u.l;
        if (one) for (int i = 0; i < L->n; i++) if (L->items[i].k != V_NUM) { one = 0; break; }
        if (one ? L->n != c : L->n != r) { v_free(&j); goto done; }
        Value m = v_mat(r, c);
        for (int i = 0; i < r; i++) {
            if (one) { for (int q = 0; q < c; q++) m.u.m->d[q] = L->items[q].u.n; break; }
            Value row = L->items[i];
            if (row.k != V_LIST || row.u.l->n != c) { v_free(&m); v_free(&j); goto done; }
            for (int q = 0; q < c; q++) { if (row.u.l->items[q].k != V_NUM) { v_free(&m); v_free(&j); goto done; } m.u.m->d[i * c + q] = row.u.l->items[q].u.n; }
        }
        v_free(&j); *out = m; ok = 1; goto done;
    }
    v_free(&j);
done:
    if (own) free(b);
    return ok;
}
static char *data_hex(Value v) {   /* 길면 앞 32바이트만 */
    size_t n; int own; unsigned char *b = data_bytes(v, &n, &own);
    size_t k = n <= 64 ? n : 32; char *o = jb_alloc(k * 2 + 40), *w = o;
    for (size_t i = 0; i < k; i++) w += sprintf(w, "%02x", b[i]);
    if (k < n) sprintf(w, "… (%zu B)", n);
    else *w = 0;
    if (own) free(b);
    return o;
}
static char *data_hex_all(Value v) {
    size_t n; int own; unsigned char *b = data_bytes(v, &n, &own);
    char *o = jb_alloc(n * 2 + 1), *w = o; for (size_t i = 0; i < n; i++) w += sprintf(w, "%02x", b[i]); *w = 0;
    if (own) free(b);
    return o;
}

/* ---------- 출력 ---------- */
static void strip0(char *s) {
    if (!strchr(s, '.')) return;
    size_t L = strlen(s); while (L && s[L-1] == '0') s[--L] = 0;
    if (L && s[L-1] == '.') s[--L] = 0;
}
char *fmt_float(double x) {
    if (isnan(x)) return jb_strdup("nan");
    if (isinf(x)) return jb_strdup(x < 0 ? "-inf" : "inf");
    if (x == 0) return jb_strdup(signbit(x) ? "-0" : "0");
    char e[64]; snprintf(e, sizeof e, "%.5e", x);
    char *ep = strchr(e, 'e'); int X = atoi(ep + 1); *ep = 0;
    char buf[400];
    if (X < -4 || X >= 6) { strip0(e); int a = abs(X); snprintf(buf, sizeof buf, "%se%c%s%d", e, X < 0 ? '-' : '+', a < 10 ? "0" : "", a); return jb_strdup(buf); }
    int dig = 5 - X; if (dig > 100) dig = 100; if (dig < 0) dig = 0;
    snprintf(buf, sizeof buf, "%.*f", dig, x); strip0(buf);
    return jb_strdup(buf);
}
static char *num_str(double v) {   /* JS String(number) */
    if (isnan(v)) return jb_strdup("NaN");
    if (isinf(v)) return jb_strdup(v < 0 ? "-Infinity" : "Infinity");
    if (v == trunc(v) && fabs(v) < 1e21) { char b[64]; snprintf(b, sizeof b, "%.0f", v); if (!strcmp(b, "-0")) strcpy(b, "0"); return jb_strdup(b); }
    char b[64];
    for (int p = 1; p <= 17; p++) { snprintf(b, sizeof b, "%.*g", p, v); if (strtod(b, NULL) == v) break; }
    if (strchr(b, 'e') && fabs(v) >= 1e-7 && fabs(v) < 1e21) {
        for (int p = 1; p <= 25; p++) { snprintf(b, sizeof b, "%.*f", p, v); if (strtod(b, NULL) == v) break; }
    }
    return jb_strdup(b);
}
typedef struct { char *s; size_t n, cap; } SB;
static void sb_put(SB *b, const char *s) { size_t L = strlen(s); if (b->n + L + 1 > b->cap) { b->cap = (b->n + L + 1) * 2; b->s = jb_realloc(b->s, b->cap); } memcpy(b->s + b->n, s, L + 1); b->n += L; }
static void sb_own(SB *b, char *s) { sb_put(b, s); free(s); }
static char *fmt_mat(Mat *m) {
    SB b = {0}; sb_put(&b, "[");
    for (int i = 0; i < m->r; i++) { if (i) sb_put(&b, " | "); for (int j = 0; j < m->c; j++) { if (j) sb_put(&b, ", "); sb_own(&b, fmt_float(m->d[i * m->c + j])); } }
    sb_put(&b, "]"); return b.s;
}
static char *fmt_item(Value v) {
    if (v.k == V_STR) { SB b = {0}; sb_put(&b, "\""); sb_put(&b, v.u.s); sb_put(&b, "\""); return b.s; }
    return fmt_any(v);
}
char *fmt_any(Value v) {
    switch (v.k) {
    case V_STR: return jb_strdup(v.u.s);
    case V_DATA: return data_hex(v);
    case V_MAT: return fmt_mat(v.u.m);
    case V_LIST: { SB b = {0}; sb_put(&b, "["); for (int i = 0; i < v.u.l->n; i++) { if (i) sb_put(&b, ", "); sb_own(&b, fmt_item(v.u.l->items[i])); } sb_put(&b, "]"); return b.s; }
    }
    if (v.u.n == trunc(v.u.n) && fabs(v.u.n) < 1e15) return num_str(v.u.n);
    return fmt_float(v.u.n);
}
char *fmt_val(Value v, int ty) {
    if (v.k == V_STR) return jb_strdup(v.u.s);
    if (ty == TY_FLOAT && v.k == V_NUM) return fmt_float(v.u.n);
    if (ty == TY_ANY || isL(ty) || v.k != V_NUM) return fmt_any(v);
    return num_str(v.u.n);
}
char *kind_txt(Value v) {
    if (v.k == V_STR) return jb_fmt("\"%s\"", v.u.s);
    if (v.k == V_NUM) return fmt_any(v);
    if (v.k == V_MAT) return jb_fmt("matrix[%d, %d]", v.u.m->r, v.u.m->c);
    if (v.k == V_DATA) return jb_strdup("data");
    return jb_strdup(v.u.l->comp ? "compound" : "리스트");
}
const char *ty_name(int ty) {
    static const char *N[] = {"?","int","float","string","bit","matrix","list","compound","any","matrix[1, 2]","색","json","data"};
    return ty >= 0 && ty <= TY_DATA ? N[ty] : "?";
}

/* ---------- JSON ---------- */
static void json_str(SB *b, const char *s) {
    sb_put(b, "\"");
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char t[8];
        switch (*p) {
        case '"': sb_put(b, "\\\""); break;
        case '\\': sb_put(b, "\\\\"); break;
        case '\n': sb_put(b, "\\n"); break;
        case '\r': sb_put(b, "\\r"); break;
        case '\t': sb_put(b, "\\t"); break;
        case '\b': sb_put(b, "\\b"); break;
        case '\f': sb_put(b, "\\f"); break;
        default:
            if (*p < 0x20) { snprintf(t, sizeof t, "\\u%04x", *p); sb_put(b, t); }
            else { t[0] = (char)*p; t[1] = 0; sb_put(b, t); }
        }
    }
    sb_put(b, "\"");
}
static void json_num(SB *b, double v) { if (!isfinite(v)) { sb_put(b, "null"); return; } sb_own(b, num_str(v)); }
static void json_rec(SB *b, Value v) {
    switch (v.k) {
    case V_NUM: json_num(b, v.u.n); return;
    case V_STR: json_str(b, v.u.s); return;
    case V_DATA: { char *h = data_hex_all(v); json_str(b, h); free(h); return; }
    case V_MAT: {
        Mat *m = v.u.m;
        if (m->r == 1) { sb_put(b, "["); for (int j = 0; j < m->c; j++) { if (j) sb_put(b, ","); json_num(b, m->d[j]); } sb_put(b, "]"); return; }
        sb_put(b, "[");
        for (int i = 0; i < m->r; i++) { if (i) sb_put(b, ","); sb_put(b, "["); for (int j = 0; j < m->c; j++) { if (j) sb_put(b, ","); json_num(b, m->d[i * m->c + j]); } sb_put(b, "]"); }
        sb_put(b, "]"); return;
    }
    case V_LIST: sb_put(b, "["); for (int i = 0; i < v.u.l->n; i++) { if (i) sb_put(b, ","); json_rec(b, v.u.l->items[i]); } sb_put(b, "]"); return;
    }
}
char *json_of(Value v) { SB b = {0}; sb_put(&b, ""); json_rec(&b, v); return b.s; }

typedef struct { const char *p; int obj; } JP;
static void jws(JP *j) { while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') j->p++; }
static int jval(JP *j, Value *out);
static int u8put(char *o, unsigned c) {
    if (c < 0x80) { o[0] = (char)c; return 1; }
    if (c < 0x800) { o[0] = (char)(0xC0 | c >> 6); o[1] = (char)(0x80 | (c & 63)); return 2; }
    if (c < 0x10000) { o[0] = (char)(0xE0 | c >> 12); o[1] = (char)(0x80 | (c >> 6 & 63)); o[2] = (char)(0x80 | (c & 63)); return 3; }
    o[0] = (char)(0xF0 | c >> 18); o[1] = (char)(0x80 | (c >> 12 & 63)); o[2] = (char)(0x80 | (c >> 6 & 63)); o[3] = (char)(0x80 | (c & 63)); return 4;
}
static int hex4(const char *q, unsigned *out) {
    unsigned c = 0;
    for (int k = 0; k < 4; k++) { char h = q[k]; c <<= 4; if (h >= '0' && h <= '9') c |= h - '0'; else if (h >= 'a' && h <= 'f') c |= h - 'a' + 10; else if (h >= 'A' && h <= 'F') c |= h - 'A' + 10; else return 0; }
    *out = c; return 1;
}
static int jstr(JP *j, char **out) {
    if (*j->p != '"') return 0; j->p++;
    size_t cap = 32, n = 0; char *s = jb_alloc(cap);
    while (*j->p && *j->p != '"') {
        if (n + 8 > cap) { cap *= 2; s = jb_realloc(s, cap); }
        if ((unsigned char)*j->p < 0x20) { free(s); return 0; }
        if (*j->p == '\\') {
            j->p++;
            char e = *j->p++;
            switch (e) {
            case 'n': s[n++] = '\n'; break; case 't': s[n++] = '\t'; break; case 'r': s[n++] = '\r'; break;
            case 'b': s[n++] = '\b'; break; case 'f': s[n++] = '\f'; break;
            case '"': case '\\': case '/': s[n++] = e; break;
            case 'u': {
                unsigned c; if (!hex4(j->p, &c)) { free(s); return 0; } j->p += 4;
                unsigned d;
                if (c >= 0xD800 && c < 0xDC00 && j->p[0] == '\\' && j->p[1] == 'u' && hex4(j->p + 2, &d) && d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); j->p += 6; }
                n += u8put(s + n, c); break;
            }
            default: free(s); return 0;
            }
        } else s[n++] = *j->p++;
    }
    if (*j->p != '"') { free(s); return 0; }
    j->p++; s[n] = 0; *out = s; return 1;
}
static int jval(JP *j, Value *out) {
    jws(j);
    char c = *j->p;
    if (c == '[') {
        j->p++; Value l = v_list(1); jws(j);
        if (*j->p == ']') { j->p++; *out = l; return 1; }
        for (;;) {
            Value x; if (!jval(j, &x)) { v_free(&l); return 0; }
            list_push(l.u.l, x); jws(j);
            if (*j->p == ',') { j->p++; continue; }
            if (*j->p == ']') { j->p++; break; }
            v_free(&l); return 0;
        }
        *out = l; return 1;
    }
    if (c == '{') { j->obj = 1; return 0; }
    if (c == '"') { char *s; if (!jstr(j, &s)) return 0; *out = v_strown(s); return 1; }
    if (!strncmp(j->p, "true", 4)) { j->p += 4; *out = v_num(1); return 1; }
    if (!strncmp(j->p, "false", 5)) { j->p += 5; *out = v_num(0); return 1; }
    if (!strncmp(j->p, "null", 4)) { j->p += 4; *out = v_num(0); return 1; }
    if (c == '-' || (c >= '0' && c <= '9')) { char *e; double v = strtod(j->p, &e); if (e == j->p) return 0; j->p = e; *out = v_num(v); return 1; }
    return 0;
}
int json_parse(const char *s, Value *out) {
    JP j = {s, 0};
    if (!jval(&j, out)) return j.obj ? -1 : 0;
    jws(&j);
    if (*j.p) { v_free(out); return 0; }
    return 1;
}
