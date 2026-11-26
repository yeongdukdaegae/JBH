/* jdh — JawBreaker Development Helper 명령
 *   라이브러리 틀 만들기 · 검사 · 굽기(.jbx) · 프로젝트에 적용
 *   라이브러리는 C 로 한 번만 씀 — jdh lib build 가 웹용(WebAssembly)으로 컴파일해 C 소스와 함께 .jbx 에 담음
 *   .jbx: "JBLX" u32 개수 · (u32 경로 길이 · 경로 · u32 크기 · 내용)…   (모두 little-endian)
 *         jdl.json (선언) · lib.wasm (웹용) · native/… (C 소스 · EXE 용) · doc/… (설명서)
 */
#define JDH_VERSION "0.5.0"
#define JDH_ENGINE_MAJOR 1
#define JDH_ENGINE_MINOR 3   /* 이 JDH 와 짝인 엔진 버전 (에디터의 JB_VER) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <unistd.h>
#define MKDIR(p) mkdir(p, 0755)
#endif

/* ---------- 작은 도구 ---------- */
static void die(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); fputs("✖ ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr); va_end(ap); exit(1);
}
static void *xalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) die("메모리가 부족합니다"); return p; }
static char *xdup(const char *s) { size_t n = strlen(s); char *r = xalloc(n + 1); memcpy(r, s, n + 1); return r; }
static char *fmtdup(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    char *r = xalloc(n + 1); va_start(ap, fmt); vsnprintf(r, n + 1, fmt, ap); va_end(ap); return r;
}
static int isDir(const char *p) { struct stat st; return !stat(p, &st) && S_ISDIR(st.st_mode); }
static int isFile(const char *p) { struct stat st; return !stat(p, &st) && S_ISREG(st.st_mode); }
static unsigned char *readAll(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    unsigned char *b = xalloc(n + 1); size_t r = fread(b, 1, n, f); fclose(f); b[r] = 0; if (len) *len = r; return b;
}
static void writeAll(const char *path, const void *data, size_t len) {
    FILE *f = fopen(path, "wb"); if (!f) die("파일을 쓸 수 없습니다: %s", path);
    if (len && fwrite(data, 1, len, f) != len) die("파일을 쓰다가 실패했습니다: %s", path);
    fclose(f);
}
static void mkdirs(const char *path) {   /* 중간 폴더까지 */
    char *p = xdup(path);
    for (char *q = p + 1; *q; q++) if (*q == '/' || *q == '\\') { char c = *q; *q = 0; MKDIR(p); *q = c; }
    MKDIR(p); free(p);
}
static const char *baseName(const char *p) { const char *a = strrchr(p, '/'), *b = strrchr(p, '\\'); const char *s = a > b ? a : b; return s ? s + 1 : p; }
static int endsWith(const char *s, const char *t) { size_t a = strlen(s), b = strlen(t); return a >= b && !strcmp(s + a - b, t); }

