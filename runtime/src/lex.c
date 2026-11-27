/* 렉서 — 편집기의 lex() 와 같은 규칙 */
#include "jb.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

static const char *KW[] = {"Use","use","Lib","call","var","public","if","else","loop","while","run","func","true","false","as","obj","cam","spk","to","for",NULL};
static const char *TY[] = {"int","float","string","bit","matrix","list","compound","data",NULL};

static int inset(const char **set, const char *s, size_t n) {
    for (int i = 0; set[i]; i++) if (strlen(set[i]) == n && !memcmp(set[i], s, n)) return 1;
    return 0;
}
static int idstart(unsigned char c) { return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0x80; }
static int idchar(unsigned char c) { return idstart(c) || (c >= '0' && c <= '9'); }

Tok *jb_lex(const char *src, const char *file, int *count) {
    int cap = 256, n = 0, line = 1;
    Tok *t = jb_alloc(sizeof(Tok) * cap);
    size_t i = 0, len = strlen(src);
#define PUSH(T_, V_, L_) do { if (n == cap) { cap *= 2; t = jb_realloc(t, sizeof(Tok) * cap); } memset(&t[n], 0, sizeof(Tok)); t[n].t = (T_); t[n].v = (V_); t[n].line = (L_); n++; } while (0)
    while (i < len) {
        unsigned char c = (unsigned char)src[i];
        if (c == '\n') { line++; i++; continue; }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v') { i++; continue; }
        if (c == 0xEF && (unsigned char)src[i+1] == 0xBB && (unsigned char)src[i+2] == 0xBF) { i += 3; continue; }   /* BOM */
        if (c == '/' && src[i+1] == '/') { while (i < len && src[i] != '\n') i++; continue; }
        if (c == '/' && src[i+1] == '*') {
            int st = line; i += 2;
            while (i < len && !(src[i] == '*' && src[i+1] == '/')) { if (src[i] == '\n') line++; i++; }
            if (i >= len) jb_fail(file, st, "주석(/* */)이 닫히지 않았습니다");
            i += 2; continue;
        }
        if (c >= '0' && c <= '9') {
            size_t j = i; int isF = 0;
            while (j < len && src[j] >= '0' && src[j] <= '9') j++;
            if (src[j] == '.' && j + 1 < len && src[j+1] >= '0' && src[j+1] <= '9') { isF = 1; j++; while (j < len && src[j] >= '0' && src[j] <= '9') j++; }
            char buf[64]; size_t k = j - i < 63 ? j - i : 63; memcpy(buf, src + i, k); buf[k] = 0;
            PUSH(isF ? T_FLOAT : T_INT, NULL, line);
            t[n-1].num = strtod(buf, NULL);
            snprintf(t[n-1].raw, sizeof t[n-1].raw, "%s", buf);
            i = j; continue;
        }
        if (idstart(c)) {
            size_t j = i; while (j < len && idchar((unsigned char)src[j])) j++;
            const char *w = jb_intern(src + i, j - i);
            PUSH(inset(KW, src + i, j - i) ? T_KW : inset(TY, src + i, j - i) ? T_TYPE : T_ID, w, line);
            i = j; continue;
        }
        if (c == '"') {
            size_t j = i + 1, sc = 64, sn = 0; char *s = jb_alloc(sc);
            while (j < len && src[j] != '"') {
                if (src[j] == '\n') jb_fail(file, line, "문자열이 닫히지 않았습니다 (\")");
                char ch;
                if (src[j] == '\\') {
                    if (src[j+1] == '\n' || src[j+1] == '\r') jb_fail(file, line, "문자열이 닫히지 않았습니다 (\")");
                    char e = src[j+1]; ch = e == 'n' ? '\n' : e == 't' ? '\t' : e; j += 2;
                    if (!e) break;
                } else ch = src[j++];
                if (sn + 2 > sc) { sc *= 2; s = jb_realloc(s, sc); }
                s[sn++] = ch;
            }
            if (j >= len) jb_fail(file, line, "문자열이 닫히지 않았습니다 (\")");
            s[sn] = 0;
            PUSH(T_STR, s, line);
            i = j + 1; continue;
        }
        static const char *TWO[] = {"==","!=","<=",">=","&&","||",NULL};
        int two = 0;
        for (int q = 0; TWO[q]; q++) if (src[i] == TWO[q][0] && src[i+1] == TWO[q][1]) { PUSH(T_OP, jb_intern(TWO[q], 2), line); i += 2; two = 1; break; }
        if (two) continue;
        if (strchr("+-*/%<>=!()[]{};,.|", c) && c) { PUSH(T_OP, jb_intern(src + i, 1), line); i++; continue; }
        {
            char ch[8] = {0}; int k = 1; if (c >= 0x80) { while (k < 4 && ((unsigned char)src[i+k] & 0xC0) == 0x80) k++; }
            memcpy(ch, src + i, k);
            jb_fail(file, line, "알 수 없는 문자입니다: %s", ch);
        }
    }
    PUSH(T_EOF, jb_intern("", 0), line);
    *count = n;
    return t;
#undef PUSH
}
