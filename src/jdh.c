/* jdh — JawBreaker Development Helper 명령
 *   라이브러리 틀 만들기 · 검사 · 굽기(.jbx) · 프로젝트에 적용
 *   .jbx: "JBX1" u32 개수 · (u32 경로 길이 · 경로 · u32 크기 · 내용)…   (모두 little-endian)
 *         jdl.json (선언) · web.js (JS 구현) · doc/… (설명서) · native/… (C 소스)
 */
#define JDH_VERSION "0.1.0"
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
    char *dir, *id, *version, *webPath;
    unsigned char *man; size_t manLen;
    JV *json;
    Ent *ents; int nents;
    int errors, warns;
} Lib;
static void lerr(Lib *L, const char *fmt, ...) { va_list ap; va_start(ap, fmt); fputs("  ✖ ", stdout); vprintf(fmt, ap); putchar('\n'); va_end(ap); L->errors++; }
static void lwarn(Lib *L, const char *fmt, ...) { va_list ap; va_start(ap, fmt); fputs("  ⚠ ", stdout); vprintf(fmt, ap); putchar('\n'); va_end(ap); L->warns++; }
static int inSet(const char *s, const char **set) { for (; *set; set++) if (!strcmp(s, *set)) return 1; return 0; }
static const char *PT[] = { "int", "float", "string", "bit", "color", "mat12", NULL };
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
/* JS 에서 명령어가 정의되어 있는지 (이름 뒤에 '(' 또는 ':' 가 오는 곳이 있는지) — 대강의 검사 */
static int jsHas(const char *js, const char *name) {
    size_t n = strlen(name);
    for (const char *p = js; (p = strstr(p, name)); p += n) {
        unsigned char b = p == js ? ' ' : (unsigned char)p[-1];
        if (isalnum(b) || b == '_' || b == '$' || b >= 0x80) continue;
        const char *q = p + n; while (*q == ' ' || *q == '\t') q++;
        if (*q == '(' || *q == ':') return 1;
    }
    return 0;
}
static Lib *libLoad(const char *dir) {
    Lib *L = xalloc(sizeof *L); memset(L, 0, sizeof *L);
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
    /* 필요한 내장 라이브러리 */
    JV *rq = jget(jget(j, "requires"), "libs");
    if (rq) {
        if (rq->k != J_ARR) lerr(L, "\"requires\".\"libs\" 는 [ ] 목록이어야 합니다");
        else for (int i = 0; i < rq->n; i++) if (rq->items[i]->k != J_STR || !inSet(rq->items[i]->str, BUILTIN)) lerr(L, "requires.libs 에는 내장 라이브러리 이름만 씁니다: %s", rq->items[i]->k == J_STR ? rq->items[i]->str : "(문자열 아님)");
    }
    /* JS 구현 */
    const char *web = jgets(j, "web");
    char *js = NULL;
    if (!web) lerr(L, "\"web\" (JS 구현 파일 경로) 가 필요합니다 (예: \"web/%s.js\")", L->id ? L->id : "lib");
    else if (!okRelPath(web)) lerr(L, "web 경로는 라이브러리 폴더 안을 가리켜야 합니다: %s", web);
    else {
        char *wp = fmtdup("%s/%s", L->dir, web); size_t len;
        js = (char*)readAll(wp, &len);
        if (!js) lerr(L, "JS 구현 파일이 없습니다: %s", web);
        else {
            if (strlen(js) != len) lerr(L, "JS 구현 파일에 0 바이트가 들어 있습니다 (글자 파일이 아님): %s", web);
            if (!strstr(js, "export default")) lerr(L, "%s: 'export default function (host) { … }' 로 시작하는 함수가 있어야 합니다", web);
            L->webPath = wp; addEnt(L, "web.js", (unsigned char*)js, len);
        }
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
        else for (int k = 0; k < ps->n; k++)
            if (ps->items[k]->k != J_STR || !inSet(ps->items[k]->str, PT)) lerr(L, "명령어 '%s' 의 %d번째 인자 형식 '%s' 은(는) 쓸 수 없습니다 (int · float · string · bit · color · mat12)", name, k + 1, ps->items[k]->k == J_STR ? ps->items[k]->str : "?");
        if (jget(c, "returns")) lerr(L, "명령어 '%s': \"returns\" 는 지원하지 않습니다 — 돌려줄 값은 \"values\" 에 선언하고 코드에서 별칭.값이름 으로 읽습니다", name);
        if (js && !jsHas(js, name)) lerr(L, "명령어 '%s' 이(가) JS 구현에 보이지 않습니다 (%s 에서 %s(…) { … } 를 찾지 못함)", name, web, name);
    }
    /* 값 */
    JV *vs = jget(j, "values"); int nval = 0;
    if (vs && vs->k != J_OBJ) lerr(L, "\"values\" 는 { \"이름\": \"형식\" } 꼴이어야 합니다");
    else if (vs) for (int i = 0; i < vs->n; i++) {
        const char *name = vs->keys[i]; nval++;
        if (!okName(name)) { lerr(L, "값 이름 '%s' 은(는) 쓸 수 없습니다", name); continue; }
        if (vs->items[i]->k != J_STR || !inSet(vs->items[i]->str, VT)) lerr(L, "값 '%s' 의 형식은 int · float · string · bit 중 하나여야 합니다", name);
        if (cm && cm->k == J_OBJ && jget(cm, name)) lerr(L, "'%s' 이(가) 명령어와 값 양쪽에 선언되었습니다", name);
        if (js && !jsHas(js, name)) lerr(L, "값 '%s' 이(가) JS 구현에 보이지 않습니다 (values: { %s: () => … } 를 찾지 못함)", name, name);
    }
    if (nval && js && !jsHas(js, "values")) lerr(L, "값을 선언했지만 JS 구현에 values: { … } 가 없습니다");
    if (!ncmd && !nval) lwarn(L, "명령어도 값도 선언하지 않았습니다");
    /* C 구현 (소스만 담음 — 컴파일은 EXE 로 내보낼 때) */
    JV *nt = jget(j, "native");
    if (nt) {
        if (nt->k != J_ARR) lerr(L, "\"native\" 는 C 파일 경로의 [ ] 목록이어야 합니다");
        else for (int i = 0; i < nt->n; i++) {
            if (nt->items[i]->k != J_STR || !okRelPath(nt->items[i]->str)) { lerr(L, "native 경로가 올바르지 않습니다"); continue; }
            const char *np = nt->items[i]->str;
            if (strncmp(np, "native/", 7) || strchr(np + 7, '/')) { lerr(L, "C 파일은 native/ 폴더 바로 아래에 둡니다: %s", np); continue; }
            char *fp = fmtdup("%s/%s", L->dir, np); if (!isFile(fp)) lerr(L, "C 구현 파일이 없습니다: %s", np); free(fp);
        }
        if (nt->k == J_ARR && nt->n) lwarn(L, "C 구현은 소스만 담습니다 — EXE 내보내기에서 쓰는 것은 JDH 0.2 부터입니다");
    }
    addFolder(L, "doc");
    if (nt && nt->k == J_ARR && nt->n) addFolder(L, "native");
}