/* ---------- JSON (읽기만) ---------- */
enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ };
typedef struct JV { int k; double num; char *str; struct JV **items; char **keys; int n; } JV;
typedef struct { const char *s; const char *err; } JP;
static void jws(JP *p) { while (*p->s == ' ' || *p->s == '\t' || *p->s == '\n' || *p->s == '\r') p->s++; }
static JV *jnew(int k) { JV *v = xalloc(sizeof *v); memset(v, 0, sizeof *v); v->k = k; return v; }
static void utf8put(char **w, unsigned c) {
    if (c < 0x80) *(*w)++ = (char)c;
    else if (c < 0x800) { *(*w)++ = (char)(0xC0 | (c >> 6)); *(*w)++ = (char)(0x80 | (c & 63)); }
    else if (c < 0x10000) { *(*w)++ = (char)(0xE0 | (c >> 12)); *(*w)++ = (char)(0x80 | ((c >> 6) & 63)); *(*w)++ = (char)(0x80 | (c & 63)); }
    else { *(*w)++ = (char)(0xF0 | (c >> 18)); *(*w)++ = (char)(0x80 | ((c >> 12) & 63)); *(*w)++ = (char)(0x80 | ((c >> 6) & 63)); *(*w)++ = (char)(0x80 | (c & 63)); }
}
static char *jstr(JP *p) {
    if (*p->s != '"') { p->err = "문자열이 필요합니다"; return NULL; }
    p->s++;
    char *out = xalloc(strlen(p->s) + 1), *w = out;
    while (*p->s && *p->s != '"') {
        if (*p->s == '\\') {
            p->s++;
            switch (*p->s) {
                case 'n': *w++ = '\n'; break; case 't': *w++ = '\t'; break; case 'r': *w++ = '\r'; break;
                case 'b': *w++ = '\b'; break; case 'f': *w++ = '\f'; break;
                case 'u': {
                    unsigned c = 0; for (int i = 1; i <= 4; i++) { if (!isxdigit((unsigned char)p->s[i])) { p->err = "\\u 뒤에는 16진수 4자리가 옵니다"; free(out); return NULL; } char h[2] = { p->s[i], 0 }; c = c * 16 + (unsigned)strtol(h, NULL, 16); }
                    p->s += 4;
                    if (c >= 0xD800 && c < 0xDC00 && p->s[1] == '\\' && p->s[2] == 'u') {
                        unsigned d = 0; int okh = 1; for (int i = 3; i <= 6; i++) { if (!isxdigit((unsigned char)p->s[i])) { okh = 0; break; } char h[2] = { p->s[i], 0 }; d = d * 16 + (unsigned)strtol(h, NULL, 16); }
                        if (okh && d >= 0xDC00 && d < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00); p->s += 6; }
                    }
                    utf8put(&w, c); break;
                }
                case 0: p->err = "문자열이 끝나지 않았습니다"; free(out); return NULL;
                default: *w++ = *p->s;
            }
            p->s++;
        } else *w++ = *p->s++;
    }
    if (*p->s != '"') { p->err = "문자열이 끝나지 않았습니다"; free(out); return NULL; }
    p->s++; *w = 0; return out;
}
static JV *jval(JP *p, int depth) {
    if (depth > 64) { p->err = "너무 깊게 겹쳐 있습니다"; return NULL; }
    jws(p);
    if (*p->s == '"') { char *s = jstr(p); if (!s) return NULL; JV *v = jnew(J_STR); v->str = s; return v; }
    if (*p->s == '{' || *p->s == '[') {
        int obj = *p->s == '{'; JV *v = jnew(obj ? J_OBJ : J_ARR); p->s++; jws(p);
        if (*p->s == (obj ? '}' : ']')) { p->s++; return v; }
        for (;;) {
            char *key = NULL;
            if (obj) { jws(p); key = jstr(p); if (!key) return NULL; jws(p); if (*p->s != ':') { p->err = "':' 이 필요합니다"; return NULL; } p->s++; }
            JV *it = jval(p, depth + 1); if (!it) return NULL;
            v->items = realloc(v->items, sizeof(JV*) * (v->n + 1)); v->keys = realloc(v->keys, sizeof(char*) * (v->n + 1));
            v->items[v->n] = it; v->keys[v->n] = key; v->n++;
            jws(p);
            if (*p->s == ',') { p->s++; continue; }
            if (*p->s == (obj ? '}' : ']')) { p->s++; return v; }
            p->err = obj ? "',' 나 '}' 이 필요합니다" : "',' 나 ']' 이 필요합니다"; return NULL;
        }
    }
    if (!strncmp(p->s, "true", 4)) { p->s += 4; JV *v = jnew(J_BOOL); v->num = 1; return v; }
    if (!strncmp(p->s, "false", 5)) { p->s += 5; return jnew(J_BOOL); }
    if (!strncmp(p->s, "null", 4)) { p->s += 4; return jnew(J_NULL); }
    char *end; double d = strtod(p->s, &end);
    if (end == p->s) { p->err = "값이 필요합니다"; return NULL; }
    p->s = end; JV *v = jnew(J_NUM); v->num = d; return v;
}
static JV *jparse(const char *text, const char **err, int *line) {
    JP p = { text, NULL };
    if ((unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) p.s += 3;   /* BOM */
    JV *v = jval(&p, 0);
    if (v) { jws(&p); if (*p.s) { p.err = "선언 뒤에 남는 글자가 있습니다"; v = NULL; } }
    if (!v) { *err = p.err ? p.err : "읽을 수 없습니다"; int ln = 1; for (const char *q = text; q < p.s; q++) if (*q == '\n') ln++; *line = ln; }
    return v;
}
static JV *jget(JV *o, const char *key) { if (!o || o->k != J_OBJ) return NULL; for (int i = 0; i < o->n; i++) if (!strcmp(o->keys[i], key)) return o->items[i]; return NULL; }
static const char *jgets(JV *o, const char *key) { JV *v = jget(o, key); return v && v->k == J_STR ? v->str : NULL; }

/* ---------- 라이브러리 폴더 읽기 · 검사 ---------- */
typedef struct { char *name; unsigned char *data; size_t len; } Ent;
typedef struct {
    char *dir, *id, *version;
    char **csrc; int ncsrc;      /* native/ 의 C 파일 (폴더 기준 경로) */
    unsigned char *man; size_t manLen;
    JV *json;
    Ent *ents; int nents;
    int errors, warns;
    int reqMa, reqMi, needEng;   /* 선언의 requires.engine (없으면 -1) · 리스트 · 행렬 인자를 쓰면 needEng = 3 (엔진 1.3 부터) */
} Lib;
static void lerr(Lib *L, const char *fmt, ...) { va_list ap; va_start(ap, fmt); fputs("  ✖ ", stdout); vprintf(fmt, ap); putchar('\n'); va_end(ap); L->errors++; }
static void lwarn(Lib *L, const char *fmt, ...) { va_list ap; va_start(ap, fmt); fputs("  ⚠ ", stdout); vprintf(fmt, ap); putchar('\n'); va_end(ap); L->warns++; }
static int inSet(const char *s, const char **set) { for (; *set; set++) if (!strcmp(s, *set)) return 1; return 0; }
/* 인자 형식 — 하나짜리 (int · float · string · bit · color · mat12) · 리스트 "[int, string]" (정해진 개수) · "[int, ...]" (몇 개든 · 0개도)
 *   · 행렬 "matrix[2, 3]" (크기 자리에 ... 를 쓰면 아무 크기) — 에디터의 jbxPT 와 같은 규칙 */
enum { PK_INT = 1, PK_FLOAT, PK_STRING, PK_BIT, PK_COLOR, PK_MAT12, PK_LIST, PK_MAT };
typedef struct { int k, n, var, el[8], r, c; } PTy;
#define PT_HELP "int · float · string · bit · color · mat12 · [int, float] (리스트 · 항목은 int · float · string · bit 를 8개까지) · [int, ...] (몇 개든) · matrix[2, 3] (행렬 · 크기 자리에 ... 를 쓰면 아무 크기)"
static int ptCode(const char *t, size_t n) {
    static const char *nm[] = { "int", "float", "string", "bit", "color", "mat12" };
    for (int i = 0; i < 6; i++) if (strlen(nm[i]) == n && !strncmp(t, nm[i], n)) return i + 1;
    return 0;
}
static const char *ptSkip(const char *p) { while (*p == ' ' || *p == '\t') p++; return p; }
static int ptParse(const char *s, PTy *o) {
    memset(o, 0, sizeof *o);
    const char *p = ptSkip(s), *e = p + strlen(p); while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
    if (e == p) return 0;
    if (*p != '[' && strncmp(p, "matrix", 6)) { o->k = ptCode(p, (size_t)(e - p)); return o->k != 0; }
    if (*p == 'm') {   /* matrix[행, 열] */
        p = ptSkip(p + 6); if (*p != '[' || e[-1] != ']') return 0; p++;
        int d[2];
        for (int q = 0; q < 2; q++) {
            p = ptSkip(p);
            if (!strncmp(p, "...", 3)) { d[q] = 0; p += 3; }
            else { if (*p < '1' || *p > '9') return 0; long v = 0; while (*p >= '0' && *p <= '9') { v = v * 10 + (*p++ - '0'); if (v > 100000) return 0; } d[q] = (int)v; }
            p = ptSkip(p);
            if (*p != (q ? ']' : ',')) return 0;
            p++;
        }
        if (ptSkip(p) != e) return 0;
        o->k = PK_MAT; o->r = d[0]; o->c = d[1]; return 1;
    }
    /* [형식, 형식] · [형식, ...] */
    if (e[-1] != ']') return 0;
    p++; e--;
    for (;;) {
        p = ptSkip(p); const char *q = p; while (q < e && *q != ',' && *q != ' ' && *q != '\t') q++;
        if (q - p == 3 && !strncmp(p, "...", 3)) {
            if (o->n != 1 || o->var) return 0;   /* ... 는 [형식, ...] 꼴로만 */
            o->var = 1;
        } else {
            int c = ptCode(p, (size_t)(q - p));
            if (!c || c > PK_BIT || o->var || o->n >= 8) return 0;
            o->el[o->n++] = c;
        }
        p = ptSkip(q);
        if (p == e) break;
        if (*p != ',') return 0;
        p++;
    }
    if (!o->n) return 0;
    o->k = PK_LIST; return 1;
}
/* 이 인자가 C 에서 몇 칸인지 (EXE 의 JdhArg 칸 · wasm 의 인자) */
static int ptSlots(const PTy *t) { return t->k == PK_MAT12 ? 2 : t->k == PK_LIST ? (t->var ? 2 : t->n) : t->k == PK_MAT ? 1 + !t->r + !t->c : 1; }
static const char *ptCty(int k) { return k == PK_FLOAT ? "double" : k == PK_STRING || k == PK_COLOR ? "const char *" : "int"; }
/* C 의 인자 꼴 (안내 문구용) */
static char *ptProto(const PTy *t, char *b, size_t n) {
    if (t->k == PK_MAT12) snprintf(b, n, "double x, double y");
    else if (t->k == PK_LIST && t->var) snprintf(b, n, "%s, int n", t->el[0] == PK_FLOAT ? "const double *items" : t->el[0] == PK_STRING ? "const char *const *items" : "const int *items");
    else if (t->k == PK_LIST) { b[0] = 0; for (int i = 0; i < t->n; i++) { size_t L = strlen(b); snprintf(b + L, n - L, "%s%s", i ? ", " : "", ptCty(t->el[i])); } }
    else if (t->k == PK_MAT) snprintf(b, n, "const double *m%s%s", t->r ? "" : ", int rows", t->c ? "" : ", int cols");
    else snprintf(b, n, "%s", ptCty(t->k));
    return b;
}
static const char *VT[] = { "int", "float", "string", "bit", NULL };
static const char *PERMS[] = { "draw", "objects.read", "objects.write", "files.read", NULL };
static const char *BUILTIN[] = { "jawbreaker", "DisPlayer", "colliders", "Canvas", "Magellan", "soundMaster", "SeongBin", NULL };
static const char *HOSTKEYS[] = { "values", NULL };
/* 이름 규칙: 영문 · 숫자 · _ (첫 글자는 숫자 아님) · UTF-8 글자(한글 등)도 허용 */
static int okName(const char *s) {
    if (!*s || isdigit((unsigned char)*s)) return 0;
    for (; *s; s++) { unsigned char c = (unsigned char)*s; if (!(isalnum(c) || c == '_' || c >= 0x80)) return 0; }
    return 1;
}
static int okRelPath(const char *p) { return *p && p[0] != '/' && p[0] != '\\' && !strstr(p, "..") && !strchr(p, ':'); }
static void addEnt(Lib *L, const char *name, unsigned char *data, size_t len) {
    L->ents = realloc(L->ents, sizeof(Ent) * (L->nents + 1));
    L->ents[L->nents].name = xdup(name); L->ents[L->nents].data = data; L->ents[L->nents].len = len; L->nents++;
}
static int cmpStr(const void *a, const void *b) { return strcmp(*(char*const*)a, *(char*const*)b); }
static void addFolder(Lib *L, const char *sub) {   /* doc/ · native/ 바로 아래 파일을 이름 순으로 */
    char *dir = fmtdup("%s/%s", L->dir, sub); DIR *d = opendir(dir); if (!d) { free(dir); return; }
    char **names = NULL; int n = 0; struct dirent *e;
    while ((e = readdir(d))) { if (e->d_name[0] == '.') continue; names = realloc(names, sizeof(char*) * (n + 1)); names[n++] = xdup(e->d_name); }
    closedir(d);
    qsort(names, n, sizeof(char*), cmpStr);
    for (int i = 0; i < n; i++) {
        char *p = fmtdup("%s/%s", dir, names[i]);
        if (isFile(p)) { size_t len; unsigned char *b = readAll(p, &len); if (b) { char *nm = fmtdup("%s/%s", sub, names[i]); addEnt(L, nm, b, len); free(nm); } }
        free(p); free(names[i]);
    }
    free(names); free(dir);
}
static Lib *libLoad(const char *dir) {
    Lib *L = xalloc(sizeof *L); memset(L, 0, sizeof *L); L->reqMa = L->reqMi = -1;
    L->dir = xdup(dir);
    size_t dl = strlen(L->dir); while (dl > 1 && (L->dir[dl-1] == '/' || L->dir[dl-1] == '\\')) L->dir[--dl] = 0;
    char *mp = fmtdup("%s/jdl.json", L->dir);
    L->man = readAll(mp, &L->manLen);
    if (!L->man) die("%s 가 없습니다 — 라이브러리 폴더(jdl.json 이 있는 곳)에서 실행하거나 폴더를 적어 주세요", mp);
    const char *err; int line = 0;
    L->json = jparse((char*)L->man, &err, &line);
    if (!L->json) die("%s:%d  선언을 읽을 수 없습니다 — %s", mp, line, err);
    free(mp);
    return L;
}
static void libCheck(Lib *L) {
    JV *j = L->json;
    if (j->k != J_OBJ) { lerr(L, "jdl.json 은 { } 로 시작해야 합니다"); return; }
    JV *ver = jget(j, "jdl");
    if (!ver || ver->k != J_NUM || ver->num != 1) lerr(L, "\"jdl\": 1 이 필요합니다 (선언 형식 번호)");
    const char *id = jgets(j, "id");
    if (!id) lerr(L, "\"id\" (라이브러리 이름) 가 필요합니다");
    else if (!okName(id)) lerr(L, "id '%s' 은(는) 쓸 수 없습니다 — 글자 · 숫자 · _ 만, 숫자로 시작하지 않게", id);
    else if (inSet(id, BUILTIN)) lerr(L, "id '%s' 은(는) 내장 라이브러리 이름이라 쓸 수 없습니다", id);
    else L->id = xdup(id);
    const char *v = jgets(j, "version");
    if (!v) lerr(L, "\"version\" 이 필요합니다 (예: \"1.0.0\")");
    else { int ok = *v != 0; for (const char *q = v; *q; q++) if (!(isalnum((unsigned char)*q) || *q == '.' || *q == '-' || *q == '_')) ok = 0; if (!ok) lerr(L, "version '%s' 에는 글자 · 숫자 · . - _ 만 씁니다", v); else L->version = xdup(v); }
    /* 권한 */
    JV *pm = jget(j, "permissions");
    if (pm) {
        if (pm->k != J_ARR) lerr(L, "\"permissions\" 는 [ ] 목록이어야 합니다");
        else for (int i = 0; i < pm->n; i++) {
            if (pm->items[i]->k != J_STR || !inSet(pm->items[i]->str, PERMS)) lerr(L, "알 수 없는 권한입니다: %s (쓸 수 있는 것: draw · objects.read · objects.write · files.read)", pm->items[i]->k == J_STR ? pm->items[i]->str : "(문자열 아님)");
        }
    }
    { const char *en = jgets(jget(j, "requires"), "engine");
      JV *enj = jget(jget(j, "requires"), "engine");
      if (enj && !en) lerr(L, "requires.engine 은 \">=1.2\" 꼴의 문자열이어야 합니다");
      if (en) {
          /* 에디터와 같은 규칙: 앞뒤 빈칸 · ">=" · 빈칸 · 주.부[.수] 만 */
          int ma = -1, mi = -1, okf = 0; const char *q = en; while (*q == ' ' || *q == '\t') q++;
          if (!strncmp(q, ">=", 2)) {
              q += 2; while (*q == ' ' || *q == '\t') q++;
              if (isdigit((unsigned char)*q)) { ma = (int)strtol(q, (char **)&q, 10);
                  if (*q == '.' && isdigit((unsigned char)q[1])) { mi = (int)strtol(q + 1, (char **)&q, 10);
                      if (*q == '.' && isdigit((unsigned char)q[1])) { q++; while (isdigit((unsigned char)*q)) q++; }
                      while (*q == ' ' || *q == '\t') q++;
                      okf = !*q; } }
          }
          if (okf) { L->reqMa = ma; L->reqMi = mi; }
          if (!okf) lerr(L, "requires.engine 은 \">=1.2\" 꼴로 씁니다 (받은 값: %s)", en);
          else if (ma > JDH_ENGINE_MAJOR || (ma == JDH_ENGINE_MAJOR && mi > JDH_ENGINE_MINOR)) lerr(L, "requires.engine 이 이 JDH 의 엔진(%d.%d)보다 높습니다: %s", JDH_ENGINE_MAJOR, JDH_ENGINE_MINOR, en);
      } }
    /* 필요한 내장 라이브러리 */
    JV *rq = jget(jget(j, "requires"), "libs");
    if (rq) {
        if (rq->k != J_ARR) lerr(L, "\"requires\".\"libs\" 는 [ ] 목록이어야 합니다");
        else for (int i = 0; i < rq->n; i++) if (rq->items[i]->k != J_STR || !inSet(rq->items[i]->str, BUILTIN)) lerr(L, "requires.libs 에는 내장 라이브러리 이름만 씁니다: %s", rq->items[i]->k == J_STR ? rq->items[i]->str : "(문자열 아님)");
    }
    if (jget(j, "web")) lerr(L, "\"web\" (JS 구현) 은 더 이상 쓰지 않습니다 — 구현은 C 로 한 번만 쓰고 \"native\" 에 적습니다");
    /* C 구현 */
    JV *nt = jget(j, "native");
    if (!nt || nt->k != J_ARR || !nt->n) lerr(L, "\"native\" (C 구현 파일 목록) 가 필요합니다 (예: [\"native/%s.c\"])", L->id ? L->id : "lib");
    else for (int i = 0; i < nt->n; i++) {
        if (nt->items[i]->k != J_STR || !okRelPath(nt->items[i]->str)) { lerr(L, "native 경로가 올바르지 않습니다"); continue; }
        const char *np = nt->items[i]->str;
        if (strncmp(np, "native/", 7) || strchr(np + 7, '/') || !endsWith(np, ".c")) { lerr(L, "C 파일은 native/ 폴더 바로 아래의 .c 파일이어야 합니다: %s", np); continue; }
        char *fp = fmtdup("%s/%s", L->dir, np);
        if (!isFile(fp)) lerr(L, "C 구현 파일이 없습니다: %s", np);
        else { L->csrc = realloc(L->csrc, sizeof(char*) * (L->ncsrc + 1)); L->csrc[L->ncsrc++] = xdup(np); }
        free(fp);
    }
    /* 명령어 */
    JV *cm = jget(j, "commands"); int ncmd = 0;
    if (cm && cm->k != J_OBJ) lerr(L, "\"commands\" 는 { } 여야 합니다");
    else if (cm) for (int i = 0; i < cm->n; i++) {
        const char *name = cm->keys[i]; JV *c = cm->items[i]; ncmd++;
        if (!okName(name)) { lerr(L, "명령어 이름 '%s' 은(는) 쓸 수 없습니다", name); continue; }
        if (inSet(name, HOSTKEYS)) { lerr(L, "'%s' 은(는) 명령어 이름으로 쓸 수 없습니다 (값 목록에 쓰는 이름)", name); continue; }
        for (int k = 0; k < i; k++) if (!strcmp(cm->keys[k], name)) lerr(L, "명령어 '%s' 이(가) 두 번 선언되었습니다", name);
        if (c->k != J_OBJ) { lerr(L, "명령어 '%s' 의 선언은 { \"params\": [ … ] } 꼴이어야 합니다", name); continue; }
        JV *ps = jget(c, "params");
        if (!ps || ps->k != J_ARR) lerr(L, "명령어 '%s': \"params\" 목록이 필요합니다 (인자가 없으면 [])", name);
        else if (ps->n > 8) lerr(L, "명령어 '%s': 인자는 8개까지입니다 (받은 것: %d개)", name, ps->n);
        else { int slots = 0, coll = 0; PTy pt;
            for (int k = 0; k < ps->n; k++) {
                if (ps->items[k]->k != J_STR || !ptParse(ps->items[k]->str, &pt)) { lerr(L, "명령어 '%s' 의 %d번째 인자 형식 '%s' 은(는) 쓸 수 없습니다 (%s)", name, k + 1, ps->items[k]->k == J_STR ? ps->items[k]->str : "?", PT_HELP); continue; }
                slots += ptSlots(&pt); if (pt.k == PK_LIST || pt.k == PK_MAT) coll = 1;
            }
            if (slots > 64) lerr(L, "명령어 '%s': 인자가 C 에서 너무 많습니다 (리스트 항목을 모두 합쳐 64칸까지 · 받은 것: %d칸)", name, slots);
            if (coll) L->needEng = 3;
        }
        if (jget(c, "returns")) lerr(L, "명령어 '%s': \"returns\" 는 지원하지 않습니다 — 돌려줄 값은 \"values\" 에 선언하고 코드에서 별칭.값이름 으로 읽습니다", name);
    }
    /* 값 */
    JV *vs = jget(j, "values"); int nval = 0;
    if (vs && vs->k != J_OBJ) lerr(L, "\"values\" 는 { \"이름\": \"형식\" } 꼴이어야 합니다");
    else if (vs) for (int i = 0; i < vs->n; i++) {
        const char *name = vs->keys[i]; nval++;
        if (!okName(name)) { lerr(L, "값 이름 '%s' 은(는) 쓸 수 없습니다", name); continue; }
        if (vs->items[i]->k != J_STR || !inSet(vs->items[i]->str, VT)) lerr(L, "값 '%s' 의 형식은 int · float · string · bit 중 하나여야 합니다", name);
        if (cm && cm->k == J_OBJ && jget(cm, name)) lerr(L, "'%s' 이(가) 명령어와 값 양쪽에 선언되었습니다", name);
    }
    if (!ncmd && !nval) lwarn(L, "명령어도 값도 선언하지 않았습니다");
    if (L->needEng && (L->reqMa < 1 || (L->reqMa == 1 && L->reqMi < L->needEng)))
        lwarn(L, "리스트 · 행렬 인자는 엔진 1.%d 부터 씁니다 — \"requires\": { \"engine\": \">=1.%d\" } 로 적어 두면 예전 에디터가 알기 쉬운 오류를 냅니다", L->needEng, L->needEng);
}

/* ---------- C → WebAssembly 컴파일 · 결과 검사 ---------- */
static char *EXEDIR = NULL;   /* jdh 실행 파일이 있는 폴더 (찾지 못하면 NULL) */
static char *homeDir(void) {   /* JDH 설치 폴더: JDH_HOME, 없으면 실행 파일의 위 폴더 */
    const char *h = getenv("JDH_HOME");
    if (h && *h && isDir(h)) return xdup(h);
    if (EXEDIR) { char *p = fmtdup("%s/..", EXEDIR); if (isDir(p)) return p; free(p); }
    return NULL;
}
static char *sdkDir(void) {
    const char *e = getenv("JDH_SDK"); if (e && *e) return xdup(e);
    char *h = homeDir(); if (h) { char *p = fmtdup("%s/sdk", h), *f = fmtdup("%s/jdh.h", p); int ok = isFile(f); free(f); if (ok) return p; free(p); }
    return NULL;
}
/* 컴파일러 명령 (따옴표까지 붙인 꼴): JDH_ZIG → 설치 폴더의 toolchain/zig → PATH 의 zig */
static char *zigCmd(void) {
    const char *e = getenv("JDH_ZIG"); if (e && *e) return xdup(e);
    char *h = homeDir();
    if (h) {
#ifdef _WIN32
        char *p = fmtdup("%s\\toolchain\\zig\\zig.exe", h);
#else
        char *p = fmtdup("%s/toolchain/zig/zig", h);
#endif
        if (isFile(p)) { char *q = fmtdup("\"%s\"", p); free(p); return q; }
        free(p);
    }
    return xdup("zig");
}
static char *catf(char *a, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    size_t al = strlen(a); a = realloc(a, al + n + 1); va_start(ap, fmt); vsnprintf(a + al, n + 1, fmt, ap); va_end(ap); return a;
}
static int compileWasm(Lib *L, const char *out) {
    char *sdk = sdkDir();
    if (!sdk) { lerr(L, "jdh.h 를 찾을 수 없습니다 — JDH 를 다시 설치하거나 환경 변수 JDH_HOME 을 확인하세요"); return 0; }
    char *zig = zigCmd();
    char *cmd = fmtdup("%s cc -target wasm32-wasi -mexec-model=reactor -O2 -s -Wall -Wno-unused-function -I\"%s\" -o \"%s\"", zig, sdk, out);
    for (int i = 0; i < L->ncsrc; i++) cmd = catf(cmd, " \"%s/%s\"", L->dir, L->csrc[i]);
#ifdef _WIN32
    char *full = fmtdup("\"%s\"", cmd);   /* cmd.exe: 따옴표로 시작하는 명령은 통째로 한 번 더 감쌈 */
#else
    char *full = xdup(cmd);
#endif
    remove(out);
    fflush(stdout);
    int r = system(full);
    free(full); free(cmd); free(zig); free(sdk);
    if (r != 0 || !isFile(out)) { lerr(L, "C 구현을 컴파일하지 못했습니다 (위 메시지를 확인하세요 · 컴파일러가 없으면 JDH 를 다시 설치)"); return 0; }
    return 1;
}
/* wasm 읽기: 내보낸 함수 이름 → 꼴 (인자 형식들 '>' 결과 형식들 · i = 32비트 정수, d = double, 그 밖은 ?) */
typedef struct { const unsigned char *p, *end; int bad; } WR;
static uint32_t wleb(WR *r) { uint32_t v = 0; int sh = 0; for (;;) { if (r->p >= r->end) { r->bad = 1; return 0; } unsigned char b = *r->p++; v |= (uint32_t)(b & 0x7F) << sh; if (!(b & 0x80)) return v; sh += 7; if (sh > 35) { r->bad = 1; return 0; } } }
static char wty(unsigned char t) { return t == 0x7F ? 'i' : t == 0x7C ? 'd' : t == 0x7E ? 'l' : t == 0x7D ? 'f' : '?'; }
typedef struct { char *name; char *sig; } WExp;
static char **WIMP; static int NWIMP;   /* 마지막으로 읽은 wasm 이 엔진(jdh)에서 가져다 쓰는 함수 */
/* host 함수 → 필요한 권한 (NULL = 늘 쓸 수 있음) — 에디터의 JBX_HOST 와 같게 */
static const char *HOSTFN[][2] = {
    {"log", NULL}, {"error", NULL},
    {"draw_clear", "draw"}, {"draw_rect", "draw"}, {"draw_circle", "draw"}, {"draw_line", "draw"}, {"draw_text", "draw"},
    {"obj_exists", "objects.read"}, {"obj_pos", "objects.read"}, {"obj_scale", "objects.read"}, {"obj_vel", "objects.read"}, {"obj_color", "objects.read"}, {"obj_text", "objects.read"},
    {"obj_set_pos", "objects.write"}, {"obj_set_scale", "objects.write"}, {"obj_set_vel", "objects.write"}, {"obj_set_color", "objects.write"}, {"obj_set_text", "objects.write"},
    {"file_size", "files.read"}, {"file_read", "files.read"},
    {NULL, NULL}
};
/* 가져다 쓰는 host 함수가 모두 있는 것이고 그 권한이 선언에 있는지 — 문제마다 msg 를 부름 */
static int hostCheck(JV *perms, void (*msg)(void *, const char *, const char *), void *ud) {
    int bad = 0;
    for (int i = 0; i < NWIMP; i++) {
        int k = 0; while (HOSTFN[k][0] && strcmp(HOSTFN[k][0], WIMP[i])) k++;
        if (!HOSTFN[k][0]) { msg(ud, WIMP[i], NULL); bad++; continue; }
        const char *need = HOSTFN[k][1]; if (!need) continue;
        int has = 0; if (perms && perms->k == J_ARR) for (int q = 0; q < perms->n; q++) if (perms->items[q]->k == J_STR && !strcmp(perms->items[q]->str, need)) has = 1;
        if (!has) { msg(ud, WIMP[i], need); bad++; }
    }
    return bad;
}
static WExp *wasmExports(const unsigned char *b, size_t len, int *count) {
    *count = 0; NWIMP = 0;
    if (len < 8 || memcmp(b, "\0asm", 4)) return NULL;
    char **types = NULL; int ntypes = 0; uint32_t *ftype = NULL; int nfunc = 0, nimp = 0; uint32_t *itype = NULL;
    WExp *ex = NULL; WR r = { b + 8, b + len, 0 };
    while (r.p < r.end && !r.bad) {
        unsigned char id = *r.p++; uint32_t sz = wleb(&r); if (r.bad || (size_t)(r.end - r.p) < sz) break;
        WR s = { r.p, r.p + sz, 0 }; r.p += sz;
        if (id == 1) {
            uint32_t n = wleb(&s);
            for (uint32_t i = 0; i < n && !s.bad; i++) {
                if (s.p >= s.end || *s.p++ != 0x60) { s.bad = 1; break; }
                uint32_t np = wleb(&s); char *sig = xalloc(np + 40); uint32_t w = 0;
                for (uint32_t k = 0; k < np && s.p < s.end; k++) sig[w++] = wty(*s.p++);
                sig[w++] = '>';
                uint32_t nr = wleb(&s); for (uint32_t k = 0; k < nr && s.p < s.end && w < np + 38; k++) sig[w++] = wty(*s.p++);
                sig[w] = 0;
                types = realloc(types, sizeof(char*) * (ntypes + 1)); types[ntypes++] = sig;
            }
        } else if (id == 2) {
            uint32_t n = wleb(&s);
            for (uint32_t i = 0; i < n && !s.bad; i++) {
                uint32_t a = wleb(&s); const unsigned char *mod = s.p; s.p += a; uint32_t c = wleb(&s); const unsigned char *fn = s.p; s.p += c;
                if (s.p >= s.end) { s.bad = 1; break; }
                unsigned char kind = *s.p++;
                if (kind == 0) {
                    uint32_t t = wleb(&s); itype = realloc(itype, sizeof(uint32_t) * (nimp + 1)); itype[nimp++] = t;
                    if (a == 3 && !memcmp(mod, "jdh", 3) && c < 64) { char nm[64]; memcpy(nm, fn, c); nm[c] = 0; WIMP = realloc(WIMP, sizeof(char*) * (NWIMP + 1)); WIMP[NWIMP++] = xdup(nm); }
                }
                else if (kind == 1) { s.p++; uint32_t fl = wleb(&s); wleb(&s); if (fl & 1) wleb(&s); }
                else if (kind == 2) { uint32_t fl = wleb(&s); wleb(&s); if (fl & 1) wleb(&s); }
                else if (kind == 3) { s.p += 2; }
                else { s.bad = 1; }
            }
        } else if (id == 3) {
            uint32_t n = wleb(&s);
            for (uint32_t i = 0; i < n && !s.bad; i++) { uint32_t t = wleb(&s); ftype = realloc(ftype, sizeof(uint32_t) * (nfunc + 1)); ftype[nfunc++] = t; }
        } else if (id == 7) {
            uint32_t n = wleb(&s);
            for (uint32_t i = 0; i < n && !s.bad; i++) {
                uint32_t nl = wleb(&s); if ((size_t)(s.end - s.p) < nl + 1) { s.bad = 1; break; }
                char *name = xalloc(nl + 1); memcpy(name, s.p, nl); name[nl] = 0; s.p += nl;
                unsigned char kind = *s.p++; uint32_t idx = wleb(&s);
                if (kind != 0) { free(name); continue; }
                uint32_t t = idx < (uint32_t)nimp ? itype[idx] : (idx - nimp < (uint32_t)nfunc ? ftype[idx - nimp] : 0xFFFFFFFFu);
                ex = realloc(ex, sizeof(WExp) * (*count + 1));
                ex[*count].name = name; ex[*count].sig = t < (uint32_t)ntypes ? types[t] : "?"; (*count)++;
            }
        }
    }
    return ex;
}
static const char *expSig(WExp *ex, int n, const char *prefix, const char *name) {
    size_t pl = strlen(prefix);
    for (int i = 0; i < n; i++) if (!strncmp(ex[i].name, prefix, pl) && !strcmp(ex[i].name + pl, name)) return ex[i].sig;
    return NULL;
}
static void libHostMsg(void *ud, const char *fn, const char *perm) {
    Lib *L = ud;
    if (!perm) lerr(L, "jdh_%s 은(는) 엔진이 주는 함수가 아닙니다 (jdh.h 에 있는 것만 쓸 수 있습니다)", fn);
    else lerr(L, "jdh_%s 을(를) 쓰려면 선언의 \"permissions\" 에 \"%s\" 가 있어야 합니다", fn, perm);
}
/* 컴파일된 것이 선언과 맞는지: 명령어 · 값이 모두 있고 인자 형식이 같은지 */
static void checkWasm(Lib *L, const unsigned char *b, size_t len) {
    int n; WExp *ex = wasmExports(b, len, &n);
    if (!ex && !n) { lerr(L, "컴파일 결과를 읽을 수 없습니다"); return; }
    JV *cm = jget(L->json, "commands"), *vs = jget(L->json, "values");
    if (cm && cm->k == J_OBJ) for (int i = 0; i < cm->n; i++) {
        const char *name = cm->keys[i]; JV *ps = jget(cm->items[i], "params"); if (!ps || ps->k != J_ARR) continue;
        char want[256]; size_t w = 0; char proto[1024] = ""; int okp = 1;
        for (int k = 0; k < ps->n && w < sizeof want - 12; k++) {
            PTy t; char pb[256];
            if (ps->items[k]->k != J_STR || !ptParse(ps->items[k]->str, &t)) { okp = 0; break; }
            /* wasm 인자: float → f64 (d) · 나머지 (int · bit · 글자 주소 · 리스트 주소 · 개수) → i32 (i) */
            if (t.k == PK_LIST && !t.var) for (int q = 0; q < t.n && w < sizeof want - 12; q++) want[w++] = t.el[q] == PK_FLOAT ? 'd' : 'i';
            else if (t.k == PK_FLOAT || t.k == PK_MAT12) for (int q = 0; q < ptSlots(&t); q++) want[w++] = 'd';
            else for (int q = 0; q < ptSlots(&t); q++) want[w++] = 'i';
            ptProto(&t, pb, sizeof pb);
            if (strlen(proto) + strlen(pb) + 3 < sizeof proto) { if (k) strcat(proto, ", "); strcat(proto, pb); }
        }
        if (!okp) continue;
        want[w++] = '>'; want[w] = 0; if (!ps->n) strcpy(proto, "void");
        const char *sig = expSig(ex, n, "jdh_cmd_", name);
        if (!sig) lerr(L, "명령어 '%s' 이(가) C 구현에 없습니다 — JDH_COMMAND(%s, %s) { … } 가 필요합니다", name, name, proto);
        else if (strcmp(sig, want)) lerr(L, "명령어 '%s' 의 인자가 선언과 다릅니다 — 선언대로라면 JDH_COMMAND(%s, %s)", name, name, proto);
    }
    if (vs && vs->k == J_OBJ) for (int i = 0; i < vs->n; i++) {
        const char *name = vs->keys[i]; if (vs->items[i]->k != J_STR) continue;
        const char *t = vs->items[i]->str, *want = !strcmp(t, "float") ? ">d" : ">i", *ct = !strcmp(t, "float") ? "double" : !strcmp(t, "string") ? "const char *" : "int";
        const char *sig = expSig(ex, n, "jdh_val_", name);
        if (!sig) lerr(L, "값 '%s' 이(가) C 구현에 없습니다 — JDH_VALUE(%s, %s) { return …; } 가 필요합니다", name, ct, name);
        else if (strcmp(sig, want)) lerr(L, "값 '%s' 의 형식이 선언과 다릅니다 — 선언대로라면 JDH_VALUE(%s, %s)", name, ct, name);
    }
    /* 엔진 기능 (host 함수) 과 권한 */
    hostCheck(jget(L->json, "permissions"), libHostMsg, L);
    /* 선언에 없는데 구현에만 있는 것 */
    for (int i = 0; i < n; i++) {
        if (!strncmp(ex[i].name, "jdh_cmd_", 8) && !(cm && cm->k == J_OBJ && jget(cm, ex[i].name + 8))) lwarn(L, "C 구현의 명령어 '%s' 이(가) 선언(jdl.json)에 없습니다 — 코드에서 쓸 수 없습니다", ex[i].name + 8);
        if (!strncmp(ex[i].name, "jdh_val_", 8) && !(vs && vs->k == J_OBJ && jget(vs, ex[i].name + 8))) lwarn(L, "C 구현의 값 '%s' 이(가) 선언(jdl.json)에 없습니다 — 코드에서 쓸 수 없습니다", ex[i].name + 8);
    }
}
/* 선언 검사 → 컴파일 → 결과 검사 → 담을 파일 목록 */
static void libPrepare(Lib *L) {
    libCheck(L);
    if (L->errors) return;
    char *bd = fmtdup("%s/build", L->dir); mkdirs(bd);
    char *out = fmtdup("%s/lib.wasm", bd);
    if (compileWasm(L, out)) {
        size_t len; unsigned char *b = readAll(out, &len);
        if (!b) lerr(L, "컴파일 결과를 읽을 수 없습니다: %s", out);
        else { checkWasm(L, b, len); addEnt(L, "lib.wasm", b, len); }
    }
    for (int i = 0; i < L->ncsrc; i++) {
        char *fp = fmtdup("%s/%s", L->dir, L->csrc[i]); size_t len; unsigned char *b = readAll(fp, &len);
        if (b) addEnt(L, L->csrc[i], b, len);
        free(fp);
    }
    addFolder(L, "doc");
    free(out); free(bd);
}

/* ---------- .jbx 쓰기 · 읽기 ---------- */
static void put32(FILE *f, uint32_t v) { unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) }; fwrite(b, 1, 4, f); }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void jbxWrite(Lib *L, const char *out) {
    FILE *f = fopen(out, "wb"); if (!f) die("파일을 쓸 수 없습니다: %s", out);
    fwrite("JBLX", 1, 4, f); put32(f, (uint32_t)(L->nents + 1));
    put32(f, 8); fwrite("jdl.json", 1, 8, f); put32(f, (uint32_t)L->manLen); fwrite(L->man, 1, L->manLen, f);
    for (int i = 0; i < L->nents; i++) {
        size_t nl = strlen(L->ents[i].name);
        put32(f, (uint32_t)nl); fwrite(L->ents[i].name, 1, nl, f); put32(f, (uint32_t)L->ents[i].len); fwrite(L->ents[i].data, 1, L->ents[i].len, f);
    }
    if (fclose(f)) die("파일을 쓰다가 실패했습니다: %s", out);
}
/* .jbx 의 선언을 읽음 (id · version) — 없거나 깨졌으면 끝 */
static JV *jbxManifest(const char *path, unsigned char **raw, size_t *rawLen) {
    size_t len; unsigned char *b = readAll(path, &len);
    if (!b) die("파일을 열 수 없습니다: %s", path);
    if (len < 8 || memcmp(b, "JBLX", 4)) die("%s 은(는) .jbx 파일이 아니거나 손상되었습니다", path);
    uint32_t n = get32(b + 4); size_t p = 8;
    for (uint32_t k = 0; k < n; k++) {
        if (p + 4 > len) die("%s 이(가) 손상되었습니다", path);
        uint32_t nl = get32(b + p); p += 4; if (p + nl + 4 > len) die("%s 이(가) 손상되었습니다", path);
        const unsigned char *name = b + p; p += nl;
        uint32_t sz = get32(b + p); p += 4; if (p + sz > len) die("%s 이(가) 손상되었습니다", path);
        if (nl == 8 && !memcmp(name, "jdl.json", 8)) {
            char *t = xalloc(sz + 1); memcpy(t, b + p, sz); t[sz] = 0;
            const char *err; int line; JV *j = jparse(t, &err, &line);
            if (!j) die("%s 의 선언을 읽을 수 없습니다 — %s", path, err);
            if (raw) { *raw = b; *rawLen = len; } else free(b);
            return j;
        }
        p += sz;
    }
    die("%s 에 선언(jdl.json)이 없습니다", path); return NULL;
}

