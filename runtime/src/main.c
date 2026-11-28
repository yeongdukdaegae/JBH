/* 시작점 — 프로젝트 폴더를 읽고 (project.jbs → main) 검사한 뒤 실행
 *   사용법: jawbreaker [옵션] <프로젝트 폴더 | 파일.jb>
 */
#define _GNU_SOURCE
#include "jb.h"
#include "rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>
#include <setjmp.h>
#include <unistd.h>
#include <limits.h>
#ifdef _WIN32   /* MinGW-w64 — 파일 이름은 UTF-8 (실행 파일의 manifest 가 UTF-8 코드 페이지를 켬) */
#include <direct.h>
#include <windows.h>
#define realpath(p, r) _fullpath(NULL, (p), PATH_MAX)
#define mkdir(p, m) _mkdir(p)
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

Project P;
const char *JB_EXE_DIR = ".";

/* 오류: 시도 중(jb_try_parse)이면 되돌아감 · 아니면 찍고 끝 */
static __thread jmp_buf *tryJmp;
void jb_fail(const char *file, int line, const char *fmt, ...) {
    va_list ap;
    if (tryJmp) { longjmp(*tryJmp, 1); }
    static pthread_mutex_t once = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&once);
    fflush(stdout);
    char msg[4096]; va_start(ap, fmt); vsnprintf(msg, sizeof msg, fmt, ap); va_end(ap);
    if (file && *file) fprintf(stderr, "✖ %s:%d  %s\n", file, line, msg);
    else fprintf(stderr, "✖ %s\n", msg);
    fflush(stderr);
    rt_show_error(file, line, msg);   /* 화면이 열려 있으면 오류를 보여 주고 닫힐 때까지 */
    _exit(1);
}
int jb_try_parse(const char *path, int isLib, NodeList *out) {
    jmp_buf jb; jmp_buf *prev = tryJmp; tryJmp = &jb;
    if (setjmp(jb)) { tryJmp = prev; return 0; }
    SrcFile *f = proj_file(path);
    int nt; Tok *t = jb_lex(f->text, f->path, &nt);
    *out = jb_parse(t, nt, f->path, isLib);
    tryJmp = prev; return 1;
}
void *jb_alloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) { fputs("메모리가 부족합니다\n", stderr); _exit(1); } return p; }
void *jb_calloc(size_t n) { void *p = calloc(1, n ? n : 1); if (!p) { fputs("메모리가 부족합니다\n", stderr); _exit(1); } return p; }
void *jb_realloc(void *p, size_t n) { p = realloc(p, n ? n : 1); if (!p) { fputs("메모리가 부족합니다\n", stderr); _exit(1); } return p; }
char *jb_strdup(const char *s) { size_t L = strlen(s); char *r = jb_alloc(L + 1); memcpy(r, s, L + 1); return r; }
char *jb_strndup(const char *s, size_t n) { char *r = jb_alloc(n + 1); memcpy(r, s, n); r[n] = 0; return r; }
char *jb_fmt(const char *fmt, ...) {
    va_list ap, aq; va_start(ap, fmt); va_copy(aq, ap);
    int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    char *r = jb_alloc(n + 1); vsnprintf(r, n + 1, fmt, aq); va_end(aq); return r;
}
static pthread_mutex_t IM = PTHREAD_MUTEX_INITIALIZER;
typedef struct In { struct In *next; size_t n; char s[]; } In;
static In *ITab[8192];
const char *jb_intern(const char *s, size_t n) {
    uint32_t h = 2166136261u; for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 16777619u; }
    pthread_mutex_lock(&IM);
    In **b = &ITab[h & 8191];
    for (In *e = *b; e; e = e->next) if (e->n == n && !memcmp(e->s, s, n)) { pthread_mutex_unlock(&IM); return e->s; }
    In *e = jb_alloc(sizeof(In) + n + 1); e->n = n; memcpy(e->s, s, n); e->s[n] = 0; e->next = *b; *b = e;
    pthread_mutex_unlock(&IM);
    return e->s;
}
const char *jb_in(const char *s) { return jb_intern(s, strlen(s)); }

