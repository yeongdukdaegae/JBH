// Layers — 그리기가 놓이는 곳 (자체 · 오브젝트 레이어 · UI) 시험: 웹과 EXE 의 화면이 같아야 함
#include "jdh.h"

JDH_COMMAND(paint, void) {
    jdh_draw_clear();
    jdh_draw_layer(JDH_LAYER_UI, 0);        // 화면에 고정 (카메라를 따르지 않음)
    jdh_draw_rect(-200, 150, 40, 40, "#ffff00");
    jdh_draw_layer(JDH_LAYER_OBJECT, 2);    // 레이어 0 (회색) 위 · 레이어 5 (흰색) 아래
    jdh_draw_rect(0, 0, 150, 150, "#ff0000");
    jdh_draw_layer(JDH_LAYER_OBJECT, -1);   // 모든 오브젝트 아래 — 나중에 그렸어도 번호가 낮으면 아래
    jdh_draw_rect(0, 0, 300, 300, "#0000ff");
    jdh_draw_layer(JDH_LAYER_OWN, 0);       // 오브젝트 위
    jdh_draw_rect(30, 30, 10, 10, "#00ff00");
}
JDH_COMMAND(bad, void) { jdh_draw_layer(7, 0); }
