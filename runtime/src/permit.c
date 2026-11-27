/* 권한 (jaw.getPermit) 의 장치 쪽 — 카메라 · 마이크 · 파일 고르기 (편집기의 execProgram 안 PERMIT 와 같은 뜻)
 *   카메라: Windows 는 Media Foundation (첫 번째 카메라 · RGB32 로 받아 RGBA 로) · 그 밖은 없음 (허락되지 않음)
 *   마이크: SDL2 녹음 장치 — 마지막 소리 조각 (1024개 · 4바이트 실수)
 *   파일: Windows 는 열기 창 (모든 파일) · 그 밖은 터미널에서 경로 · 환경 변수 JB_PERMIT_FILE 이 있으면 그 파일 (시험용)
 *   windows.h 의 이름이 실행기의 이름과 겹쳐서 실행기 헤더 없이 따로 둔 파일
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <SDL.h>
#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <commdlg.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#else
#include <unistd.h>
#endif

/* ---------- 카메라 ---------- */
static pthread_mutex_t camMu = PTHREAD_MUTEX_INITIALIZER;
static unsigned char *camPx;     /* 마지막 프레임 (RGBA) */
static int camW, camH;
static unsigned camSeq;
static volatile int camState;    /* 0 = 꺼짐 · 1 = 켜는 중 · 2 = 켜짐 · -1 = 실패 */
static volatile int camStop;