/* ---------- 프로젝트 파일 ---------- */
static int endsWith(const char *s, const char *t) { size_t a = strlen(s), b = strlen(t); return a >= b && !strcmp(s + a - b, t); }
static char *readAll(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb"); if (!f) return NULL;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *b = jb_alloc(n + 1); size_t r = fread(b, 1, n, f); b[r] = 0; fclose(f);
    *len = r; return b;
}
static int isText(const char *p) { return endsWith(p, ".jbs") || endsWith(p, ".jb") || endsWith(p, ".jbl") || endsWith(p, ".json") || endsWith(p, ".tl"); }
static void scan(const char *dir, const char *rel) {
    char full[PATH_MAX]; snprintf(full, sizeof full, "%s%s%s", dir, *rel ? "/" : "", rel);
    DIR *d = opendir(full); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char r2[PATH_MAX]; snprintf(r2, sizeof r2, "%s%s%s", rel, *rel ? "/" : "", e->d_name);
        char f2[PATH_MAX * 2]; snprintf(f2, sizeof f2, "%s/%s", dir, r2);
        struct stat st; if (stat(f2, &st)) continue;
        if (S_ISDIR(st.st_mode)) { scan(dir, r2); continue; }
        P.files = jb_realloc(P.files, sizeof(SrcFile) * (P.nfiles + 1));
        SrcFile *sf = &P.files[P.nfiles++]; sf->path = jb_strdup(r2); sf->text = NULL; sf->len = (size_t)st.st_size;
        if (isText(r2)) {
            size_t L; char *t = readAll(f2, &L);
            if (t && !endsWith(r2, ".tl")) { char *w = t; for (char *q = t; *q; q++) if (!(*q == '\r' && q[1] == '\n')) *w++ = *q; *w = 0; L = w - t; }
            sf->text = t ? t : jb_strdup(""); sf->len = L;
        }
    }
    closedir(d);
}
static int cmpFile(const void *a, const void *b) { return strcmp(((const SrcFile*)a)->path, ((const SrcFile*)b)->path); }
SrcFile *proj_file(const char *path) {
    if (!path) return NULL;
    SrcFile key; key.path = (char*)path;
    return bsearch(&key, P.files, P.nfiles, sizeof(SrcFile), cmpFile);
}
const char *proj_text(const char *path) {
    SrcFile *f = proj_file(path); if (!f) return NULL;
    if (!f->text) { char full[PATH_MAX * 2]; snprintf(full, sizeof full, "%s/%s", P.root, f->path); size_t L; f->text = readAll(full, &L); if (!f->text) f->text = jb_strdup(""); else f->len = L; }
    return f->text;
}
/* 바이너리 파일 (이미지 · 소리 · 글꼴) — 처음 쓸 때 읽음 */
const unsigned char *proj_bytes(const char *path, size_t *len) {
    SrcFile *f = proj_file(path); if (!f) return NULL;
    proj_text(path);
    *len = f->len; return (const unsigned char *)f->text;
}
static char *normPath(const char *p) {
    char *buf = jb_strdup(p), *out = jb_alloc(strlen(p) + 2); out[0] = 0;
    char *segs[1024]; int n = 0;
    for (char *q = buf; *q; q++) if (*q == '\\') *q = '/';
    char *save = NULL;
    for (char *s = strtok_r(buf, "/", &save); s; s = strtok_r(NULL, "/", &save)) {
        if (!strcmp(s, ".")) continue;
        if (!strcmp(s, "..")) { if (!n) { free(buf); free(out); return NULL; } n--; continue; }
        if (n < 1024) segs[n++] = s;
    }
    for (int i = 0; i < n; i++) { if (i) strcat(out, "/"); strcat(out, segs[i]); }
    free(buf); return out;
}
char *path_resolve(const char *raw, const char *fromFile) {
    while (*raw == ' ' || *raw == '\t' || *raw == '\n' || *raw == '\r') raw++;
    char *p = jb_strdup(raw); size_t L = strlen(p); while (L && (p[L-1] == ' ' || p[L-1] == '\t' || p[L-1] == '\n' || p[L-1] == '\r')) p[--L] = 0;
    for (char *q = p; *q; q++) if (*q == '\\') *q = '/';
    int rel = !strncmp(p, "./", 2) || !strncmp(p, "../", 3);
    char *s = p; if (*s == '/') s++;
    char *full = jb_alloc(strlen(fromFile) + strlen(s) + 3); full[0] = 0;
    if (rel) { strcpy(full, fromFile); char *sl = strrchr(full, '/'); if (sl) sl[1] = 0; else full[0] = 0; if (!*full) strcpy(full, "/"); }
    strcat(full, s);
    char *r = normPath(full); free(full); free(p); return r;
}

