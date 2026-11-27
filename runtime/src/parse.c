/* 파서 — 편집기의 parse() 를 그대로 옮김 */
#include "jb.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

typedef struct { Tok *toks; int n, p; const char *file; int isLib; int nast; } PS;
static __thread PS *S;

void nl_push(NodeList *l, Node *n) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 4; l->v = jb_realloc(l->v, sizeof(Node*) * l->cap); }
    l->v[l->n++] = n;
}
Node *node_new(int k, int line, const char *file) { Node *n = jb_calloc(sizeof(Node)); n->k = k; n->line = line; n->file = file; return n; }

static Tok *T(void) { return &S->toks[S->p < S->n ? S->p : S->n - 1]; }
static Tok *TK(int k) { int q = S->p + k; if (q >= S->n) q = S->n - 1; return &S->toks[q]; }
static int isOp(const char *v) { return T()->t == T_OP && !strcmp(T()->v, v); }
static int isKw(const char *v) { return T()->t == T_KW && !strcmp(T()->v, v); }
static int LW(const char *v) { return T()->t == T_ID && !strcmp(T()->v, v); }
static char dbuf[256];
static const char *desc(Tok *t) {
    if (t->t == T_EOF) return "파일 끝";
    if (t->t == T_STR) return "문자열";
    snprintf(dbuf, sizeof dbuf, "'%s'", t->t == T_INT || t->t == T_FLOAT ? t->raw : t->v); return dbuf;
}
#define E(...) jb_fail(S->file, T()->line, __VA_ARGS__)
#define EL(l, ...) jb_fail(S->file, (l), __VA_ARGS__)
static Node *N(int k, int line) { return node_new(k, line, S->file); }
static Tok *opx(const char *v) { if (!isOp(v)) E("'%s' 이(가) 필요합니다 (현재: %s)", v, desc(T())); return &S->toks[S->p++]; }
static void semi(void) { if (!isOp(";")) EL(S->toks[S->p > 0 ? S->p - 1 : 0].line, "문장 끝에 ';' 이(가) 필요합니다"); S->p++; }
static Tok *ident(const char *what) { if (T()->t != T_ID) E("%s이(가) 필요합니다 (현재: %s)", what, desc(T())); return &S->toks[S->p++]; }

static Node *expr(void);
static Node *primary(void);
static NodeList block(void);
static Node *condOr(void);

static Node *strN(const char *s, int line, int lit) { Node *n = N(N_STR, line); n->str = jb_strdup(s); n->lit = lit; return n; }
static Node *numN(double v, int line) { Node *n = N(N_NUM, line); n->num = v; snprintf(n->raw, sizeof n->raw, "%g", v); return n; }
static Node *binN(int op, Node *a, Node *b, int line) { Node *n = N(N_BIN, line); n->op = op; n->a = a; n->b = b; return n; }
static Node *unN(int op, Node *e, int line) { Node *n = N(N_UN, line); n->op = op; n->a = e; return n; }

/* 오브젝트 속성 이름 → 키 */
static int strieq(const char *a, const char *b) { for (; *a && *b; a++, b++) if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0; return !*a && !*b; }
int obj_prop_key(const char *p) {
    static const struct { const char *n; int k; } T_[] = {
        {"position",K_POS},{"pos",K_POS},{"coord",K_POS},{"scale",K_SCALE},{"color",K_COLOR},{"text",K_TEXT},{"velocity",K_VEL},{"vel",K_VEL},
        {"istrigged",K_ISTRIG},{"istriggered",K_ISTRIG},{"triggedid",K_TRIGID},{"triggeredid",K_TRIGID},{"iscollided",K_ISCOL},{"collidedobj",K_COLID},{NULL,0}};
    for (int i = 0; T_[i].n; i++) if (strieq(T_[i].n, p)) return T_[i].k;
    return 0;
}
static int keepOf(const char *p, int *k, int *i) {   /* Xpos · Ypos · Xscale · Yscale · Xvel · Yvel */
    static const struct { const char *n; int k, i; } T_[] = {{"xscale",K_SCALE,0},{"yscale",K_SCALE,1},{"xpos",K_POS,0},{"ypos",K_POS,1},{"xvel",K_VEL,0},{"yvel",K_VEL,1},{NULL,0,0}};
    for (int q = 0; T_[q].n; q++) if (strieq(T_[q].n, p)) { *k = T_[q].k; *i = T_[q].i; return 1; }
    return 0;
}
/* obj.id.Xpos → idx(objprop(Position), 1, 1 or 2) */
static Node *objComp(int line, const char *id, const char *prop) {
    int k, i; if (!keepOf(prop, &k, &i)) return NULL;
    Node *base = N(N_OBJPROP, line); base->id = id; base->key = k;
    base->prop = k == K_POS ? "Position" : k == K_SCALE ? "Scale" : "Velocity";
    Node *x = N(N_IDX, line); x->a = base; x->b = numN(1, line); x->c = numN(i + 1, line);
    return x;
}