/* ---------- 명령 ---------- */
static const char *TPL_C =
"// %s — C 구현 (한 번만 씁니다: 웹에서는 WebAssembly 로, EXE 에서는 실행기와 함께 컴파일됩니다)\n"
"//   웹에서는 격리된 공간에서 실행됩니다: 에디터 · 저장 공간 · 프로젝트 파일에는 닿지 못합니다.\n"
"#include \"jdh.h\"\n"
"#include <stdio.h>\n"
"\n"
"static int count;\n"
"\n"
"// 명령어: 코드에서  별칭.hello(\"이름\");\n"
"JDH_COMMAND(hello, const char *name) {\n"
"    char line[256];\n"
"    count++;\n"
"    snprintf(line, sizeof line, \"hello, %%s\", name);\n"
"    jdh_log(line);\n"
"}\n"
"\n"
"// 인자가 없는 명령어는 void\n"
"JDH_COMMAND(reset, void) { count = 0; }\n"
"\n"
"// 값: 코드에서  별칭.count   (명령어는 값을 돌려주지 않습니다 — 돌려줄 것은 값으로)\n"
"JDH_VALUE(int, count) { return count; }\n"
"\n"
"// 리스트 · 행렬 인자: jdl.json 에 \"[int, ...]\" · \"[float, string]\" · \"matrix[2, 3]\" 처럼 쓰고, C 에서 받는 꼴은 jdh.h 맨 위에 있습니다.\n"
"//   예)  \"avg\": { \"params\": [\"[float, ...]\"] }   →   JDH_COMMAND(avg, const double *items, int n) { … }\n"
"//\n"
"// 선택: JDH_INIT() { }  실행을 시작할 때 · JDH_FRAME(dt) { }  프레임마다 (dt = 지난 시간 ms) · JDH_STOP() { }  끝날 때\n"
"//\n"
"// 엔진 기능 (그리기 · 오브젝트 읽기 · 쓰기 · 프로젝트 파일 읽기) 은 jdh.h 에 있습니다.\n"
"//   쓰려면 jdl.json 의 \"permissions\" 에 \"draw\" · \"objects.read\" · \"objects.write\" · \"files.read\" 중 필요한 것을 적습니다.\n";
static const char *TPL_JDL =
"{\n"
"  \"jdl\": 1,\n"
"  \"id\": \"%s\",\n"
"  \"version\": \"0.1.0\",\n"
"  \"requires\": { \"engine\": \">=1.3\", \"libs\": [] },\n"
"  \"permissions\": [],\n"
"  \"commands\": {\n"
"    \"hello\": { \"params\": [\"string\"], \"doc\": { \"ko\": \"이름을 받아 인사를 로그에 찍음\" } },\n"
"    \"reset\": { \"params\": [] }\n"
"  },\n"
"  \"values\": { \"count\": \"int\" },\n"
"  \"native\": [\"native/%s.c\"]\n"
"}\n";
static const char *TPL_DOC =
"// %s — 설명서 (읽기 전용으로 열림)\n"
"//\n"
"//   Use %s call 별칭;\n"
"//\n"
"//   별칭.hello(\"이름\");     인사를 디버그 로그에 찍음\n"
"//   별칭.reset();            횟수를 0 으로\n"
"//   별칭.count               지금까지 hello 를 부른 횟수 (int)\n";
static int cmdNewLib(const char *id) {
    if (!okName(id)) die("라이브러리 이름 '%s' 은(는) 쓸 수 없습니다 — 글자 · 숫자 · _ 만, 숫자로 시작하지 않게", id);
    if (inSet(id, BUILTIN)) die("'%s' 은(는) 내장 라이브러리 이름이라 쓸 수 없습니다", id);
    if (isDir(id) || isFile(id)) die("이미 '%s' 이(가) 있습니다", id);
    char *p;
    p = fmtdup("%s/native", id); mkdirs(p); free(p);
    p = fmtdup("%s/doc", id); mkdirs(p); free(p);
    char *t;
    p = fmtdup("%s/jdl.json", id); t = fmtdup(TPL_JDL, id, id); writeAll(p, t, strlen(t)); free(p); free(t);
    p = fmtdup("%s/native/%s.c", id, id); t = fmtdup(TPL_C, id); writeAll(p, t, strlen(t)); free(p); free(t);
    p = fmtdup("%s/doc/%s.jbl", id, id); t = fmtdup(TPL_DOC, id, id); writeAll(p, t, strlen(t)); free(p); free(t);
    printf("✔ 라이브러리 틀을 만들었습니다: %s/\n    jdl.json        선언 (명령어 · 값 · 권한)\n    native/%s.c  C 구현\n    doc/%s.jbl  설명서\n  다음: cd %s → jdh lib check → jdh lib build\n", id, id, id, id);
    return 0;
}
static int cmdNewProject(const char *name) {
    if (isDir(name) || isFile(name)) die("이미 '%s' 이(가) 있습니다", name);
    char *p = fmtdup("%s/lib", name); mkdirs(p); free(p);
    p = fmtdup("%s/ext", name); mkdirs(p); free(p);
    char *t = fmtdup("// Jawbreaker 프로젝트 파일 — 이 파일이 있는 폴더가 프로젝트 최상위입니다.\njbs 1\nname = %s\nmain = main.jb\n", baseName(name));
    p = fmtdup("%s/project.jbs", name); writeAll(p, t, strlen(t)); free(p); free(t);
    t = fmtdup("// 코드는 run{ } 안에 써야 실행됩니다.\nUse jawbreaker call jaw;\n\nrun{\n    print(\"Hello, %s\");\n}\n", baseName(name));
    p = fmtdup("%s/main.jb", name); writeAll(p, t, strlen(t)); free(p); free(t);
    printf("✔ 프로젝트를 만들었습니다: %s/\n", name);
    return 0;
}
static int cmdLibCheck(const char *dir, Lib **out) {
    Lib *L = libLoad(dir);
    printf("검사: %s\n", L->dir);
    libPrepare(L);
    if (L->errors) printf("✖ 오류 %d개%s\n", L->errors, L->warns ? fmtdup(" · 주의 %d개", L->warns) : "");
    else printf("✔ 문제 없음%s — %s %s\n", L->warns ? fmtdup(" (주의 %d개)", L->warns) : "", L->id, L->version);
    if (out) *out = L;
    return L->errors ? 1 : 0;
}
static int cmdLibBuild(const char *dir, const char *outArg) {
    Lib *L; if (cmdLibCheck(dir, &L)) { puts("  굽지 않았습니다 — 위 오류를 먼저 고치세요"); return 1; }
    char *out = outArg ? xdup(outArg) : fmtdup("%s/%s-%s.jbx", L->dir, L->id, L->version);
    jbxWrite(L, out);
    size_t total; unsigned char *b = readAll(out, &total); free(b);
    printf("✔ 구웠습니다: %s (%lu 바이트 · 파일 %d개)\n  적용: 프로젝트 폴더에서  jdh lib add %s\n", out, (unsigned long)total, L->nents + 1, out);
    return 0;
}
static const char *projDir(const char *arg) {
    const char *d = arg ? arg : ".";
    char *p = fmtdup("%s/project.jbs", d);
    if (!isFile(p)) die("%s 에 project.jbs 가 없습니다 — 프로젝트 폴더에서 실행하거나 폴더를 적어 주세요", d);
    free(p); return d;
}
static int cmdLibAdd(const char *jbx, const char *projArg) {
    const char *pd = projDir(projArg);
    unsigned char *raw; size_t len;
    JV *j = jbxManifest(jbx, &raw, &len);
    const char *id = jgets(j, "id"), *ver = jgets(j, "version");
    if (!id || !okName(id)) die("%s 의 선언에 올바른 id 가 없습니다", jbx);
    char *ext = fmtdup("%s/ext", pd); mkdirs(ext);
    char *dst = fmtdup("%s/%s.jbx", ext, id);
    int had = isFile(dst);
    writeAll(dst, raw, len);
    printf("✔ %s %s — %s: %s\n  코드에서:  Use %s call 별칭;\n", id, ver ? ver : "", had ? "새 파일로 바꿨습니다" : "프로젝트에 적용했습니다", dst, id);
    return 0;
}
static int cmdLibList(const char *projArg) {
    const char *pd = projDir(projArg);
    char *ext = fmtdup("%s/ext", pd); DIR *d = opendir(ext); int n = 0;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            if (!endsWith(e->d_name, ".jbx")) continue;
            char *p = fmtdup("%s/%s", ext, e->d_name); JV *j = jbxManifest(p, NULL, NULL);
            const char *id = jgets(j, "id"), *ver = jgets(j, "version");
            JV *cm = jget(j, "commands"), *vs = jget(j, "values"), *pm = jget(j, "permissions");
            printf("  %s %s — 명령어 %d개 · 값 %d개 · 권한: ", id ? id : "?", ver ? ver : "", cm && cm->k == J_OBJ ? cm->n : 0, vs && vs->k == J_OBJ ? vs->n : 0);
            if (!pm || pm->k != J_ARR || !pm->n) fputs("없음", stdout);
            else for (int i = 0; i < pm->n; i++) printf("%s%s", i ? ", " : "", pm->items[i]->k == J_STR ? pm->items[i]->str : "?");
            putchar('\n'); n++; free(p);
        }
        closedir(d);
    }
    if (!n) puts("  적용된 라이브러리가 없습니다 (ext/ 가 비어 있음)");
    return 0;
}
static int cmdLibRemove(const char *id, const char *projArg) {
    const char *pd = projDir(projArg);
    if (!okName(id)) die("라이브러리 이름이 올바르지 않습니다: %s", id);
    char *p = fmtdup("%s/ext/%s.jbx", pd, id);
    if (!isFile(p)) die("이 프로젝트에 '%s' 라이브러리가 없습니다 (%s)", id, p);
    if (remove(p)) die("지울 수 없습니다: %s", p);
    printf("✔ %s 을(를) 프로젝트에서 뺐습니다\n", id);
    return 0;
}