/* ---------- 실행 파일 안에 묶은 프로젝트 (편집기의 EXE 변환) ----------
 *   실행 파일 끝: … "JBPK" u32 개수 · (u32 경로 길이 · 경로 · u32 크기 · 내용)… · u64 시작 위치 · "JBPAYLD1"
 *   쓰는 파일 (genJson · 저장) 은 실행 파일 옆 "<이름>_data" 폴더 — 다음 실행 때 묶은 파일보다 먼저 읽음
 */
static char *selfExe(void) {
#ifdef _WIN32
    wchar_t w[32768]; DWORD n = GetModuleFileNameW(NULL, w, 32767); if (!n) return NULL; w[n] = 0;
    int L = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL); char *r = jb_alloc(L + 1);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, r, L, NULL, NULL);
    for (char *q = r; *q; q++) if (*q == '\\') *q = '/';
    return r;
#else
    char buf[PATH_MAX]; ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return NULL; buf[n] = 0; return jb_strdup(buf);
#endif
}
static uint32_t rd32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int loadPayload(const char *exe) {
    FILE *f = fopen(exe, "rb"); if (!f) return 0;
    unsigned char tail[16];
    if (fseek(f, -16, SEEK_END) || fread(tail, 1, 16, f) != 16 || memcmp(tail + 8, "JBPAYLD1", 8)) { fclose(f); return 0; }
    uint64_t off = 0; for (int i = 7; i >= 0; i--) off = off << 8 | tail[i];
    long end = ftell(f) - 16;
    if ((long)off >= end) { fclose(f); return 0; }
    size_t len = (size_t)(end - (long)off); unsigned char *b = jb_alloc(len);
    fseek(f, (long)off, SEEK_SET); if (fread(b, 1, len, f) != len || memcmp(b, "JBPK", 4)) { fclose(f); free(b); return 0; }
    fclose(f);
    uint32_t n = rd32(b + 4); size_t p = 8;
    for (uint32_t i = 0; i < n && p + 8 <= len; i++) {
        uint32_t pl = rd32(b + p); p += 4; char *path = jb_strndup((char *)b + p, pl); p += pl;
        uint32_t dl = rd32(b + p); p += 4; if (p + dl > len) break;
        P.files = jb_realloc(P.files, sizeof(SrcFile) * (P.nfiles + 1));
        SrcFile *sf = &P.files[P.nfiles++]; sf->path = path;
        char *t = jb_alloc(dl + 1); memcpy(t, b + p, dl); t[dl] = 0; p += dl;
        size_t L = dl;
        if (isText(path) && !endsWith(path, ".tl")) { char *w = t; for (char *q = t; q < t + L; q++) if (!(*q == '\r' && q[1] == '\n')) *w++ = *q; *w = 0; L = w - t; }
        sf->text = t; sf->len = L;
    }
    free(b);
    return P.nfiles > 0;
}
/* 저장 폴더의 파일이 묶은 파일을 덮음 */
static void overlay(const char *dir, const char *rel) {
    char full[PATH_MAX]; snprintf(full, sizeof full, "%s%s%s", dir, *rel ? "/" : "", rel);
    DIR *d = opendir(full); if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char r2[PATH_MAX]; snprintf(r2, sizeof r2, "%s%s%s", rel, *rel ? "/" : "", e->d_name);
        char f2[PATH_MAX * 2]; snprintf(f2, sizeof f2, "%s/%s", dir, r2);
        struct stat st; if (stat(f2, &st)) continue;
        if (S_ISDIR(st.st_mode)) { overlay(dir, r2); continue; }
        size_t L; char *t = readAll(f2, &L); if (!t) continue;
        SrcFile *sf = NULL; for (int i = 0; i < P.nfiles; i++) if (!strcmp(P.files[i].path, r2)) sf = &P.files[i];
        if (!sf) { P.files = jb_realloc(P.files, sizeof(SrcFile) * (P.nfiles + 1)); sf = &P.files[P.nfiles++]; sf->path = jb_strdup(r2); }
        else free(sf->text);
        sf->text = t; sf->len = L;
    }
    closedir(d);
}