#ifdef _WIN32
/* IID_IMFMediaSource — mfuuid 가 없는 툴체인에서도 링크되게 직접 둠 */
static const GUID JB_IID_IMFMediaSource = {0x279a808d, 0xaec7, 0x40c8, {0x9c, 0x6b, 0xa6, 0xb4, 0x92, 0xc7, 0x8a, 0x66}};
static void camFrame(const BYTE *p, LONG stride, int w, int h) {
    size_t need = (size_t)w * h * 4;
    pthread_mutex_lock(&camMu);
    if (camW != w || camH != h || !camPx) { free(camPx); camPx = malloc(need); camW = w; camH = h; }
    if (camPx) {
        LONG as = stride < 0 ? -stride : stride;
        for (int y = 0; y < h; y++) {
            const BYTE *s = stride < 0 ? p + (size_t)(h - 1 - y) * as : p + (size_t)y * as;   /* 음수 = 아래에서 위로 */
            unsigned char *d = camPx + (size_t)y * w * 4;
            for (int x = 0; x < w; x++) { d[x * 4] = s[x * 4 + 2]; d[x * 4 + 1] = s[x * 4 + 1]; d[x * 4 + 2] = s[x * 4]; d[x * 4 + 3] = 255; }   /* BGRX → RGBA */
        }
        camSeq++;
    }
    pthread_mutex_unlock(&camMu);
}
static void *camThread(void *arg) {
    (void)arg;
    IMFAttributes *attr = NULL, *ra = NULL; IMFActivate **devs = NULL; UINT32 n = 0;
    IMFMediaSource *src = NULL; IMFSourceReader *rd = NULL; IMFMediaType *mt = NULL, *cur = NULL;
    int ok = 0, w = 0, h = 0; LONG stride = 0;
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) goto fail;
    if (FAILED(MFCreateAttributes(&attr, 1))) goto fail;
    IMFAttributes_SetGUID(attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(MFEnumDeviceSources(attr, &devs, &n)) || !n) goto fail;
    if (FAILED(IMFActivate_ActivateObject(devs[0], &JB_IID_IMFMediaSource, (void **)&src))) goto fail;   /* 개인 정보 설정에서 막혀 있으면 여기서 실패 */
    if (FAILED(MFCreateAttributes(&ra, 1))) goto fail;
    IMFAttributes_SetUINT32(ra, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);   /* 카메라의 형식 → RGB32 로 바꿔 줌 */
    if (FAILED(MFCreateSourceReaderFromMediaSource(src, ra, &rd))) goto fail;
    if (FAILED(MFCreateMediaType(&mt))) goto fail;
    IMFMediaType_SetGUID(mt, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    IMFMediaType_SetGUID(mt, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
    if (FAILED(IMFSourceReader_SetCurrentMediaType(rd, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, NULL, mt))) goto fail;
    if (FAILED(IMFSourceReader_GetCurrentMediaType(rd, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur))) goto fail;
    { UINT64 fs = 0; if (FAILED(IMFMediaType_GetUINT64(cur, &MF_MT_FRAME_SIZE, &fs))) goto fail; w = (int)(fs >> 32); h = (int)(UINT32)fs; }
    { UINT32 st = 0; if (SUCCEEDED(IMFMediaType_GetUINT32(cur, &MF_MT_DEFAULT_STRIDE, &st))) stride = (LONG)(INT32)st; else stride = w * 4; }
    if (w <= 0 || h <= 0 || w > 8192 || h > 8192) goto fail;
    ok = 1; camState = 2;
    while (!camStop) {
        DWORD idx = 0, flags = 0; LONGLONG ts = 0; IMFSample *smp = NULL;
        if (FAILED(IMFSourceReader_ReadSample(rd, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &idx, &flags, &ts, &smp))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) { if (smp) IMFSample_Release(smp); break; }
        if (!smp) continue;
        IMFMediaBuffer *buf = NULL;
        if (SUCCEEDED(IMFSample_ConvertToContiguousBuffer(smp, &buf))) {
            BYTE *p = NULL; DWORD len = 0;
            if (SUCCEEDED(IMFMediaBuffer_Lock(buf, &p, NULL, &len))) {
                LONG as = stride < 0 ? -stride : stride;
                if ((size_t)len >= (size_t)as * h && as >= w * 4) camFrame(p, stride, w, h);
                IMFMediaBuffer_Unlock(buf);
            }
            IMFMediaBuffer_Release(buf);
        }
        IMFSample_Release(smp);
    }
fail:
    if (cur) IMFMediaType_Release(cur);
    if (mt) IMFMediaType_Release(mt);
    if (rd) IMFSourceReader_Release(rd);
    if (src) { IMFMediaSource_Shutdown(src); IMFMediaSource_Release(src); }
    if (ra) IMFAttributes_Release(ra);
    if (devs) { for (UINT32 i = 0; i < n; i++) IMFActivate_Release(devs[i]); CoTaskMemFree(devs); }
    if (attr) IMFAttributes_Release(attr);
    MFShutdown();
    CoUninitialize();
    camState = ok ? 0 : -1;
    return NULL;
}
#endif

/* 켬 — 첫 프레임 형식을 알 때까지 기다림 (5초) · 1 = 켜짐 */
int jb_permit_cam_start(void) {
#ifdef _WIN32
    if (camState == 2) return 1;
    camStop = 0; camState = 1;
    pthread_t t; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &at, camThread, NULL)) { pthread_attr_destroy(&at); camState = -1; return 0; }
    pthread_attr_destroy(&at);
    for (int i = 0; i < 500 && camState == 1; i++) SDL_Delay(10);
    return camState == 2;
#else
    return 0;
#endif
}
void jb_permit_cam_stop(void) { camStop = 1; }
/* 새 프레임이 있으면 *buf 에 복사 (크기가 바뀌면 다시 잡음) — 1 = 새 것 */
int jb_permit_cam_take(unsigned char **buf, int *w, int *h, unsigned *seq) {
    int got = 0;
    pthread_mutex_lock(&camMu);
    if (camPx && camSeq != *seq) {
        if (*w != camW || *h != camH || !*buf) { free(*buf); *buf = malloc((size_t)camW * camH * 4); }
        if (*buf) { memcpy(*buf, camPx, (size_t)camW * camH * 4); *w = camW; *h = camH; *seq = camSeq; got = 1; }
    }
    pthread_mutex_unlock(&camMu);
    return got;
}
void jb_permit_cam_size(int *w, int *h) { pthread_mutex_lock(&camMu); *w = camW; *h = camH; pthread_mutex_unlock(&camMu); }
/* 지금 프레임의 바이트 — 너비 · 높이 (4바이트 big-endian) · RGBA (편집기의 camSnap 과 같은 꼴 · 꺼져 있으면 0 · 0) */
unsigned char *jb_permit_cam_copy(size_t *n) {
    pthread_mutex_lock(&camMu);
    int w = camPx ? camW : 0, h = camPx ? camH : 0; size_t L = 8 + (size_t)w * h * 4;
    unsigned char *o = malloc(L);
    if (o) {
        for (int i = 0; i < 4; i++) { o[i] = (unsigned char)((unsigned)w >> (24 - 8 * i)); o[4 + i] = (unsigned char)((unsigned)h >> (24 - 8 * i)); }
        if (w && h) memcpy(o + 8, camPx, (size_t)w * h * 4);
    }
    pthread_mutex_unlock(&camMu);
    *n = o ? L : 0; return o;
}