/* ---------- jdh build exe — 프로젝트를 Windows 실행 파일로 (사용자 라이브러리의 C 구현을 실행기와 함께 컴파일) ---------- */
typedef struct { char *name; const unsigned char *data; size_t len; } JEnt;
static JEnt *jbxEntries(const char *path, int *count) {
    size_t len; unsigned char *b = readAll(path, &len); *count = 0;
    if (!b) die("파일을 열 수 없습니다: %s", path);
    if (len < 8 || memcmp(b, "JBLX", 4)) die("%s 은(는) .jbx 파일이 아니거나 예전 형식입니다 — jdh lib build 로 다시 구워 주세요", path);
    uint32_t n = get32(b + 4); size_t p = 8; JEnt *e = xalloc(sizeof(JEnt) * (n + 1));
    for (uint32_t k = 0; k < n; k++) {
        if (p + 4 > len) die("%s 이(가) 손상되었습니다", path);
        uint32_t nl = get32(b + p); p += 4; if (p + nl + 4 > len) die("%s 이(가) 손상되었습니다", path);
        e[k].name = xalloc(nl + 1); memcpy(e[k].name, b + p, nl); e[k].name[nl] = 0; p += nl;
        uint32_t sz = get32(b + p); p += 4; if (p + sz > len) die("%s 이(가) 손상되었습니다", path);
        e[k].data = b + p; e[k].len = sz; p += sz;
    }
    *count = (int)n; return e;
}
static JEnt *jent(JEnt *e, int n, const char *name) { for (int i = 0; i < n; i++) if (!strcmp(e[i].name, name)) return &e[i]; return NULL; }
static int run(const char *cmd) {
#ifdef _WIN32
    char *full = fmtdup("\"%s\"", cmd);
#else
    char *full = xdup(cmd);
#endif
    fflush(stdout); int r = system(full); free(full); return r;
}
static const char *ctyOf(const char *t) { return !strcmp(t, "float") ? "double" : !strcmp(t, "string") || !strcmp(t, "color") ? "const char *" : "int"; }
static int tcode(const char *t) { return !strcmp(t, "int") ? 1 : !strcmp(t, "float") ? 2 : !strcmp(t, "string") ? 3 : !strcmp(t, "bit") ? 4 : !strcmp(t, "color") ? 5 : !strcmp(t, "mat12") ? 6 : 0; }
static const char *slotOf(int k) { return k == PK_FLOAT ? "d" : k == PK_STRING || k == PK_COLOR ? "s" : "i"; }
static char *cstrLit(const char *s) {   /* C 문자열 상수 (따옴표 포함) */
    char *o = xalloc(strlen(s) * 4 + 3), *w = o; *w++ = '"';
    for (; *s; s++) { unsigned char c = (unsigned char)*s; if (c == '"' || c == '\\') { *w++ = '\\'; *w++ = (char)c; } else if (c < 32) { w += sprintf(w, "\\%03o", c); } else *w++ = (char)c; }
    *w++ = '"'; *w = 0; return o;
}
/* 프로젝트 파일을 실행 파일 끝에 붙임: "JBPK" u32 개수 · (u32 경로 길이 · 경로 · u32 크기 · 내용)… · u64 시작 위치 · "JBPAYLD1" */
typedef struct { char **path; int n; } PList;
static void projScan(const char *root, const char *rel, PList *pl) {
    char *dir = *rel ? fmtdup("%s/%s", root, rel) : xdup(root); DIR *d = opendir(dir); if (!d) { free(dir); return; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char *r2 = *rel ? fmtdup("%s/%s", rel, e->d_name) : xdup(e->d_name), *f2 = fmtdup("%s/%s", root, r2);
        if (isDir(f2)) {
            if (!*rel && (!strcmp(e->d_name, "build") || !strcmp(e->d_name, "ext") || endsWith(e->d_name, "_data"))) { free(r2); free(f2); continue; }
            projScan(root, r2, pl); free(r2);
        } else if (isFile(f2) && !endsWith(e->d_name, ".exe") && !endsWith(e->d_name, ".zip")) {
            pl->path = realloc(pl->path, sizeof(char*) * (pl->n + 1)); pl->path[pl->n++] = r2;
        } else free(r2);
        free(f2);
    }
    closedir(d); free(dir);
}
static void put64(FILE *f, uint64_t v) { for (int i = 0; i < 8; i++) fputc((int)((v >> (8 * i)) & 255), f); }
static void appendPayload(const char *exe, const char *root) {
    PList pl = { NULL, 0 }; projScan(root, "", &pl);
    qsort(pl.path, pl.n, sizeof(char*), cmpStr);
    FILE *f = fopen(exe, "ab"); if (!f) die("실행 파일에 프로젝트를 붙일 수 없습니다: %s", exe);
    fseek(f, 0, SEEK_END); long start = ftell(f);
    fwrite("JBPK", 1, 4, f); put32(f, (uint32_t)pl.n);
    for (int i = 0; i < pl.n; i++) {
        char *fp = fmtdup("%s/%s", root, pl.path[i]); size_t len; unsigned char *b = readAll(fp, &len);
        if (!b) die("파일을 읽을 수 없습니다: %s", fp);
        size_t nl = strlen(pl.path[i]);
        put32(f, (uint32_t)nl); fwrite(pl.path[i], 1, nl, f); put32(f, (uint32_t)len); fwrite(b, 1, len, f);
        free(b); free(fp);
    }
    put64(f, (uint64_t)start); fwrite("JBPAYLD1", 1, 8, f);
    if (fclose(f)) die("실행 파일을 쓰다가 실패했습니다: %s", exe);
    printf("  프로젝트 파일 %d개를 넣었습니다\n", pl.n);
}
static char *projName(const char *pd) {
    char *p = fmtdup("%s/project.jbs", pd); unsigned char *t = readAll(p, NULL); free(p);
    char *name = NULL;
    for (char *ln = strtok((char*)t, "\n"); ln; ln = strtok(NULL, "\n")) {
        while (*ln == ' ' || *ln == '\t') ln++;
        if (!strncmp(ln, "name", 4)) { char *eq = strchr(ln, '='); if (eq) { eq++; while (*eq == ' ' || *eq == '\t') eq++; size_t L = strlen(eq); while (L && (eq[L-1] == '\r' || eq[L-1] == ' ' || eq[L-1] == '\t')) eq[--L] = 0; if (L) name = xdup(eq); } }
    }
    if (!name) name = xdup("game");
    for (char *q = name; *q; q++) if (strchr("\\/:*?\"<>|", *q)) *q = '_';
    return name;
}
static void exeHostMsg(void *ud, const char *fn, const char *perm) {
    if (!perm) fprintf(stderr, "  ✖ %s: jdh_%s 은(는) 엔진이 주는 함수가 아닙니다\n", (const char *)ud, fn);
    else fprintf(stderr, "  ✖ %s: jdh_%s 은(는) \"%s\" 권한이 필요합니다\n", (const char *)ud, fn, perm);
}
static int cmdBuildExe(const char *projArg, const char *outArg) {
    const char *pd = projDir(projArg);
    char *home = homeDir(), *sdk = sdkDir();
    if (!sdk) die("jdh.h 를 찾을 수 없습니다 — JDH 를 다시 설치하거나 환경 변수 JDH_HOME 을 확인하세요");
    const char *rte = getenv("JDH_RUNTIME"), *sdle = getenv("JDH_SDL");
    char *rt = rte && *rte ? xdup(rte) : fmtdup("%s/runtime", sdk), *sdl = sdle && *sdle ? xdup(sdle) : fmtdup("%s/SDL2", sdk);
    char *rtsrc = fmtdup("%s/src", rt);
    if (!isDir(rtsrc)) die("실행기 소스가 없습니다: %s — JDH 를 다시 설치해 주세요", rtsrc);
    (void)home;
    char *bd = fmtdup("%s/build/jdh", pd); mkdirs(bd);
    /* 1. 라이브러리 풀기 · 잇는 표 만들기 */
    char *ext = fmtdup("%s/ext", pd); DIR *d = opendir(ext);
    char **jbx = NULL; int nj = 0;
    if (d) { struct dirent *e; while ((e = readdir(d))) if (endsWith(e->d_name, ".jbx")) { jbx = realloc(jbx, sizeof(char*) * (nj + 1)); jbx[nj++] = fmtdup("%s/%s", ext, e->d_name); } closedir(d); }
    if (nj) qsort(jbx, nj, sizeof(char*), cmpStr);
    char *gen = xdup("/* jdh build exe 가 만든 파일 — 사용자 라이브러리를 실행기에 잇는 표 */\n#include \"jdh_native.h\"\n");
    char *tbl = xdup("");
    char **objsrc = NULL; int *objlib = NULL; int nobj = 0;
    for (int li = 0; li < nj; li++) {
        int ne; JEnt *en = jbxEntries(jbx[li], &ne);
        JEnt *me = jent(en, ne, "jdl.json"), *we = jent(en, ne, "lib.wasm");
        if (!me) die("%s 에 선언(jdl.json)이 없습니다", jbx[li]);
        char *mt = xalloc(me->len + 1); memcpy(mt, me->data, me->len); mt[me->len] = 0;
        const char *err; int line; JV *j = jparse(mt, &err, &line); if (!j) die("%s 의 선언을 읽을 수 없습니다 — %s", jbx[li], err);
        const char *id = jgets(j, "id"); if (!id) die("%s 의 선언에 id 가 없습니다", jbx[li]);
        if (!we) die("%s 에 컴파일된 구현(lib.wasm)이 없습니다 — jdh lib build 로 다시 구워 주세요", jbx[li]);
        int hasInit = 0, hasFrame = 0, hasStop = 0;
        if (we) { int nx; WExp *ex = wasmExports(we->data, we->len, &nx);
            if (hostCheck(jget(j, "permissions"), exeHostMsg, jbx[li])) die("라이브러리 '%s' 가 선언하지 않은 권한의 기능을 씁니다 — jdh lib build 로 다시 구워 주세요", id);
            for (int q = 0; q < nx; q++) { if (!strcmp(ex[q].name, "jdh_init")) hasInit = 1; if (!strcmp(ex[q].name, "jdh_frame")) hasFrame = 1; if (!strcmp(ex[q].name, "jdh_stop")) hasStop = 1; } }
        int nsrc = 0;
        for (int q = 0; q < ne; q++) {
            if (strncmp(en[q].name, "native/", 7) || !endsWith(en[q].name, ".c") || strchr(en[q].name + 7, '/') || strstr(en[q].name, "..")) continue;
            char *ld = fmtdup("%s/x%d", bd, li); mkdirs(ld);
            char *fp = fmtdup("%s/%s", ld, en[q].name + 7); writeAll(fp, en[q].data, en[q].len);
            objsrc = realloc(objsrc, sizeof(char*) * (nobj + 1)); objlib = realloc(objlib, sizeof(int) * (nobj + 1)); objsrc[nobj] = fp; objlib[nobj++] = li; nsrc++; free(ld);
        }
        if (!nsrc) die("라이브러리 '%s' 에 C 소스가 없어 EXE 에 넣을 수 없습니다 (%s)", id, jbx[li]);
        JV *cm = jget(j, "commands"), *vs = jget(j, "values");
        int nc = cm && cm->k == J_OBJ ? cm->n : 0, nv = vs && vs->k == J_OBJ ? vs->n : 0;
        char *ct = xdup(""), *vt = xdup("");
        for (int q = 0; q < nc; q++) {
            JV *ps = jget(cm->items[q], "params"); int np = ps && ps->k == J_ARR ? ps->n : 0;
            if (np > 8) die("라이브러리 '%s' 의 명령어 '%s': 인자는 8개까지입니다", id, cm->keys[q]);
            char *proto = xdup(""), *call = xdup(""), *codes = xdup(""); int k = 0;
            for (int a = 0; a < np; a++) {
                const char *t = ps->items[a]->k == J_STR ? ps->items[a]->str : "int"; PTy pt;
                if (!ptParse(t, &pt)) die("라이브러리 '%s' 의 명령어 '%s': 알 수 없는 인자 형식 '%s'", id, cm->keys[q], t);
                if (k + ptSlots(&pt) > 64) die("라이브러리 '%s' 의 명령어 '%s': 인자가 C 에서 64칸을 넘습니다", id, cm->keys[q]);
                /* 표: {형식, 항목 수, 몇 개든, {항목 형식}, 행, 열} — 실행기의 JdhPT */
                char els[64] = "0"; if (pt.k == PK_LIST) { els[0] = 0; for (int e = 0; e < pt.n; e++) snprintf(els + strlen(els), sizeof els - strlen(els), "%s%d", e ? ", " : "", pt.el[e]); }
                codes = catf(codes, "%s{%d, %d, %d, {%s}, %d, %d}", a ? ", " : "", pt.k, pt.n, pt.var, els, pt.r, pt.c);
                const char *sep = k ? ", " : "";
                if (pt.k == PK_MAT12) { proto = catf(proto, "%sdouble, double", sep); call = catf(call, "%sa[%d].d, a[%d].d", sep, k, k + 1); k += 2; }
                else if (pt.k == PK_LIST && pt.var) {
                    const char *pty = pt.el[0] == PK_FLOAT ? "const double *" : pt.el[0] == PK_STRING ? "const char *const *" : "const int *";
                    proto = catf(proto, "%s%s, int", sep, pty); call = catf(call, "%s(%s)a[%d].p, a[%d].i", sep, pty, k, k + 1); k += 2;
                } else if (pt.k == PK_LIST) for (int e = 0; e < pt.n; e++) {
                    proto = catf(proto, "%s%s", k ? ", " : "", ptCty(pt.el[e])); call = catf(call, "%sa[%d].%s", k ? ", " : "", k, slotOf(pt.el[e])); k++;
                } else if (pt.k == PK_MAT) {
                    proto = catf(proto, "%sconst double *", sep); call = catf(call, "%s(const double *)a[%d].p", sep, k); k++;
                    if (!pt.r) { proto = catf(proto, ", int"); call = catf(call, ", a[%d].i", k); k++; }
                    if (!pt.c) { proto = catf(proto, ", int"); call = catf(call, ", a[%d].i", k); k++; }
                } else { proto = catf(proto, "%s%s", sep, ptCty(pt.k)); call = catf(call, "%sa[%d].%s", sep, k, slotOf(pt.k)); k++; }
            }
            if (!np) proto = catf(proto, "void");
            gen = catf(gen, "void jdh_x%d_cmd_%s(%s);\nstatic void c_%d_%d(const JdhArg *a) { (void)a; jdh_x%d_cmd_%s(%s); }\n", li, cm->keys[q], proto, li, q, li, cm->keys[q], call);
            char *nm = cstrLit(cm->keys[q]);
            ct = catf(ct, "    {%s, %d, {%s}, c_%d_%d},\n", nm, np, np ? codes : "{0}", li, q);
            free(nm); free(proto); free(call); free(codes);
        }
        for (int q = 0; q < nv; q++) {
            const char *t = vs->items[q]->k == J_STR ? vs->items[q]->str : "int"; int tc = tcode(t);
            gen = catf(gen, "%s jdh_x%d_val_%s(void);\nstatic void v_%d_%d(JdhArg *o) { o->%s = jdh_x%d_val_%s(); }\n", ctyOf(t), li, vs->keys[q], li, q, tc == 2 ? "d" : tc == 3 ? "s" : "i", li, vs->keys[q]);
            char *nm = cstrLit(vs->keys[q]);
            vt = catf(vt, "    {%s, %d, v_%d_%d},\n", nm, tc, li, q); free(nm);
        }
        if (hasInit) gen = catf(gen, "void jdh_x%d_init(void);\n", li);
        if (hasFrame) gen = catf(gen, "void jdh_x%d_frame(double);\n", li);
        if (hasStop) gen = catf(gen, "void jdh_x%d_stop(void);\n", li);
        gen = catf(gen, "static const JdhCmd cmds_%d[] = {\n%s    {0}\n};\nstatic const JdhVal vals_%d[] = {\n%s    {0}\n};\n", li, ct, li, vt);
        char *idl = cstrLit(id), *fi = hasInit ? fmtdup("jdh_x%d_init", li) : xdup("0"), *ff = hasFrame ? fmtdup("jdh_x%d_frame", li) : xdup("0"), *fs = hasStop ? fmtdup("jdh_x%d_stop", li) : xdup("0");
        tbl = catf(tbl, "    {%s, cmds_%d, %d, vals_%d, %d, %s, %s, %s},\n", idl, li, nc, li, nv, fi, ff, fs);
        printf("  라이브러리 %s %s — C 파일 %d개\n", id, jgets(j, "version") ? jgets(j, "version") : "", nsrc);
    }
    gen = catf(gen, "const JdhLib JDH_LIBS[] = {\n%s    {0}\n};\nconst int JDH_NLIBS = %d;\n", tbl, nj);
    char *genp = fmtdup("%s/jdh_ext_gen.c", bd); writeAll(genp, gen, strlen(gen));
    if (getenv("JDH_GEN_ONLY")) { printf("✔ 표를 만들었습니다: %s\n", genp); return 0; }
    /* 2. 컴파일: 라이브러리 C (라이브러리마다 이름을 구분) → 실행기 + 표 + 라이브러리 */
    char *zig = zigCmd(); const char *tg = "-target x86_64-windows-gnu";
    char *objs = xdup("");
    for (int q = 0; q < nobj; q++) {
        char *o = fmtdup("%s/x%d_%d.o", bd, objlib[q], q);
        char *cmd = fmtdup("%s cc %s -O2 -c -Wall -Wno-unused-function -DJDH_LIB=x%d -I\"%s\" -o \"%s\" \"%s\"", zig, tg, objlib[q], sdk, o, objsrc[q]);
        if (run(cmd)) die("라이브러리의 C 구현을 컴파일하지 못했습니다: %s", objsrc[q]);
        objs = catf(objs, " \"%s\"", o); free(cmd);
    }
    char *name = projName(pd);
    char *out = outArg ? xdup(outArg) : fmtdup("%s/%s.exe", pd, name);
    char *srcs = xdup(""); DIR *sd = opendir(rtsrc); struct dirent *se; int nrt = 0;
    while (sd && (se = readdir(sd))) if (endsWith(se->d_name, ".c") && strcmp(se->d_name, "ext_none.c")) { srcs = catf(srcs, " \"%s/%s\"", rtsrc, se->d_name); nrt++; }
    if (sd) closedir(sd);
    if (!nrt) die("실행기 소스가 비어 있습니다: %s", rtsrc);
    char *sdla = fmtdup("%s/lib/libSDL2.a", sdl);
    if (!isFile(sdla)) die("SDL2 가 없습니다: %s — JDH 를 다시 설치해 주세요", sdla);
    remove(out);
    printf("  실행기를 컴파일합니다 (처음에는 1분쯤 걸릴 수 있습니다)…\n");
    char *cmd = fmtdup("%s cc %s -O2 -mwindows -w -I\"%s/vendor\" -I\"%s/include/SDL2\" -I\"%s\" -I\"%s\" -D_REENTRANT -DSDL_MAIN_HANDLED -o \"%s\"%s \"%s\"%s \"%s\" -lm -lpthread -lcomdlg32 -lsetupapi -lcfgmgr32 -lwinmm -limm32 -lversion -lole32 -loleaut32 -lgdi32 -luuid -lshell32 -luser32 -ladvapi32",
        zig, tg, rt, sdl, rtsrc, sdk, out, srcs, genp, objs, sdla);
    if (run(cmd) || !isFile(out)) die("실행 파일을 만들지 못했습니다 (위 메시지를 확인하세요)");
    /* 3. 프로젝트 붙이기 */
    appendPayload(out, pd);
    size_t total; unsigned char *b = readAll(out, &total); free(b);
    printf("✔ 만들었습니다: %s (%.1f MB)\n", out, total / 1048576.0);
    return 0;
}
static int cmdOpen(void) {
#ifdef _WIN32
    char exe[MAX_PATH]; DWORD n = GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) die("jdh 의 위치를 알 수 없습니다");
    char *sl = strrchr(exe, '\\'); if (sl) *sl = 0;
    char *dev = fmtdup("%s\\jbdev.exe", exe);
    if (!isFile(dev)) die("jbdev.exe 가 없습니다: %s", dev);
    if ((INT_PTR)ShellExecuteA(NULL, "open", dev, NULL, NULL, SW_SHOWNORMAL) <= 32) die("JawBreaker Dev. 를 열 수 없습니다");
    return 0;