/* ---------- project.jbs ---------- */
static void readJbs(const char *pickMain) {
    const char *jbs = NULL; int njbs = 0;
    for (int i = 0; i < P.nfiles; i++) if (!strchr(P.files[i].path, '/') && endsWith(P.files[i].path, ".jbs")) { if (!jbs) jbs = P.files[i].path; njbs++; }
    if (!jbs) jb_fail("", 1, "프로젝트 최상위에 .jbs 파일이 없습니다 (디렉토리임을 나타내는 프로젝트 파일이 필요합니다)");
    if (njbs > 1) jb_fail(jbs, 1, ".jbs 파일이 여러 개입니다");
    const char *src = proj_text(jbs);
    int ver = -1, ln = 0;
    const char *p = src;
    while (*p) {
        const char *e = strchr(p, '\n'); if (!e) e = p + strlen(p);
        ln++;
        char *line = jb_strndup(p, e - p);
        /* 줄 처음이나 공백 뒤의 // 만 주석 */
        for (char *q = line; *q; q++) if (q[0] == '/' && q[1] == '/' && (q == line || q[-1] == ' ' || q[-1] == '\t')) { *q = 0; break; }
        char *a = line; while (*a == ' ' || *a == '\t' || *a == '\r') a++;
        size_t L = strlen(a); while (L && (a[L-1] == ' ' || a[L-1] == '\t' || a[L-1] == '\r')) a[--L] = 0;
        if (*a) {
            if (ver < 0) {
                if (strncmp(a, "jbs", 3) || !(a[3] == ' ' || a[3] == '\t')) jb_fail(jbs, ln, "프로젝트 파일의 첫 줄은 'jbs 1' 이어야 합니다");
                char *v = a + 3; while (*v == ' ' || *v == '\t') v++;
                for (char *q = v; *q; q++) if (*q < '0' || *q > '9') jb_fail(jbs, ln, "프로젝트 파일의 첫 줄은 'jbs 1' 이어야 합니다");
                ver = atoi(v); if (ver != 1) jb_fail(jbs, ln, "지원하지 않는 jbs 버전입니다: %d", ver);
            } else {
                char *eq = strchr(a, '=');
                char *k = a; size_t kl = 0; while (k[kl] && (k[kl] == '_' || (k[kl] >= 'A' && k[kl] <= 'Z') || (k[kl] >= 'a' && k[kl] <= 'z'))) kl++;
                char *after = k + kl; while (*after == ' ' || *after == '\t') after++;
                if (!kl || !eq || after != eq) jb_fail(jbs, ln, "'키 = 값' 형식이어야 합니다 (예: main = main.jb)");
                char *v = eq + 1; while (*v == ' ' || *v == '\t') v++;
                if (kl == 4 && !strncmp(k, "name", 4)) P.name = jb_strdup(v);
                else if (kl == 4 && !strncmp(k, "main", 4)) P.main = jb_strdup(v);
                else { k[kl] = 0; jb_fail(jbs, ln, "알 수 없는 항목 '%s' (사용 가능: name, main)", k); }
            }
        }
        free(line);
        p = *e ? e + 1 : e;
    }
    if (ver < 0) jb_fail(jbs, 1, "프로젝트 파일에 'jbs 1' 줄이 필요합니다");
    /* 실행할 파일: 지정한 .jb → project.jbs 의 main → main.jb (편집기의 pickMain) */
    char *m = NULL;
    if (pickMain && proj_file(pickMain) && endsWith(pickMain, ".jb")) m = jb_strdup(pickMain);
    if (!m && P.main) { char *n = normPath(P.main); if (n && proj_file(n)) m = n; }
    if (!m && proj_file("main.jb")) m = jb_strdup("main.jb");
    if (!m || !endsWith(m, ".jb")) jb_fail("", 1, "실행할 .jb 파일이 없습니다: %s", m ? m : (P.main ? P.main : "main.jb"));
    P.main = proj_file(m)->path;
}