/* ---------- 마이크 ---------- */
static SDL_AudioDeviceID micDev;
#define MIC_N 1024
static float micBuf[MIC_N];   /* 마지막 소리 조각 (오래된 것부터) */
static void SDLCALL micCb(void *ud, Uint8 *stream, int len) {
    (void)ud; const float *s = (const float *)stream; int n = len / (int)sizeof(float);
    if (n >= MIC_N) memcpy(micBuf, s + n - MIC_N, sizeof micBuf);
    else { memmove(micBuf, micBuf + n, sizeof(float) * (MIC_N - n)); memcpy(micBuf + MIC_N - n, s, sizeof(float) * n); }
}
int jb_permit_mic_start(void) {
    if (micDev) return 1;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return 0;
    SDL_AudioSpec want, have; memset(&want, 0, sizeof want);
    want.freq = 44100; want.format = AUDIO_F32SYS; want.channels = 1; want.samples = 1024; want.callback = micCb;
    memset(micBuf, 0, sizeof micBuf);
    micDev = SDL_OpenAudioDevice(NULL, 1, &want, &have, 0);
    if (!micDev) { SDL_QuitSubSystem(SDL_INIT_AUDIO); return 0; }
    SDL_PauseAudioDevice(micDev, 0);
    return 1;
}
/* 지금 소리 조각의 바이트 — 4바이트 실수 big-endian (편집기의 micSnap 과 같은 꼴 · 꺼져 있으면 0바이트) */
unsigned char *jb_permit_mic_copy(size_t *n) {
    if (!micDev) { *n = 0; return malloc(1); }
    float t[MIC_N]; SDL_LockAudioDevice(micDev); memcpy(t, micBuf, sizeof t); SDL_UnlockAudioDevice(micDev);
    unsigned char *o = malloc(MIC_N * 4);
    if (!o) { *n = 0; return NULL; }
    for (int i = 0; i < MIC_N; i++) { Uint32 u; memcpy(&u, &t[i], 4); for (int q = 0; q < 4; q++) o[i * 4 + q] = (unsigned char)(u >> (24 - 8 * q)); }
    *n = MIC_N * 4; return o;
}
void jb_permit_mic_stop(void) { if (micDev) { SDL_CloseAudioDevice(micDev); micDev = 0; SDL_QuitSubSystem(SDL_INIT_AUDIO); } }

/* ---------- 파일 고르기 ---------- */
int jb_permit_file_pick(char *out, size_t n) {
    const char *e = getenv("JB_PERMIT_FILE");
    out[0] = 0;
    if (e) { snprintf(out, n, "%s", e); return *out != 0; }
#ifdef _WIN32
    wchar_t buf[4096] = L"";
    OPENFILENAMEW o; memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o; o.hwndOwner = GetActiveWindow();
    o.lpstrFile = buf; o.nMaxFile = 4096;
    o.lpstrFilter = L"*.*\0*.*\0\0";
    o.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (!GetOpenFileNameW(&o)) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, (int)n, NULL, NULL) > 0;
#else
    if (!isatty(0)) return 0;
    fprintf(stderr, "🔐 jaw.getPermit(\"file\") — 고칠 파일 경로 (빈 줄 = 취소): "); fflush(stderr);
    if (!fgets(out, (int)n, stdin)) { out[0] = 0; return 0; }
    size_t L = strlen(out); while (L && (out[L - 1] == '\n' || out[L - 1] == '\r')) out[--L] = 0;
    return L > 0;
#endif
}