static int isIdStart(unsigned char c) { return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c >= 0x80; }
static int isIdChar(unsigned char c) { return isIdStart(c) || (c >= '0' && c <= '9'); }
static int scanId(const char *s, int i) { if (!isIdStart((unsigned char)s[i])) return -1; while (isIdChar((unsigned char)s[i])) i++; return i; }

/* "...%이름%..." → "" + 이름 + "..." */
static Node *strNode(Tok *t) {
    const char *s = t->v;
    if (!strchr(s, '%')) return strN(s, t->line, 1);
    NodeList parts = {0};
    size_t cap = strlen(s) + 1, ln = 0; char *lit = jb_alloc(cap); lit[0] = 0;
#define FLUSH() do { if (ln) { lit[ln] = 0; nl_push(&parts, strN(lit, t->line, 0)); ln = 0; } } while (0)
    int i = 0, L = (int)strlen(s);
    while (i < L) {
        char c = s[i];
        if (c == '%') {
            if (s[i+1] == '%') { lit[ln++] = '%'; i += 2; continue; }
            /* %a% · %a.b% · %a.b.c% */
            int segs[3][2], ns = 0, j = i + 1, okm = 0;
            for (;;) {
                int e = scanId(s, j); if (e < 0) break;
                segs[ns][0] = j; segs[ns][1] = e; ns++; j = e;
                if (s[j] == '%') { okm = 1; break; }
                if (s[j] == '.' && ns < 3) { j++; continue; }
                break;
            }
            if (!okm) {   /* %리스트[번호]% 등 — 앞부분 이름(.이름) 다음 [ */
                int e0 = scanId(s, i + 1);
                if (e0 > 0) {
                    int jj = e0;
                    if (s[jj] == '.') { int e1 = scanId(s, jj + 1); if (e1 > 0) jj = e1; else jj = -1; }
                    if (jj > 0 && s[jj] == '[') {
                        int ok = 0;
                        while (s[jj] == '[') {
                            int d = 0, k = jj;
                            for (; k < L; k++) { if (s[k] == '[') d++; else if (s[k] == ']' && --d == 0) break; else if (s[k] == '%' || s[k] == '"') break; }
                            if (k >= L || s[k] != ']') break;
                            jj = k + 1; ok = 1;
                        }
                        if (ok && s[jj] == '%') {
                            char *src = jb_strndup(s + i + 1, jj - i - 1);
                            int nt; Tok *sub = jb_lex(src, S->file, &nt);
                            for (int q = 0; q < nt; q++) sub[q].line = t->line;
                            PS sv = *S; S->toks = sub; S->n = nt; S->p = 0;
                            Node *e = primary();
                            if (T()->t != T_EOF) { int l2 = t->line; *S = sv; EL(l2, "문자열 안의 %%%s%% 은(는) 변수[번호] 형태여야 합니다", src); }
                            *S = sv;
                            FLUSH(); nl_push(&parts, e); i = jj + 1; free(src); continue;
                        }
                    }
                }
            }
            if (okm) {
                const char *sg[3]; for (int q = 0; q < ns; q++) sg[q] = jb_intern(s + segs[q][0], segs[q][1] - segs[q][0]);
                int isObj = !strcmp(sg[0], "obj") && ns > 1;
                const char *oid = isObj && ns == 3 ? sg[1] : NULL, *on = isObj ? sg[ns - 1] : NULL;
                Node *comp = isObj ? objComp(t->line, oid, on) : NULL;
                int key = isObj && !comp ? obj_prop_key(on) : 0;
                int ovar = isObj && !comp && !key && oid != NULL;
                if (!isObj || comp || key || ovar) {
                    FLUSH();
                    if (comp) nl_push(&parts, comp);
                    else if (key) { Node *x = N(N_OBJPROP, t->line); x->id = oid; x->prop = on; x->key = key; nl_push(&parts, x); }
                    else if (ovar) { Node *x = N(N_OBJVAR, t->line); x->id = oid; x->name = on; nl_push(&parts, x); }
                    else if (ns == 2) { Node *x = N(N_MEMBER, t->line); x->lib = sg[0]; x->name = sg[1]; nl_push(&parts, x); }
                    else { Node *x = N(N_ID, t->line); x->name = sg[0]; nl_push(&parts, x); }   /* 편집기와 같이: 첫 이름 */
                    i = j + 1; continue;
                }
            }
        }
        lit[ln++] = c; i++;
    }
    if (!parts.n) { lit[ln] = 0; Node *r = strN(lit, t->line, 1); free(lit); return r; }
    FLUSH(); free(lit);
    Node *e = strN("", t->line, 0);
    for (int q = 0; q < parts.n; q++) e = binN('+', e, parts.v[q], t->line);
    e->lit = 1; free(parts.v);
    return e;
#undef FLUSH
}
static Node *withIdx(Node *base) {
    while (isOp("[")) {
        Tok *o = &S->toks[S->p++]; Node *i = expr();
        if (isOp("]")) { S->p++; Node *x = N(N_IDX, o->line); x->a = base; x->b = i; base = x; continue; }
        if (!isOp(",")) E("행렬 성분은 [행, 열], 리스트 항목은 [번호] 로 씁니다 (예: m[1, 2] · list[3])");
        S->p++; Node *j = expr(); opx("]");
        Node *x = N(N_IDX, o->line); x->a = base; x->b = i; x->c = j; base = x;
    }
    return base;
}
static Node *matLit(Tok *t) {
    Node *m = N(N_MAT, t->line);
    if (isOp("]")) { S->p++; m->empty = 1; m->nrows = 1; m->rowLen = jb_calloc(sizeof(int)); return m; }
    int cap = 4; m->rowLen = jb_calloc(sizeof(int) * cap); m->nrows = 1;
    for (;;) {
        nl_push(&m->args, expr()); m->rowLen[m->nrows - 1]++;
        if (isOp(",")) { S->p++; continue; }
        if (isOp("|")) { S->p++; if (m->nrows == cap) { cap *= 2; m->rowLen = jb_realloc(m->rowLen, sizeof(int) * cap); } m->rowLen[m->nrows++] = 0; continue; }
        break;
    }
    opx("]");
    for (int r = 1; r < m->nrows; r++) if (m->rowLen[r] != m->rowLen[0]) {
        char b[256] = ""; for (int q = 0; q < m->nrows; q++) { char x[16]; snprintf(x, sizeof x, "%s%d", q ? " / " : "", m->rowLen[q]); strcat(b, x); }
        EL(t->line, "행렬의 각 행은 성분 개수가 같아야 합니다 (%s)", b);
    }
    return m;
}
/* obj.id.Position · obj.Xscale · obj.id.변수 */
static Node *objRef(Tok *t) {
    opx("."); Tok *a = ident("오브젝트 id (또는 Xscale · Yscale · Xpos · Ypos)");
    if (isOp(".")) {
        S->p++; Tok *b = ident("속성 이름 (Position, Scale, Velocity, Color, Text, Xpos, Ypos, Xscale, Yscale, Xvel, Yvel) 또는 귀속된 파일의 변수 이름");
        Node *comp = objComp(t->line, a->v, b->v); if (comp) return comp;
        int key = obj_prop_key(b->v);
        if (!key) { Node *x = N(N_OBJVAR, t->line); x->id = a->v; x->name = b->v; return x; }
        Node *x = N(N_OBJPROP, t->line); x->id = a->v; x->prop = b->v; x->key = key; return x;
    }
    int k, i;
    if (keepOf(a->v, &k, &i)) { Node *x = N(N_OBJKEEP, t->line); x->name = a->v; x->keepK = k; x->keepI = i; return x; }
    int key = obj_prop_key(a->v);
    if (key) { Node *x = N(N_OBJPROP, t->line); x->id = NULL; x->prop = a->v; x->key = key; return x; }
    EL(a->line, "'obj.%s' 을(를) 알 수 없습니다 — obj.id.Position 처럼 쓰거나, obj.Xscale · obj.Yscale · obj.Xpos · obj.Ypos · obj.Xvel · obj.Yvel 을 쓰세요", a->v);
    return NULL;
}
static void camFix(Node *x, const char *pre, const char *what, int line) {
    if (x->k == N_IDX) { camFix(x->a, pre, what, line); return; }
    if (x->k == N_OBJPROP || x->k == N_OBJVAR) {
        if (!x->id) EL(line, "%s는 %s.id.%s 처럼 id 를 써야 합니다", what, pre, x->prop ? x->prop : x->name);
        x->id = jb_in(jb_fmt("%s.%s", pre, x->id)); return;
    }
    EL(line, "%s는 %s.id.Position · %s.id.Xpos 처럼 씁니다", what, pre, pre);
}
static Node *camRef(Tok *t) { Node *r = objRef(t); camFix(r, t->v, !strcmp(t->v, "cam") ? "카메라" : "스피커", t->line); return r; }
static NodeList argsList(void) {
    NodeList a = {0}; opx("(");
    if (!isOp(")")) { for (;;) { nl_push(&a, expr()); if (isOp(",")) { S->p++; continue; } break; } }
    opx(")"); return a;
}
static Node *callN(int line, const char *lib, const char *name) { Node *c = N(N_CALL, line); c->lib = lib; c->name = name; return c; }
static Node *primary(void) {
    Tok *t = T();
    if (t->t == T_INT || t->t == T_FLOAT) { S->p++; Node *n = N(N_NUM, t->line); n->num = t->num; n->isFloat = t->t == T_FLOAT; snprintf(n->raw, sizeof n->raw, "%s", t->raw); return n; }
    if (t->t == T_STR) { S->p++; return strNode(t); }
    if (t->t == T_KW && (!strcmp(t->v, "true") || !strcmp(t->v, "false"))) { S->p++; Node *n = N(N_BOOL, t->line); n->num = !strcmp(t->v, "true"); return n; }
    if (t->t == T_OP && !strcmp(t->v, "(")) { S->p++; Node *e = expr(); opx(")"); return e; }
    if (t->t == T_OP && !strcmp(t->v, "[")) { S->p++; return matLit(t); }
    if (t->t == T_KW && !strcmp(t->v, "obj")) { S->p++; Node *r = objRef(t); return r->k == N_OBJPROP || r->k == N_OBJVAR ? withIdx(r) : r; }
    if (t->t == T_KW && (!strcmp(t->v, "cam") || !strcmp(t->v, "spk"))) { S->p++; Node *r = camRef(t); return r->k == N_OBJPROP || r->k == N_OBJVAR ? withIdx(r) : r; }
    if (t->t == T_ID) {
        S->p++;
        if (isOp(".")) {
            S->p++;
            if (isKw("cam") || isKw("spk")) {   /* seo.cam.id.setZoom(…) · sou.spk.id.belongto(…) */
                const char *sub = T()->v; S->p++; opx(".");
                Tok *a = ident(!strcmp(sub, "cam") ? "카메라 id" : "스피커 id"); opx(".");
                Tok *f = ident("명령어 이름");
                if (!isOp("(")) EL(f->line, "'%s.%s.%s.%s' 다음에는 ( ) 가 필요합니다 (명령어 호출)", t->v, sub, a->v, f->v);
                Node *c = callN(t->line, t->v, f->v); c->sub = sub;
                nl_push(&c->args, strN(a->v, a->line, 0));
                NodeList r = argsList(); for (int q = 0; q < r.n; q++) nl_push(&c->args, r.v[q]);
                return c;
            }
            if (isKw("obj")) {   /* can.obj.id.isMouseTouched · col.obj.id.isCollided · mag.obj.id.runAnime(…) */
                S->p++; opx("."); Tok *a = ident("오브젝트 id");
                if (isOp("(")) { Node *c = callN(t->line, t->v, a->v); c->args = argsList(); c->canObj = 1; return c; }
                const char *id = a->v, *prop;
                if (isOp(".")) { S->p++; prop = ident("속성 이름 (isMouseTouched · IsButtonClicked)")->v; } else { prop = a->v; id = NULL; }
                if (id && isOp("(")) {
                    Node *idn = strN(id, a->line, 0); NodeList as = argsList();
                    Node *c = callN(t->line, t->v, prop); c->canObj = 1; c->objId = 1;
                    if (!strcmp(prop, "setExistableScene")) { for (int q = 0; q < as.n; q++) nl_push(&c->args, as.v[q]); nl_push(&c->args, idn); }
                    else { nl_push(&c->args, idn); for (int q = 0; q < as.n; q++) nl_push(&c->args, as.v[q]); }
                    return c;
                }
                if (strieq(prop, "iscollided") || strieq(prop, "collidedobj")) { Node *x = N(N_OBJPROP, t->line); x->id = id; x->prop = prop; x->key = strieq(prop, "iscollided") ? K_ISCOL : K_COLID; x->colLib = t->v; return x; }
                if (strieq(prop, "animeframe")) { Node *x = N(N_OBJPROP, t->line); x->id = id; x->prop = prop; x->key = K_ANIFR; x->colLib = t->v; return x; }
                if (strieq(prop, "isbuttonpushed") || strieq(prop, "isbuttonrelease") || strieq(prop, "isbuttonreleased")) {
                    Node *x = N(N_CANOBJ, t->line); x->lib = t->v; x->id = id; x->prop = prop; x->btn = strieq(prop, "isbuttonpushed") ? 'p' : 'r'; return x;
                }
                if (!strieq(prop, "ismousetouched") && !strieq(prop, "isbuttonclicked")) EL(a->line, "'%s.obj.id.' 다음에는 isMouseTouched 나 IsButtonClicked 를 씁니다 (받은 이름: %s)", t->v, prop);
                Node *x = N(N_CANOBJ, t->line); x->lib = t->v; x->id = id; x->prop = prop; x->btn = strieq(prop, "isbuttonclicked") ? 'c' : 0; return x;
            }
            Tok *m = ident("명령어/변수 이름");
            if (isOp("(")) { Node *c = callN(t->line, t->v, m->v); c->args = argsList(); return c; }
            Node *x = N(N_MEMBER, t->line); x->lib = t->v; x->name = m->v; return withIdx(x);
        }
        if (isOp("(")) { Node *c = callN(t->line, NULL, t->v); c->args = argsList(); return c; }
        Node *x = N(N_ID, t->line); x->name = t->v; return withIdx(x);
    }
    E("값(식)이 필요합니다 (현재: %s)", desc(t));
    return NULL;
}
static Node *unary(void) {
    if (T()->t == T_OP && (!strcmp(T()->v, "-") || !strcmp(T()->v, "!"))) { Tok *o = &S->toks[S->p++]; return unN(o->v[0], unary(), o->line); }
    return primary();
}
static int NOTJ(const char *v) { return !strcmp(v,"and")||!strcmp(v,"or")||!strcmp(v,"xor")||!strcmp(v,"nand")||!strcmp(v,"nor")||!strcmp(v,"not"); }
static int termStart(void) {
    Tok *t = T();
    if (t->t == T_STR || t->t == T_INT || t->t == T_FLOAT) return 1;
    if (t->t == T_OP) return !strcmp(t->v, "(");
    if (t->t == T_KW) return !strcmp(t->v,"obj")||!strcmp(t->v,"cam")||!strcmp(t->v,"spk")||!strcmp(t->v,"true")||!strcmp(t->v,"false");
    return t->t == T_ID && !NOTJ(t->v);
}
static const char *LV[6][5] = {{"||"},{"&&"},{"==","!="},{"<",">","<=",">="},{"+","-"},{"*","/","%"}};
static int opCode(const char *v) {
    if (!strcmp(v,"<=")) return 'l'; if (!strcmp(v,">=")) return 'g'; if (!strcmp(v,"==")) return 'e'; if (!strcmp(v,"!=")) return 'n';
    if (!strcmp(v,"&&")) return '&'; if (!strcmp(v,"||")) return '|';
    return v[0];
}
static int inLv(int l, const char *v) { for (int q = 0; q < 5 && LV[l][q]; q++) if (!strcmp(LV[l][q], v)) return 1; return 0; }
static Node *binL(int l) {
    if (l >= 6) return unary();
    Node *a = binL(l + 1); int lastLit = a->lit;
    for (;;) {
        if (T()->t == T_OP && inLv(l, T()->v)) {
            Tok *o = &S->toks[S->p++]; Node *b = binL(l + 1);
            a = binN(opCode(o->v), a, b, o->line); lastLit = b->lit; continue;
        }
        if (l == 4 && (T()->t == T_STR || (lastLit && termStart()))) {
            int ln = T()->line; Node *b = binL(l + 1);
            a = binN('+', a, b, ln); lastLit = b->lit; continue;
        }
        return a;
    }
}
static Node *expr(void) { return binL(0); }
static Node *condUnit(void) {
    if (LW("not")) { Tok *o = &S->toks[S->p++]; return unN('!', condUnit(), o->line); }
    if (isOp("(") && ((TK(1)->t == T_OP && !strcmp(TK(1)->v, "[")) ||
        (TK(1)->t == T_ID && !strcmp(TK(1)->v, "not") && TK(2)->t == T_OP && (!strcmp(TK(2)->v, "[") || !strcmp(TK(2)->v, "("))))) {
        S->p++; Node *e = condOr(); opx(")"); return e;
    }
    const char *close;
    if (isOp("[")) close = "]"; else if (isOp("(")) close = ")";
    else { E("조건은 [ ] 안에 써야 합니다 (현재: %s)", desc(T())); return NULL; }
    S->p++; Node *e = expr(); opx(close); return e;
}
static Node *boolN(Node *e) { return unN('!', unN('!', e, e->line), e->line); }
static Node *condJoin(Node *(*next)(void), const char **words) {
    Node *a = next();
    for (;;) {
        Tok *t = T(); const char *w = NULL;
        int has_and = 0, has_or = 0;
        for (int q = 0; words[q]; q++) { if (!strcmp(words[q], "and")) has_and = 1; if (!strcmp(words[q], "or")) has_or = 1; }
        if (t->t == T_ID) for (int q = 0; words[q]; q++) if (!strcmp(words[q], t->v)) w = words[q];
        if (!w && t->t == T_OP && !strcmp(t->v, "&&") && has_and) w = "and";
        if (!w && t->t == T_OP && !strcmp(t->v, "||") && has_or) w = "or";
        if (!w) return a;
        S->p++;
        if (!(isOp("[") || isOp("(") || LW("not"))) E("'%s' 다음에는 [조건] 이 와야 합니다 (예: if [a > 1] %s [b < 2]{ })", t->v, w);
        Node *b = next();
        if (!strcmp(w, "and") || !strcmp(w, "nand")) { a = binN('&', a, b, t->line); if (!strcmp(w, "nand")) a = unN('!', a, t->line); }
        else if (!strcmp(w, "or") || !strcmp(w, "nor")) { a = binN('|', a, b, t->line); if (!strcmp(w, "nor")) a = unN('!', a, t->line); }
        else a = binN('n', boolN(a), boolN(b), t->line);
    }
}
static const char *W_AND[] = {"and","nand",NULL}, *W_XOR[] = {"xor",NULL}, *W_OR[] = {"or","nor",NULL};
static Node *condAnd(void) { return condJoin(condUnit, W_AND); }
static Node *condXor(void) { return condJoin(condAnd, W_XOR); }
static Node *condOr(void) { return condJoin(condXor, W_OR); }