#else
    die("jdh open 은 Windows 에서만 됩니다"); return 1;
#endif
}
static void usage(void) {
    puts("jdh " JDH_VERSION " — JawBreaker Development Helper\n"
         "\n"
         "  jdh version                      버전\n"
         "  jdh new project <이름>           빈 프로젝트 폴더 만들기\n"
         "  jdh new lib <id>                 라이브러리 틀 만들기\n"
         "  jdh lib check [폴더]             선언과 C 구현이 맞는지 검사 (컴파일해 봄)\n"
         "  jdh lib build [폴더] [-o 파일]   라이브러리 폴더를 .jbx 하나로 굽기\n"
         "  jdh lib add <.jbx> [프로젝트]    프로젝트의 ext/ 에 적용\n"
         "  jdh lib list [프로젝트]          적용된 라이브러리 보기\n"
         "  jdh lib remove <id> [프로젝트]   프로젝트에서 빼기\n"
         "  jdh build exe [프로젝트] [-o 파일]  Windows 실행 파일 만들기 (라이브러리 포함)\n"
         "  jdh open                         JawBreaker Dev. 열기");
}
int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    { char me[MAX_PATH]; DWORD mn = GetModuleFileNameA(NULL, me, MAX_PATH); if (mn && mn < MAX_PATH) { char *sl = strrchr(me, '\\'); if (sl) { *sl = 0; EXEDIR = xdup(me); } } }
#else
    { char me[4096]; ssize_t mn = readlink("/proc/self/exe", me, sizeof me - 1); if (mn > 0) { me[mn] = 0; char *sl = strrchr(me, '/'); if (sl) { *sl = 0; EXEDIR = xdup(me); } } }