/* ---------- .jbx 쓰기 · 읽기 ---------- */
static void put32(FILE *f, uint32_t v) { unsigned char b[4] = { (unsigned char)v, (unsigned char)(v >> 8), (unsigned char)(v >> 16), (unsigned char)(v >> 24) }; fwrite(b, 1, 4, f); }
static uint32_t get32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static void jbxWrite(Lib *L, const char *out) {
    FILE *f = fopen(out, "wb"); if (!f) die("파일을 쓸 수 없습니다: %s", out);
    fwrite("JBX1", 1, 4, f); put32(f, (uint32_t)(L->nents + 1));
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
    if (len < 8 || memcmp(b, "JBX1", 4)) die("%s 은(는) .jbx 파일이 아니거나 손상되었습니다", path);
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
static const char *TPL_JS =
"// %s — JS 구현 (JawBreaker Dev. 실행 · HTML · APK)\n"
"//   이 코드는 격리된 공간에서 실행됩니다: 에디터 · 저장 공간 · 프로젝트 파일에는 닿지 못하고,\n"
"//   엔진과는 host 와 아래에서 돌려주는 명령어 · 값으로만 주고받습니다.\n"
"export default function (host) {\n"
"  let count = 0;\n"
"\n"
"  // host.onFrame(dt => { });      프레임마다 (dt = 지난 시간 ms)\n"
"  // host.onStop(() => { });       실행이 끝날 때\n"
"  // host.log(\"글자\");             디버그 로그에 찍기\n"
"\n"
"  return {\n"
"    // 명령어: 코드에서  별칭.hello(\"이름\");\n"
"    hello(name) { count++; host.log(\"hello, \" + name); },\n"
"    reset() { count = 0; },\n"
"\n"
"    // 값: 코드에서  별칭.count   (명령어는 값을 돌려주지 않습니다 — 돌려줄 것은 여기에)\n"
"    values: { count: () => count }\n"
"  };\n"
"}\n";
static const char *TPL_JDL =
"{\n"
"  \"jdl\": 1,\n"
"  \"id\": \"%s\",\n"
"  \"version\": \"0.1.0\",\n"
"  \"requires\": { \"engine\": \">=1.2\", \"libs\": [] },\n"
"  \"permissions\": [],\n"
"  \"commands\": {\n"
"    \"hello\": { \"params\": [\"string\"], \"doc\": { \"ko\": \"이름을 받아 인사를 로그에 찍음\" } },\n"
"    \"reset\": { \"params\": [] }\n"
"  },\n"
"  \"values\": { \"count\": \"int\" },\n"
"  \"web\": \"web/%s.js\"\n"
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
    p = fmtdup("%s/web", id); mkdirs(p); free(p);
    p = fmtdup("%s/doc", id); mkdirs(p); free(p);
    char *t;
    p = fmtdup("%s/jdl.json", id); t = fmtdup(TPL_JDL, id, id); writeAll(p, t, strlen(t)); free(p); free(t);
    p = fmtdup("%s/web/%s.js", id, id); t = fmtdup(TPL_JS, id); writeAll(p, t, strlen(t)); free(p); free(t);
    p = fmtdup("%s/doc/%s.jbl", id, id); t = fmtdup(TPL_DOC, id, id); writeAll(p, t, strlen(t)); free(p); free(t);
    printf("✔ 라이브러리 틀을 만들었습니다: %s/\n    jdl.json        선언 (명령어 · 값 · 권한)\n    web/%s.js   JS 구현\n    doc/%s.jbl  설명서\n  다음: cd %s → jdh lib check → jdh lib build\n", id, id, id, id);
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
    libCheck(L);
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
         "  jdh lib check [폴더]             선언과 JS 구현이 맞는지 검사\n"
         "  jdh lib build [폴더] [-o 파일]   라이브러리 폴더를 .jbx 하나로 굽기\n"
         "  jdh lib add <.jbx> [프로젝트]    프로젝트의 ext/ 에 적용\n"
         "  jdh lib list [프로젝트]          적용된 라이브러리 보기\n"
         "  jdh lib remove <id> [프로젝트]   프로젝트에서 빼기\n"
         "  jdh open                         JawBreaker Dev. 열기");
}
int main(int argc, char **argv) {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
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
