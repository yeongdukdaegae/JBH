// 시험용: objects.read 권한 없이 오브젝트를 읽으려 함 → jdh lib check 가 거부해야 함
#include "jdh.h"
static double x;
JDH_COMMAND(peek, const char *id) { double y; jdh_obj_pos(id, &x, &y); }
JDH_VALUE(double, x) { return x; }