#endif
    if (argc < 2 || !strcmp(argv[1], "-h") || !strcmp(argv[1], "--help") || !strcmp(argv[1], "help")) { usage(); return argc < 2 ? 2 : 0; }
    const char *a = argv[1], *b = argc > 2 ? argv[2] : NULL;
    if (!strcmp(a, "version")) { puts("jdh " JDH_VERSION); return 0; }
    if (!strcmp(a, "open")) return cmdOpen();
    if (!strcmp(a, "new") && b) {
        if (argc < 4) die("이름이 필요합니다: jdh new %s <이름>", b);
        if (!strcmp(b, "lib")) return cmdNewLib(argv[3]);
        if (!strcmp(b, "project")) return cmdNewProject(argv[3]);
    }
    if (!strcmp(a, "build") && b && !strcmp(b, "exe")) {
        const char *pj = NULL, *out = NULL;
        for (int i = 3; i < argc; i++) { if (!strcmp(argv[i], "-o")) { if (++i >= argc) die("-o 뒤에 파일 이름이 필요합니다"); out = argv[i]; } else pj = argv[i]; }
        return cmdBuildExe(pj, out);
    }
    if (!strcmp(a, "lib") && b) {
        if (!strcmp(b, "check")) return cmdLibCheck(argc > 3 ? argv[3] : ".", NULL);
        if (!strcmp(b, "build")) {
            const char *dir = ".", *out = NULL;
            for (int i = 3; i < argc; i++) { if (!strcmp(argv[i], "-o")) { if (++i >= argc) die("-o 뒤에 파일 이름이 필요합니다"); out = argv[i]; } else dir = argv[i]; }
            return cmdLibBuild(dir, out);
        }
        if (!strcmp(b, "add")) { if (argc < 4) die(".jbx 파일이 필요합니다: jdh lib add <.jbx> [프로젝트 폴더]"); return cmdLibAdd(argv[3], argc > 4 ? argv[4] : NULL); }
        if (!strcmp(b, "list")) return cmdLibList(argc > 3 ? argv[3] : NULL);
        if (!strcmp(b, "remove")) { if (argc < 4) die("라이브러리 이름이 필요합니다: jdh lib remove <id> [프로젝트 폴더]"); return cmdLibRemove(argv[3], argc > 4 ? argv[4] : NULL); }
    }
    usage(); return 2;
}
