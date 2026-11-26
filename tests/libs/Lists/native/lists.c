// Lists — 리스트 · 행렬 인자 시험 (웹과 EXE 가 같은 줄을 내야 함)
#include "jdh.h"
#include <stdio.h>
#include <string.h>

static char out[4096];
static int n;
static double total;

static void put(const char *fmt, double x) { size_t L = strlen(out); snprintf(out + L, sizeof out - L, fmt, x); }

JDH_COMMAND(isum, const int *items, int cnt) {
    long s = 0; for (int i = 0; i < cnt; i++) s += items[i];
    n = cnt; total = (double)s; snprintf(out, sizeof out, "isum n=%d sum=%ld", cnt, s);
}
JDH_COMMAND(fsum, const double *items, int cnt) {
    double s = 0; for (int i = 0; i < cnt; i++) s += items[i];
    n = cnt; total = s; snprintf(out, sizeof out, "fsum n=%d sum=%g", cnt, s);
}
JDH_COMMAND(join, const char *const *items, int cnt) {
    n = cnt; snprintf(out, sizeof out, "join n=%d:", cnt);
    for (int i = 0; i < cnt; i++) { size_t L = strlen(out); snprintf(out + L, sizeof out - L, "%s%s", i ? "|" : "", items[i]); }
}
JDH_COMMAND(bits, const int *items, int cnt) {
    n = cnt; snprintf(out, sizeof out, "bits ");
    for (int i = 0; i < cnt; i++) put("%.0f", (double)items[i]);
}
JDH_COMMAND(pair, int a, const char *b) { snprintf(out, sizeof out, "pair %d %s", a, b); }
JDH_COMMAND(mix, const char *tag, double x, double y, int k, int z) { snprintf(out, sizeof out, "mix %s %g %g %d %d", tag, x, y, k, z); }
JDH_COMMAND(mat, const double *m) {
    snprintf(out, sizeof out, "mat");
    for (int i = 0; i < 6; i++) put(" %g", m[i]);
}
JDH_COMMAND(anymat, const double *m, int rows, int cols) {
    double s = 0; for (int i = 0; i < rows * cols; i++) s += m[i];
    total = s; snprintf(out, sizeof out, "anymat %dx%d sum=%g last=%g", rows, cols, s, rows * cols ? m[rows * cols - 1] : 0.0);
}
JDH_COMMAND(rowmat, const double *m, int rows) {
    snprintf(out, sizeof out, "rowmat rows=%d", rows);
    for (int r = 0; r < rows; r++) { put(" (%g", m[r * 2]); put(",%g)", m[r * 2 + 1]); }
}
JDH_VALUE(const char *, out) { return out; }
JDH_VALUE(int, n) { return n; }
JDH_VALUE(double, total) { return total; }