static void usage(const char *a0) {
    fprintf(stderr,
        "Jawbreaker (C 판)\n"
        "사용법: %s [옵션] <프로젝트 폴더 | 프로젝트 안의 파일.jb>\n"
        "  폴더 안의 .jbs 를 읽고 main 파일을 실행합니다 (파일.jb 를 주면 그 파일을 실행).\n"
        "옵션:\n"
        "  --size WxH        창 크기 (기본 1280x720)\n"
        "  --fullscreen      전체 화면\n"
        "  --check           검사만 하고 실행하지 않음\n"
        "  --no-box          오류를 메시지 상자로 보이지 않음 (콘솔에만)\n"
        "  --mute            소리 끔\n"
        "  --shot 파일.png --shot-at ms   그 시각의 화면을 PNG 로 저장하고 끝 (테스트용)\n"
        "  --keys \"ms:키:d|u,…\"   그 시각에 키를 누르고 뗌 (테스트용)\n"
        "  --log             시스템 메시지(귀속 · 장면 등)도 찍음\n", a0);
}
int main(int argc, char **argv) {
    {   /* 실행 파일이 있는 폴더 (기본 글꼴 찾기) */
#ifndef _WIN32
        char buf[PATH_MAX]; ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
        if (n > 0) { buf[n] = 0; char *sl = strrchr(buf, '/'); if (sl) { *sl = 0; JB_EXE_DIR = jb_strdup(buf); } }
#endif
        if (!strcmp(JB_EXE_DIR, ".") && strchr(argv[0], '/')) { char *d = jb_strdup(argv[0]); *strrchr(d, '/') = 0; JB_EXE_DIR = d; }
    }
    const char *target = NULL;
    int noBox = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-h") || !strcmp(a, "--help")) { usage(argv[0]); return 0; }
        else if (!strcmp(a, "--size") && i + 1 < argc) { sscanf(argv[++i], "%dx%d", &RT_OPT.winW, &RT_OPT.winH); }
        else if (!strcmp(a, "--fullscreen")) RT_OPT.fullscreen = 1;
        else if (!strcmp(a, "--check")) RT_OPT.checkOnly = 1;
        else if (!strcmp(a, "--no-box")) noBox = 1;   /* jdh run · check: 오류는 콘솔에만 (메시지 상자 없이) */
        else if (!strcmp(a, "--mute")) RT_OPT.mute = 1;
        else if (!strcmp(a, "--log")) RT_OPT.sysLog = 1;
        else if (!strcmp(a, "--shot") && i + 1 < argc) RT_OPT.shot = argv[++i];
        else if (!strcmp(a, "--shot-at") && i + 1 < argc) RT_OPT.shotAt = atof(argv[++i]);
        else if (!strcmp(a, "--keys") && i + 1 < argc) RT_OPT.keys = argv[++i];
        else if (a[0] == '-') { usage(argv[0]); return 2; }
        else target = a;
    }
#ifdef _WIN32
    timeBeginPeriod(1);   /* 1ms 타이머 (틱 · 프레임 간격) */
#endif
    char *exe = selfExe();
    if (!target && exe && loadPayload(exe)) {   /* 편집기에서 EXE 로 변환한 게임 */
        RT_OPT.box = !noBox;
        char *dir = jb_strdup(exe); char *sl = strrchr(dir, '/'); if (sl) *sl = 0; JB_EXE_DIR = dir;
        char *base = sl ? sl + 1 : exe; size_t bl = strlen(base); if (bl > 4 && (!strcmp(base + bl - 4, ".exe") || !strcmp(base + bl - 4, ".EXE"))) bl -= 4;
        P.root = jb_fmt("%s/%.*s_data", dir, (int)bl, base);
        mkdir(P.root, 0755);
        overlay(P.root, "");
        qsort(P.files, P.nfiles, sizeof(SrcFile), cmpFile);
        readJbs(NULL);
        jb_build();
        if (RT_OPT.checkOnly) { printf("검사 통과: %s\n", P.main); return 0; }
        jb_run();
        return 0;
    }
    if (!target) { usage(argv[0]); return 2; }
    char *rp = realpath(target, NULL);
    if (!rp) { fprintf(stderr, "열 수 없습니다: %s\n", target); return 1; }
    for (char *q = rp; *q; q++) if (*q == '\\') *q = '/';
    struct stat st; stat(rp, &st);
    const char *pick = NULL;
    if (!S_ISDIR(st.st_mode)) {   /* 파일 → 위로 올라가며 .jbs 가 있는 폴더 */
        char *dir = jb_strdup(rp); char *sl = strrchr(dir, '/'); if (sl) *sl = 0;
        char *found = NULL;
        for (char *d = jb_strdup(dir); ; ) {
            DIR *dd = opendir(d); int has = 0;
            if (dd) { struct dirent *e; while ((e = readdir(dd))) if (endsWith(e->d_name, ".jbs")) has = 1; closedir(dd); }
            if (has) { found = d; break; }
            char *s2 = strrchr(d, '/'); if (!s2 || s2 == d) break; *s2 = 0;
        }
        if (!found) { fprintf(stderr, ".jbs 프로젝트 파일이 있는 폴더를 찾을 수 없습니다: %s\n", target); return 1; }
        P.root = found;
        pick = jb_strdup(rp + strlen(found) + 1);
    } else P.root = rp;
    scan(P.root, "");
    qsort(P.files, P.nfiles, sizeof(SrcFile), cmpFile);
    readJbs(pick);
    jb_build();
    if (RT_OPT.checkOnly) { printf("검사 통과: %s (라이브러리 %d · 코드 파일 %d)\n", P.main, P.nlibs, P.ntasks); return 0; }
    jb_run();
    return 0;
}