static Node *stmt(void);
static NodeList block(void) {
    opx("{"); NodeList b = {0};
    while (!isOp("}")) {
        if (T()->t == T_EOF) EL(S->toks[S->p > 0 ? S->p - 1 : 0].line, "'}' 이(가) 필요합니다 — 블록이 닫히지 않았습니다");
        nl_push(&b, stmt());
    }
    S->p++; return b;
}
static int tyOf(const char *v) {
    if (!strcmp(v,"int")) return TY_INT; if (!strcmp(v,"float")) return TY_FLOAT; if (!strcmp(v,"string")) return TY_STRING;
    if (!strcmp(v,"bit")) return TY_BIT; if (!strcmp(v,"matrix")) return TY_MATRIX; if (!strcmp(v,"list")) return TY_LIST;
    if (!strcmp(v,"data")) return TY_DATA;
    return TY_COMPOUND;
}
static void mDims(int *d, int isVar) {
    if (!isOp("[")) E(isVar ? "행렬은 크기를 정해야 합니다 (예: var matrix[2, 3] call 이름;)" : "행렬은 크기를 정해야 합니다 (예: matrix[2, 3])");
    S->p++;
    for (int k = 0; k < 2; k++) {
        Tok *x = T();
        if (x->t != T_INT || x->num < 1) E("행렬의 %s 개수는 1 이상의 정수여야 합니다 (예: matrix[2, 3])", k ? "열" : "행");
        S->p++; d[k] = (int)x->num; if (!k) opx(",");
    }
    opx("]");
}
static Node *varDecl(int atTop) {
    Tok *t = &S->toks[S->p++]; int pub = 0;
    if (isKw("public")) { if (!atTop) E("public 변수는 파일 최상위에서만 선언할 수 있습니다 (run{ } · func 밖)"); S->p++; pub = 1; }
    if (T()->t != T_TYPE) E("변수 형식(int, float, string, bit, matrix, list, compound)이 필요합니다 (현재: %s)", desc(T()));
    Node *n = N(N_VAR, t->line); n->ty = tyOf(S->toks[S->p++].v); n->pub = pub;
    if (n->ty == TY_MATRIX) { mDims(n->dims, 1); n->hasDims = 1; if ((long)n->dims[0] * n->dims[1] > 100000) E("행렬이 너무 큽니다 (성분 100000개까지)"); }
    if (!isKw("call")) {
        if (n->ty == TY_MATRIX) E("'call' 이(가) 필요합니다 (예: var matrix[%d, %d] call 이름;)", n->dims[0], n->dims[1]);
        E("'call' 이(가) 필요합니다 (예: var %s call 이름;)", ty_name(n->ty));
    }
    S->p++;
    Tok *nm = ident("변수 이름"); n->name = nm->v;
    if (isOp("=")) {
        if (pub) E("public 변수는 선언할 때 값을 넣을 수 없습니다 — run{ } 안에서 '%s = 값;' 으로 넣으세요", nm->v);
        S->p++; n->expr = expr();
    }
    semi(); return n;
}
static Node *ifStmt(void) {
    Tok *t = &S->toks[S->p++]; Node *n = N(N_IF, t->line);
    n->expr = condOr(); n->then = block();
    if (isKw("else")) {
        S->p++; n->hasEls = 1;
        if (isKw("if")) { nl_push(&n->els, ifStmt()); n->elseIf = 1; }
        else n->els = block();
    }
    return n;
}
static Node *loopStmt(void) {
    Tok *t = &S->toks[S->p++]; Node *n = N(N_LOOP, t->line);
    n->expr = (isOp("[") || isOp("(")) ? condOr() : NULL;
    if (isKw("for")) {
        S->p++;
        if (isOp("{")) E("loop for 다음에는 반복할 시간(ms)을 씁니다 (예: loop for 1000{ } · loop [hp > 0] for 3000{ })");
        n->ms = expr();
    }
    n->body = block();
    return n;
}
static Node *stmt(void) {
    Tok *t = T();
    if (t->t == T_KW) {
        if (!strcmp(t->v, "var")) return varDecl(0);
        if (!strcmp(t->v, "if")) return ifStmt();
        if (!strcmp(t->v, "loop")) return loopStmt();
        if (!strcmp(t->v, "while")) E("while 은 loop 로 바뀌었습니다 — 'loop [조건]{ }' 형태로 쓰세요");
        if (!strcmp(t->v, "obj") || !strcmp(t->v, "cam") || !strcmp(t->v, "spk")) {
            S->p++; Node *r0 = !strcmp(t->v, "obj") ? objRef(t) : camRef(t);
            Node *r = r0->k == N_OBJKEEP ? objComp(t->line, NULL, r0->name) : r0->k == N_IDX ? r0 : withIdx(r0);
            if (!isOp("=")) E("obj.id.속성 은 '= 값;' 으로 값을 넣을 때 문장으로 쓸 수 있습니다");
            S->p++; Node *e = expr(); semi();
            if (r->k == N_IDX) { Node *n = N(N_EASSIGN, t->line); n->a = r->a; n->b = r->b; n->c = r->c; n->expr = e; return n; }
            Node *n = N(N_OASSIGN, t->line); n->a = r; n->expr = e; return n;
        }
        if (!strcmp(t->v,"Use")||!strcmp(t->v,"use")||!strcmp(t->v,"Lib")||!strcmp(t->v,"func")||!strcmp(t->v,"run"))
            E("'%s' 은(는) 블록 안에서 쓸 수 없습니다 (파일 최상위에만 쓸 수 있습니다)", t->v);
        if (!strcmp(t->v, "else")) E("else 앞에 짝이 되는 if 가 없습니다 — 바로 위 if 에 이미 else 가 있거나, 위쪽 if 의 { } 가 닫히지 않았는지 확인하세요 (} 하나 빠짐)");
        E("여기서는 '%s' 을(를) 쓸 수 없습니다", t->v);
    }
    if (t->t == T_TYPE) E("변수는 'var %s call 이름;' 형태로 만듭니다", t->v);
    if (t->t == T_ID) {
        Tok *nx = TK(1);
        if (nx->t == T_OP && !strcmp(nx->v, "=")) { S->p += 2; Node *n = N(N_ASSIGN, t->line); n->name = t->v; n->expr = expr(); semi(); return n; }
        if (nx->t == T_OP && !strcmp(nx->v, "[")) {
            S->p++; Node *b = N(N_ID, t->line); b->name = t->v; Node *r = withIdx(b);
            if (!isOp("=")) E("행렬 성분은 '이름[행, 열] = 값;', 리스트 항목은 '이름[번호] = 값;' 으로 넣습니다");
            S->p++; Node *n = N(N_EASSIGN, t->line); n->a = r->a; n->b = r->b; n->c = r->c; n->expr = expr(); semi();
            return n;
        }
        if (nx->t == T_OP && (!strcmp(nx->v, "(") || !strcmp(nx->v, "."))) {
            Node *c = primary();
            if (c->k != N_CALL) EL(t->line, "값만 단독으로 쓸 수 없습니다 — 명령어 호출이어야 합니다 (예: jaw.log(\"hi\");)");
            if (!strcmp(c->name, "setParent") && isOp("(")) {
                S->p++; Node *ch = expr(); opx(")");
                if (!isKw("as")) E("'as' 가 필요합니다 (예: dis.setParent() (\"자식\") as (\"부모\");)");
                S->p++; opx("("); Node *pa = expr(); opx(")");
                c->relChild = ch; c->relParent = pa;
            } else if (!strcmp(c->name, "setParent") && isKw("obj")) {
                S->p++; opx("."); Tok *ch = ident("자식 오브젝트 id");
                if (!isKw("as")) E("'as' 가 필요합니다 (예: dis.setParent() (\"자식\") as (\"부모\");)");
                S->p++;
                if (!isKw("obj")) E("'as' 다음에는 obj.부모id 를 씁니다 (예: dis.setParent() obj.자식 as obj.부모;)");
                S->p++; opx("."); Tok *pa = ident("부모 오브젝트 id");
                c->relChild = strN(ch->v, ch->line, 0); c->relParent = strN(pa->v, pa->line, 0);
            }
            if (LW("save")) {
                S->p++; if (!isKw("as")) E("'save' 다음에는 'as' 가 필요합니다 (예: mag.importFile(\"img/a.png\") save as (\"hero\");)");
                S->p++; c->saveAs = expr();
            }
            if (isKw("to")) { S->p++; c->toExpr = expr(); if (isKw("for")) { S->p++; c->forExpr = expr(); } }
            else if (isKw("for")) { S->p++; c->forExpr = expr(); }
            if (LW("ease")) { S->p++; c->easeExpr = expr(); }
            if (isKw("as")) { S->p++; c->asName = ident("받을 변수 이름")->v; c->asLine = S->toks[S->p - 1].line; }
            semi(); return c;
        }
    }
    E("알 수 없는 문장입니다: %s", desc(t));
    return NULL;
}
static Node *useStmt(void) {
    Tok *t = &S->toks[S->p++]; Tok *lib = ident("라이브러리 id");
    if (!isKw("call")) E("'call' 이(가) 필요합니다 (예: Use %s call 이름)", lib->v);
    S->p++; Tok *al = ident("별칭 이름");
    if (isOp(";")) S->p++;
    Node *n = N(N_USE, t->line); n->libId = lib->v; n->alias = al->v; return n;
}
static Node *libHeader(void) {
    Tok *t = &S->toks[S->p++];
    if (S->nast) EL(t->line, "Lib 헤더는 파일 맨 위(첫 문장)에 있어야 합니다");
    if (!LW("id")) E("'Lib id 이름;' 형태로 써야 합니다 (예: Lib id greet parent base;)");
    S->p++; Tok *id = ident("라이브러리 id");
    Node *n = N(N_LIB, t->line); n->libId = id->v;
    if (LW("parent")) { S->p++; n->parent = ident("부모 라이브러리 id")->v; }
    semi(); return n;
}
static Node *funcDef(void) {
    Tok *t = &S->toks[S->p++];
    Node *n = N(N_FUNC, t->line);
    if (T()->t == T_TYPE) { n->retTy = tyOf(S->toks[S->p++].v); if (n->retTy == TY_MATRIX) mDims(n->retDims, 0); }
    n->name = ident("함수 이름")->v; opx("(");
    int cap = 4; n->params = jb_calloc(sizeof(Param) * cap);
    if (!isOp(")")) {
        for (;;) {
            if (T()->t != T_TYPE) E("매개변수 형식이 필요합니다 (현재: %s)", desc(T()));
            Param pm; memset(&pm, 0, sizeof pm); pm.ty = tyOf(S->toks[S->p++].v); if (pm.ty == TY_MATRIX) mDims(pm.dims, 0);
            pm.name = ident("매개변수 이름")->v;
            if (n->nparams == cap) { cap *= 2; n->params = jb_realloc(n->params, sizeof(Param) * cap); }
            n->params[n->nparams++] = pm;
            if (isOp(",")) { S->p++; continue; }
            break;
        }
    }
    opx(")");
    n->body = block();
    return n;
}
static Node *top(void) {
    Tok *t = T();
    if (t->t == T_ID && TK(1)->t == T_OP && !strcmp(TK(1)->v, ".") && TK(2)->t == T_ID && (!strcmp(TK(2)->v, "BelongObj") || !strcmp(TK(2)->v, "BelongScene"))) {
        int sc = !strcmp(TK(2)->v, "BelongScene");
        E("%s 는 이제 경로 없이 run{ } 안에 씁니다 — 귀속시킬 파일의 run{ } 안에 %s.%s(\"%s\"); (그 파일 전체가 귀속됨)", TK(2)->v, t->v, TK(2)->v, sc ? "장면" : "id");
    }
    if (t->t == T_KW) {
        if (!strcmp(t->v, "Use") || !strcmp(t->v, "use")) return useStmt();
        if (!strcmp(t->v, "var")) return varDecl(1);
        if (!strcmp(t->v, "Lib")) { if (!S->isLib) E("Lib 헤더는 라이브러리(.jbl) 파일에서만 쓸 수 있습니다"); return libHeader(); }
        if (!strcmp(t->v, "run")) { if (S->isLib) E("run{ } 은 코드 파일(.jb) 에서만 쓸 수 있습니다"); S->p++; Node *n = N(N_RUN, t->line); n->body = block(); return n; }
        if (!strcmp(t->v, "func")) { if (!S->isLib) E("func 는 라이브러리(.jbl) 파일에서만 정의할 수 있습니다"); return funcDef(); }
    }
    if (!S->isLib) E("코드는 run{ ... } 안에 써야 실행됩니다 (파일 최상위에는 Use, var, run 만 쓸 수 있습니다)");
    E("라이브러리 최상위에는 Lib, Use, var, func 만 쓸 수 있습니다");
    return NULL;
}
NodeList jb_parse(Tok *toks, int ntok, const char *file, int isLib) {
    PS st = {toks, ntok, 0, file, isLib, 0}; PS *prev = S; S = &st;
    NodeList ast = {0};
    while (T()->t != T_EOF) { nl_push(&ast, top()); st.nast++; }
    S = prev;
    return ast;
}
