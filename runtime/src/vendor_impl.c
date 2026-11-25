/* 바깥 라이브러리 (한 파일 헤더) — 이미지 · 글꼴 · PNG 저장 · 소리 디코더 */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#include "stb_image.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#define DR_MP3_IMPLEMENTATION
#include "dr_mp3.h"
#undef L
#undef R
#undef C
#include "stb_vorbis.c"
