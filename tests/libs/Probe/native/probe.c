// 시험용: 엔진이 주는 기능 (그리기 · 오브젝트 · 파일) 을 모두 씀
#include "jdh.h"
#include <stdio.h>
#include <string.h>
static int exists, fsize = -2, frames; static double px, py, sw, vx; static char color[16], text[128], fhead[64];
static char fsrc[64], fdst[64];
JDH_COMMAND(paint, void) {
    jdh_draw_clear();
    jdh_draw_rect(-200, 0, 120, 80, "#ff0000");
    jdh_draw_circle(0, 0, 100, 100, "#00ff00");
    jdh_draw_line(150, -50, 250, 50, 10, "#0000ff");
    jdh_draw_text(0, 200, 40, "hi", "#ffffff");
}
JDH_COMMAND(unpaint, void) { jdh_draw_clear(); }
JDH_COMMAND(look, const char *id) {
    exists = jdh_obj_exists(id);
    double x = 0, y = 0; px = py = sw = vx = 0;
    if (jdh_obj_pos(id, &x, &y)) { px = x; py = y; }
    if (jdh_obj_scale(id, &x, &y)) sw = x;
    if (jdh_obj_vel(id, &x, &y)) vx = x;
    if (jdh_obj_color(id, color, sizeof color) < 0) strcpy(color, "-");
    if (jdh_obj_text(id, text, sizeof text) < 0) strcpy(text, "-");
}
JDH_COMMAND(move, const char *id, double x, double y) { jdh_obj_set_pos(id, x, y); }
JDH_COMMAND(grow, const char *id, double w, double h) { jdh_obj_set_scale(id, w, h); }
JDH_COMMAND(push, const char *id, double x, double y) { jdh_obj_set_vel(id, x, y); }
JDH_COMMAND(recolor, const char *id, const char *c) { jdh_obj_set_color(id, c); }
JDH_COMMAND(retext, const char *id, const char *t) { jdh_obj_set_text(id, t); }
JDH_COMMAND(readf, const char *path) {
    fsize = jdh_file_size(path);
    int n = jdh_file_read(path, fhead, sizeof fhead - 1);
    fhead[n > 0 ? n : 0] = 0;
    for (char *p = fhead; *p; p++) if (*p == '\n' || *p == '\r') *p = '/';
}
// 프레임마다: fsrc 의 위치로 fdst 를 옮김 (웹에서는 한 박자 늦게)
JDH_COMMAND(follow, const char *src, const char *dst) { snprintf(fsrc, sizeof fsrc, "%s", src); snprintf(fdst, sizeof fdst, "%s", dst); }
JDH_FRAME(dt) {
    (void)dt; frames++;
    double x, y;
    if (*fsrc && jdh_obj_pos(fsrc, &x, &y) && jdh_obj_exists(fdst)) jdh_obj_set_pos(fdst, x + 100, y);
}
JDH_VALUE(int, exists) { return exists; }
JDH_VALUE(double, px) { return px; }
JDH_VALUE(double, py) { return py; }
JDH_VALUE(double, sw) { return sw; }
JDH_VALUE(double, vx) { return vx; }
JDH_VALUE(const char *, color) { return color; }
JDH_VALUE(const char *, text) { return text; }
JDH_VALUE(int, fsize) { return fsize; }
JDH_VALUE(const char *, fhead) { return fhead; }
JDH_VALUE(int, frames) { return frames; }
