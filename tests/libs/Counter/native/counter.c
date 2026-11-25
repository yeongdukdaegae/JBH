#include "jdh.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
static int total, frames; static char name[128]; static double px;
JDH_COMMAND(add, int n) { total += n; }
JDH_COMMAND(reset, void) { total = 0; }
JDH_COMMAND(say, const char *s) { snprintf(name, sizeof name, "%s", s); char b[200]; snprintf(b, sizeof b, "say %s", s); jdh_log(b); }
JDH_COMMAND(at, double x, double y, const char *color) { px = x + y; char b[200]; snprintf(b, sizeof b, "at %g %g %s sqrt=%g", x, y, color, sqrt(x * x + y * y)); jdh_log(b); }
JDH_COMMAND(probe, void) {
    char b[300]; FILE *f = fopen("/etc/passwd", "r"); FILE *w = fopen("out.txt", "w");
    char *m = malloc(1 << 20); if (m) memset(m, 1, 1 << 20);
    snprintf(b, sizeof b, "probe read=%s write=%s malloc=%s env=%s", f ? "OPENED" : "blocked", w ? "OPENED" : "blocked", m ? "ok" : "fail", getenv("PATH") ? "SEEN" : "none");
    jdh_log(b); free(m);
}
JDH_COMMAND(boom, void) { abort(); }
JDH_COMMAND(hang, void) { for (volatile int i = 0;; i++) { } }
JDH_VALUE(int, total) { return total; }
JDH_VALUE(const char *, name) { return name; }
JDH_VALUE(double, px) { return px; }
JDH_VALUE(int, frames) { return frames; }
JDH_FRAME(dt) { (void)dt; frames++; }
