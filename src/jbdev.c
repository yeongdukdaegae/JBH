/* jbdev — JawBreaker Dev. (Windows 네이티브 에디터 · C / Win32 · HTML 을 쓰지 않음)
 *   화면: 위 = 단추 줄 · 왼쪽 = 프로젝트 탐색기 (폴더 · 파일) · 가운데 = 코드 칸 (색칠) · 아래 = 로그 칸
 *   프로젝트 = 디스크의 폴더 (project.jbs 가 있는 곳) — 파일을 바로 열고 저장함 (브라우저 저장 공간 없음)
 *   ▶ 실행 (F5) = 저장한 뒤 그 프로젝트의 run.bat (없으면 jdh run) — 출력 (print · 오류) 은 로그 칸에 · ■ 정지 = 그 프로그램을 끔 (자식까지)
 *   EXE 만들기 = jdh build exe · 새 프로젝트 = jdh new project
 *   화면 글자는 5개 언어 (ko · en · ja · zh-CN · zh-TW · Windows 표시 언어를 따름)
 *   열기 전에 새 버전이 있는지 확인 (jdh update --check · 5초 한도) → 있으면 "업데이트하시겠습니까?" 를 물음.
 *     예 = jdh update --yes · 아니요 = 그 버전은 다시 묻지 않음 · 취소 = 다음에 다시 물음 · JDH_NO_UPDATE=1 이면 확인하지 않음.
 *   jbdev.exe [프로젝트 폴더] — 그 프로젝트를 엶 (없으면 지난번 프로젝트)
 *   jbdev.exe --selftest <프로젝트> <결과 이름> — 시험용: 열고 · 실행하고 · 끝나면 로그 (<결과>.log) 와 창 그림 (<결과>.bmp) 을 남기고 닫힘
 */
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <richedit.h>
#include <wchar.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int fileExists(const wchar_t *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static int dirExists(const wchar_t *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static void fail(const wchar_t *msg) { MessageBoxW(NULL, msg, L"JawBreaker Dev.", MB_OK | MB_ICONERROR); }

/* bin\jdh.exe 를 실행 — show = 0: 창 없이 (출력을 out 에) · 1: 새 명령 창. 끝나면 1 (종료 코드는 code 에), 못 띄웠거나 시간이 넘으면 0 */
static int runJdh(const wchar_t *dir, const wchar_t *args, int show, DWORD waitMs, char *out, DWORD outn, DWORD *code) {
    wchar_t exe[MAX_PATH]; static wchar_t cmd[MAX_PATH + 128];
    _snwprintf(exe, MAX_PATH, L"%ls\\jdh.exe", dir); exe[MAX_PATH - 1] = 0;
    if (!fileExists(exe)) return 0;
    _snwprintf(cmd, MAX_PATH + 128, L"\"%ls\" %ls", exe, args); cmd[MAX_PATH + 127] = 0;
    SECURITY_ATTRIBUTES sa; sa.nLength = sizeof sa; sa.lpSecurityDescriptor = NULL; sa.bInheritHandle = TRUE;
    HANDLE rd = NULL, wr = NULL, nul = NULL;
    STARTUPINFOW si; PROCESS_INFORMATION pi; ZeroMemory(&si, sizeof si); si.cb = sizeof si; ZeroMemory(&pi, sizeof pi);
    if (!show) {
        if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
        SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
        nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
        si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = nul; si.hStdInput = nul;
    }
    BOOL ok = CreateProcessW(exe, cmd, NULL, NULL, show ? FALSE : TRUE, show ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    if (wr) CloseHandle(wr);
    if (nul && nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { if (rd) CloseHandle(rd); return 0; }
    int done = WaitForSingleObject(pi.hProcess, waitMs) == WAIT_OBJECT_0;
    if (!done) TerminateProcess(pi.hProcess, 1);
    else if (code) GetExitCodeProcess(pi.hProcess, code);
    if (rd) {
        DWORD got = 0, avail = 0;
        if (out && outn) { out[0] = 0; if (done && PeekNamedPipe(rd, NULL, 0, NULL, &avail, NULL) && avail && ReadFile(rd, out, outn - 1 < avail ? outn - 1 : avail, &got, NULL)) out[got] = 0; }
        CloseHandle(rd);
    }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return done;
}
/* 새 버전이 있으면 묻고 업데이트 — 업데이트했으면 1 (새 jbdev.exe 로 다시 시작해야 함) */
static int checkUpdate(const wchar_t *dir) {
    wchar_t no[8]; DWORD nn = GetEnvironmentVariableW(L"JDH_NO_UPDATE", no, 8);
    if (nn && nn < 8 && wcscmp(no, L"0")) return 0;
    char out[64]; DWORD code = 0;
    if (!runJdh(dir, L"update --check", 0, 5000, out, sizeof out, &code) || code != 10) return 0;
    char ver[24]; int n = 0;
    for (; n < 23 && ((out[n] >= '0' && out[n] <= '9') || out[n] == '.'); n++) ver[n] = out[n];
    ver[n] = 0; if (!n) return 0;
    /* "아니요" 로 건너뛴 버전: %APPDATA%\JDH\update-skip.txt */
    wchar_t app[MAX_PATH], skip[MAX_PATH]; skip[0] = 0;
    DWORD an = GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH);
    if (an && an < MAX_PATH - 32) {
        _snwprintf(skip, MAX_PATH, L"%ls\\JDH", app); skip[MAX_PATH - 1] = 0; CreateDirectoryW(skip, NULL);
        _snwprintf(skip, MAX_PATH, L"%ls\\JDH\\update-skip.txt", app); skip[MAX_PATH - 1] = 0;
        HANDLE f = CreateFileW(skip, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (f != INVALID_HANDLE_VALUE) { char old[24]; DWORD got = 0; ReadFile(f, old, 23, &got, NULL); old[got] = 0; CloseHandle(f); if (!strcmp(old, ver)) return 0; }
    }
    wchar_t wver[24], msg[512]; for (int i = 0; i <= n; i++) wver[i] = (wchar_t)ver[i];
    _snwprintf(msg, 512, L"새 버전 %ls 이(가) 나왔습니다. 업데이트하시겠습니까?\n\n예: 지금 업데이트 — 바뀐 파일만 받아 검사한 뒤 바꿉니다\n아니요: 이 버전은 다시 묻지 않음\n취소: 다음에 다시 물음", wver); msg[511] = 0;
    int r = MessageBoxW(NULL, msg, L"JawBreaker Dev. 업데이트", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDNO && skip[0]) {
        HANDLE f = CreateFileW(skip, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, ver, (DWORD)n, &w, NULL); CloseHandle(f); }
    }
    if (r != IDYES) return 0;
    code = 1;
    if (!runJdh(dir, L"update --yes --pause", 1, INFINITE, NULL, 0, &code) || code) { MessageBoxW(NULL, L"업데이트하지 못했습니다. 지금 버전으로 엽니다.", L"JawBreaker Dev.", MB_OK | MB_ICONWARNING); return 0; }
    return 1;
}


/* ================= 화면 글자 (5개 언어) ================= */
enum { L_KO, L_EN, L_JA, L_ZHCN, L_ZHTW };
static int LANG = L_EN;
enum { S_NEWP, S_OPEN, S_SAVE, S_RUN, S_STOP, S_EXE, S_NOPROJ, S_NOTPROJ, S_ASKSAVE, S_BINARY, S_RUNNING, S_DONE, S_FAILRUN, S_PICKDIR,
       S_NEWNAME, S_SAVED, S_STOPPED, S_BUSY, S_NOJDH, S_EXISTS, S_TITLE_NONE, S_N };
static const wchar_t *TR[S_N][5] = {
    {L"새 프로젝트", L"New project", L"新規プロジェクト", L"新建项目", L"新建專案"},
    {L"폴더 열기", L"Open folder", L"フォルダを開く", L"打开文件夹", L"開啟資料夾"},
    {L"저장", L"Save", L"保存", L"保存", L"儲存"},
    {L"▶ 실행", L"▶ Run", L"▶ 実行", L"▶ 运行", L"▶ 執行"},
    {L"■ 정지", L"■ Stop", L"■ 停止", L"■ 停止", L"■ 停止"},
    {L"EXE 만들기", L"Build EXE", L"EXE を作成", L"生成 EXE", L"產生 EXE"},
    {L"프로젝트를 열거나 새로 만드세요.", L"Open a project or create a new one.", L"プロジェクトを開くか新しく作成してください。", L"请打开或新建一个项目。", L"請開啟或新建一個專案。"},
    {L"이 폴더에는 project.jbs 가 없습니다 — Jawbreaker 프로젝트 폴더를 고르세요.", L"This folder has no project.jbs — choose a Jawbreaker project folder.", L"このフォルダーには project.jbs がありません — Jawbreaker のプロジェクトフォルダーを選んでください。", L"此文件夹中没有 project.jbs — 请选择 Jawbreaker 项目文件夹。", L"此資料夾中沒有 project.jbs — 請選擇 Jawbreaker 專案資料夾。"},
    {L"'%ls' 을(를) 저장할까요?", L"Save '%ls'?", L"'%ls' を保存しますか?", L"要保存 '%ls' 吗?", L"要儲存 '%ls' 嗎?"},
    {L"글자 파일이 아니라 여기서는 열 수 없습니다: %ls", L"Not a text file, so it cannot be opened here: %ls", L"テキストファイルではないので、ここでは開けません: %ls", L"不是文本文件，无法在此打开: %ls", L"不是文字檔案，無法在此開啟: %ls"},
    {L"▶ 실행: %ls", L"▶ Run: %ls", L"▶ 実行: %ls", L"▶ 运行: %ls", L"▶ 執行: %ls"},
    {L"■ 끝남 (종료 코드 %lu · %.1f초)", L"■ Finished (exit code %lu · %.1fs)", L"■ 終了 (終了コード %lu · %.1f 秒)", L"■ 结束 (退出代码 %lu · %.1f 秒)", L"■ 結束 (結束代碼 %lu · %.1f 秒)"},
    {L"실행하지 못했습니다: %ls", L"Could not start: %ls", L"起動できませんでした: %ls", L"无法启动: %ls", L"無法啟動: %ls"},
    {L"Jawbreaker 프로젝트 폴더를 고르세요 (project.jbs 가 있는 곳)", L"Choose a Jawbreaker project folder (where project.jbs is)", L"Jawbreaker のプロジェクトフォルダーを選んでください (project.jbs がある場所)", L"请选择 Jawbreaker 项目文件夹 (project.jbs 所在处)", L"請選擇 Jawbreaker 專案資料夾 (project.jbs 所在處)"},
    {L"새 프로젝트 폴더 (이름을 적으세요)", L"New project folder (type a name)", L"新しいプロジェクトフォルダー (名前を入力)", L"新项目文件夹 (输入名称)", L"新專案資料夾 (輸入名稱)"},
    {L"저장했습니다: %ls", L"Saved: %ls", L"保存しました: %ls", L"已保存: %ls", L"已儲存: %ls"},
    {L"■ 정지했습니다", L"■ Stopped", L"■ 停止しました", L"■ 已停止", L"■ 已停止"},
    {L"이미 실행 중입니다 — 먼저 ■ 정지하세요.", L"Already running — press ■ Stop first.", L"すでに実行中です — 先に ■ 停止してください。", L"正在运行 — 请先按 ■ 停止。", L"正在執行 — 請先按 ■ 停止。"},
    {L"jdh.exe 를 찾을 수 없습니다 — JDH 를 다시 설치해 주세요.", L"jdh.exe not found — please reinstall JDH.", L"jdh.exe が見つかりません — JDH を再インストールしてください。", L"找不到 jdh.exe — 请重新安装 JDH。", L"找不到 jdh.exe — 請重新安裝 JDH。"},
    {L"이미 있는 이름입니다: %ls", L"That name already exists: %ls", L"すでにある名前です: %ls", L"该名称已存在: %ls", L"該名稱已存在: %ls"},
    {L"(프로젝트 없음)", L"(no project)", L"(プロジェクトなし)", L"(无项目)", L"(無專案)"},
};
#define T(k) TR[k][LANG]
static void pickLang(void) {
    LANGID id = GetUserDefaultUILanguage(); WORD pri = PRIMARYLANGID(id), sub = SUBLANGID(id);
    LANG = pri == LANG_KOREAN ? L_KO : pri == LANG_JAPANESE ? L_JA : pri == LANG_CHINESE ? (sub == SUBLANG_CHINESE_SIMPLIFIED || sub == SUBLANG_CHINESE_SINGAPORE ? L_ZHCN : L_ZHTW) : L_EN;
    wchar_t e[16]; DWORD n = GetEnvironmentVariableW(L"JDH_LANG", e, 16);   /* 시험용: ko · en · ja · zh-CN · zh-TW */
    if (n && n < 16) { if (!wcscmp(e, L"ko")) LANG = L_KO; else if (!wcscmp(e, L"en")) LANG = L_EN; else if (!wcscmp(e, L"ja")) LANG = L_JA; else if (!wcscmp(e, L"zh-CN")) LANG = L_ZHCN; else if (!wcscmp(e, L"zh-TW")) LANG = L_ZHTW; }
}

/* ================= 상태 ================= */
enum { ID_NEWP = 101, ID_OPEN, ID_SAVE, ID_RUN, ID_STOP, ID_EXE, ID_TREE, ID_EDIT, ID_LOG, ID_STATUS };
#define WM_APP_OUT (WM_APP + 1)   /* 자식의 출력 (lParam = wchar_t* · 받은 쪽이 free) */
#define WM_APP_END (WM_APP + 2)   /* 자식이 끝남 (wParam = 종료 코드) */
#define TIMER_COLOR 1
static HWND hMain, hTree, hEdit, hLog, hStatus, hBtn[6];
static HFONT hFontUI, hFontCode;
static int DPI = 96;
#define SC(x) MulDiv((x), DPI, 96)   /* 화면 배율 (125% · 150% …) 에 맞춘 크기 */
static wchar_t BINDIR[MAX_PATH];          /* jbdev.exe · jdh.exe 가 있는 곳 */
static wchar_t PROJ[MAX_PATH];            /* 연 프로젝트 폴더 ("" = 없음) */
static wchar_t CUR[MAX_PATH];             /* 코드 칸의 파일 ("" = 없음) */
static int DIRTY, COLORING;
static wchar_t **TPATHS; static int NTP;  /* 탐색기 항목 → 경로 */
static HANDLE childProc, childJob;
static DWORD childStart;
static int SELFTEST; static wchar_t SELFOUT[MAX_PATH];

static void status(const wchar_t *fmt, ...) { wchar_t b[1024]; va_list ap; va_start(ap, fmt); _vsnwprintf(b, 1024, fmt, ap); va_end(ap); b[1023] = 0; SetWindowTextW(hStatus, b); }
static void logAppend(const wchar_t *t) {
    int n = GetWindowTextLengthW(hLog);
    if (n > 400000) { SendMessageW(hLog, EM_SETSEL, 0, n / 2); SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)L""); n = GetWindowTextLengthW(hLog); }
    SendMessageW(hLog, EM_SETSEL, n, n); SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)t); SendMessageW(hLog, WM_VSCROLL, SB_BOTTOM, 0);
}
static void logLine(const wchar_t *fmt, ...) { wchar_t b[2048]; va_list ap; va_start(ap, fmt); _vsnwprintf(b, 2046, fmt, ap); va_end(ap); b[2045] = 0; wcscat(b, L"\r\n"); logAppend(b); }
static const wchar_t *baseName(const wchar_t *p) { const wchar_t *s = wcsrchr(p, L'\\'); return s ? s + 1 : p; }

/* ================= 파일 (UTF-8 ↔ 코드 칸) ================= */
static char *readFileA(const wchar_t *p, DWORD *len) {
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return NULL;
    DWORD n = GetFileSize(f, NULL), got = 0; char *b = malloc(n + 1);
    if (!b || !ReadFile(f, b, n, &got, NULL)) { CloseHandle(f); free(b); return NULL; }
    CloseHandle(f); b[got] = 0; if (len) *len = got; return b;
}
static int writeFileA(const wchar_t *p, const char *b, DWORD n) {
    HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    DWORD w = 0; int ok = WriteFile(f, b, n, &w, NULL) && w == n; CloseHandle(f); return ok;
}
static int isTextName(const wchar_t *p) {
    const wchar_t *x = wcsrchr(p, L'.'); if (!x) return 0;
    static const wchar_t *E[] = {L".jb", L".jbl", L".jbs", L".json", L".txt", L".md", L".bat", L".tl", L".csv", NULL};
    for (int i = 0; E[i]; i++) if (!_wcsicmp(x, E[i])) return 1;
    return 0;
}
/* 코드 칸 글자 → UTF-8 (줄 끝 \n) */
static char *editTextUtf8(DWORD *n) {
    int L = GetWindowTextLengthW(hEdit); wchar_t *w = malloc((L + 1) * sizeof(wchar_t)); GetWindowTextW(hEdit, w, L + 1);
    int o = 0; for (int i = 0; w[i]; i++) { if (w[i] == L'\r') { w[o++] = L'\n'; if (w[i + 1] == L'\n') i++; } else w[o++] = w[i]; } w[o] = 0;   /* \r\n · \r → \n */
    int m = WideCharToMultiByte(CP_UTF8, 0, w, o, NULL, 0, NULL, NULL); char *b = malloc(m + 1);
    WideCharToMultiByte(CP_UTF8, 0, w, o, b, m, NULL, NULL); b[m] = 0; free(w); *n = (DWORD)m; return b;
}

/* ================= 색칠 (주석 · 문자열 · 키워드 · 형식 · 숫자) ================= */
static const wchar_t *KW[] = {L"Use", L"use", L"Lib", L"call", L"var", L"public", L"if", L"else", L"loop", L"while", L"run", L"func", L"true", L"false", L"as", L"obj", L"cam", L"spk", L"to", L"for", L"parent", L"id", NULL};
static const wchar_t *TY[] = {L"int", L"float", L"string", L"bit", L"matrix", L"list", L"compound", L"data", NULL};
enum { C_TEXT, C_KW, C_TYPE, C_STR, C_NUM, C_COM, C_N };
static const COLORREF COL[C_N] = {RGB(30, 30, 30), RGB(0, 70, 200), RGB(0, 128, 128), RGB(170, 60, 0), RGB(120, 60, 170), RGB(110, 140, 100)};
static int inList(const wchar_t *w, int n, const wchar_t **L) { for (int i = 0; L[i]; i++) if ((int)wcslen(L[i]) == n && !wcsncmp(L[i], w, n)) return 1; return 0; }
static void paintRange(int a, int b, int c) {
    CHARFORMAT2W cf; ZeroMemory(&cf, sizeof cf); cf.cbSize = sizeof cf; cf.dwMask = CFM_COLOR | CFM_ITALIC; cf.crTextColor = COL[c]; cf.dwEffects = c == C_COM ? CFE_ITALIC : 0;
    CHARRANGE cr = {a, b}; SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr); SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
}
static void recolor(void) {
    if (!hEdit) return;
    int L = GetWindowTextLengthW(hEdit); if (L > 400000) return;   /* 아주 큰 파일은 색칠하지 않음 */
    wchar_t *w = malloc((L + 1) * sizeof(wchar_t)); GETTEXTEX gt = {(DWORD)((L + 1) * sizeof(wchar_t)), GT_DEFAULT, 1200, NULL, NULL};
    L = (int)SendMessageW(hEdit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)w); w[L] = 0;   /* RichEdit 위치와 같은 글자 (\r 하나가 한 칸) */
    COLORING = 1;
    CHARRANGE sel; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&sel);
    POINT sp; SendMessageW(hEdit, EM_GETSCROLLPOS, 0, (LPARAM)&sp);
    DWORD em = (DWORD)SendMessageW(hEdit, EM_SETEVENTMASK, 0, 0);
    SendMessageW(hEdit, WM_SETREDRAW, FALSE, 0);
    paintRange(0, L, C_TEXT);
    for (int i = 0; i < L;) {
        wchar_t c = w[i];
        if (c == L'/' && i + 1 < L && w[i + 1] == L'/') { int j = i; while (j < L && w[j] != L'\r' && w[j] != L'\n') j++; paintRange(i, j, C_COM); i = j; continue; }
        if (c == L'"') { int j = i + 1; while (j < L && w[j] != L'"' && w[j] != L'\r' && w[j] != L'\n') { if (w[j] == L'\\' && j + 1 < L) j++; j++; } if (j < L && w[j] == L'"') j++; paintRange(i, j, C_STR); i = j; continue; }
        if ((c >= L'0' && c <= L'9') && (i == 0 || !(iswalnum(w[i - 1]) || w[i - 1] == L'_'))) { int j = i; while (j < L && (iswalnum(w[j]) || w[j] == L'.')) j++; paintRange(i, j, C_NUM); i = j; continue; }
        if (iswalpha(c) || c == L'_') {
            int j = i; while (j < L && (iswalnum(w[j]) || w[j] == L'_')) j++;
            if (inList(w + i, j - i, KW)) paintRange(i, j, C_KW); else if (inList(w + i, j - i, TY)) paintRange(i, j, C_TYPE);
            i = j; continue;
        }
        i++;
    }
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&sel);
    SendMessageW(hEdit, EM_SETSCROLLPOS, 0, (LPARAM)&sp);
    SendMessageW(hEdit, WM_SETREDRAW, TRUE, 0); InvalidateRect(hEdit, NULL, TRUE);
    SendMessageW(hEdit, EM_SETEVENTMASK, 0, em);
    COLORING = 0; free(w);
}

/* ================= 열기 · 저장 ================= */
static void setTitle(void) {
    wchar_t t[MAX_PATH * 2];
    if (!PROJ[0]) _snwprintf(t, MAX_PATH * 2, L"JawBreaker Dev. — %ls", T(S_TITLE_NONE));
    else _snwprintf(t, MAX_PATH * 2, L"JawBreaker Dev. — %ls%ls%ls%ls", baseName(PROJ), CUR[0] ? L" · " : L"", CUR[0] ? baseName(CUR) : L"", DIRTY ? L" •" : L"");
    t[MAX_PATH * 2 - 1] = 0; SetWindowTextW(hMain, t);
}
static int saveCur(void) {
    if (!CUR[0] || !DIRTY) return 1;
    DWORD n; char *b = editTextUtf8(&n); int ok = writeFileA(CUR, b, n); free(b);
    if (ok) { DIRTY = 0; setTitle(); status(T(S_SAVED), baseName(CUR)); }
    return ok;
}
/* 바꾸기 전에: 고친 것이 있으면 저장할지 물음 — 0 = 취소 */
static int askSave(void) {
    if (!CUR[0] || !DIRTY) return 1;
    if (SELFTEST) return saveCur();
    wchar_t m[MAX_PATH + 64]; _snwprintf(m, MAX_PATH + 64, T(S_ASKSAVE), baseName(CUR)); m[MAX_PATH + 63] = 0;
    int r = MessageBoxW(hMain, m, L"JawBreaker Dev.", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return 0;
    if (r == IDYES) return saveCur();
    DIRTY = 0; return 1;
}
static void openFile(const wchar_t *p) {
    if (!isTextName(p)) { wchar_t m[MAX_PATH + 64]; _snwprintf(m, MAX_PATH + 64, T(S_BINARY), baseName(p)); m[MAX_PATH + 63] = 0; status(L"%ls", m); return; }
    DWORD n; char *b = readFileA(p, &n); if (!b) return;
    int off = (n >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF) ? 3 : 0;
    int m = MultiByteToWideChar(CP_UTF8, 0, b + off, (int)n - off, NULL, 0); wchar_t *w = malloc((m * 2 + 1) * sizeof(wchar_t)), *t = malloc((m + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, b + off, (int)n - off, t, m); t[m] = 0; free(b);
    int o = 0; for (int i = 0; i < m; i++) { if (t[i] == L'\r') continue; if (t[i] == L'\n') w[o++] = L'\r'; w[o++] = t[i]; } w[o] = 0; free(t);   /* \n → \r\n (코드 칸) */
    COLORING = 1; SetWindowTextW(hEdit, w); COLORING = 0; free(w);
    wcsncpy(CUR, p, MAX_PATH - 1); CUR[MAX_PATH - 1] = 0; DIRTY = 0;
    SendMessageW(hEdit, EM_SETREADONLY, FALSE, 0);
    recolor(); setTitle(); status(L"%ls", p);
}

/* ================= 탐색기 ================= */
static void treeAdd(HTREEITEM parent, const wchar_t *dir, int depth) {
    if (depth > 12) return;
    wchar_t pat[MAX_PATH]; _snwprintf(pat, MAX_PATH, L"%ls\\*", dir); pat[MAX_PATH - 1] = 0;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return;
    /* 폴더 먼저 · 이름 순 (FindFirstFile 은 NTFS 에서 이름 순) — 두 번 돎 */
    for (int pass = 0; pass < 2; pass++) {
        if (pass) { FindClose(h); h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return; }
        do {
            if (fd.cFileName[0] == L'.') continue;
            int isd = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            if (isd != (pass == 0)) continue;
            if (isd && depth == 0 && !_wcsicmp(fd.cFileName, L"build")) continue;   /* 만들어진 파일 */
            wchar_t full[MAX_PATH]; _snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName); full[MAX_PATH - 1] = 0;
            TPATHS = realloc(TPATHS, sizeof(wchar_t*) * (NTP + 1)); TPATHS[NTP] = _wcsdup(full);
            TVINSERTSTRUCTW ti; ZeroMemory(&ti, sizeof ti); ti.hParent = parent; ti.hInsertAfter = TVI_LAST;
            ti.item.mask = TVIF_TEXT | TVIF_PARAM; ti.item.pszText = fd.cFileName; ti.item.lParam = NTP++;
            HTREEITEM it = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&ti);
            if (isd) { treeAdd(it, full, depth + 1); if (depth == 0) SendMessageW(hTree, TVM_EXPAND, TVE_EXPAND, (LPARAM)it); }
        } while (FindNextFileW(h, &fd));
    }
    FindClose(h);
}
static void treeReload(void) {
    SendMessageW(hTree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    for (int i = 0; i < NTP; i++) free(TPATHS[i]); free(TPATHS); TPATHS = NULL; NTP = 0;
    if (PROJ[0]) treeAdd(TVI_ROOT, PROJ, 0);
}
static void rememberProj(void) {
    wchar_t app[MAX_PATH], p[MAX_PATH]; DWORD an = GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH); if (!an || an >= MAX_PATH - 40) return;
    _snwprintf(p, MAX_PATH, L"%ls\\JDH", app); CreateDirectoryW(p, NULL);
    _snwprintf(p, MAX_PATH, L"%ls\\JDH\\dev-last.txt", app); p[MAX_PATH - 1] = 0;
    char u8[MAX_PATH * 4]; int n = WideCharToMultiByte(CP_UTF8, 0, PROJ, -1, u8, sizeof u8, NULL, NULL); if (n > 0) writeFileA(p, u8, (DWORD)n - 1);
}
static int lastProj(wchar_t *out) {
    wchar_t app[MAX_PATH], p[MAX_PATH]; DWORD an = GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH); if (!an || an >= MAX_PATH - 40) return 0;
    _snwprintf(p, MAX_PATH, L"%ls\\JDH\\dev-last.txt", app); p[MAX_PATH - 1] = 0;
    DWORD n; char *b = readFileA(p, &n); if (!b) return 0;
    int m = MultiByteToWideChar(CP_UTF8, 0, b, (int)n, out, MAX_PATH - 1); out[m > 0 ? m : 0] = 0; free(b); return m > 0;
}
static int openProject(const wchar_t *dir) {
    wchar_t full[MAX_PATH], jbs[MAX_PATH];
    if (!GetFullPathNameW(dir, MAX_PATH, full, NULL)) return 0;
    size_t L = wcslen(full); while (L > 3 && full[L - 1] == L'\\') full[--L] = 0;
    _snwprintf(jbs, MAX_PATH, L"%ls\\project.jbs", full); jbs[MAX_PATH - 1] = 0;
    if (!fileExists(jbs)) { MessageBoxW(hMain, T(S_NOTPROJ), L"JawBreaker Dev.", MB_OK | MB_ICONWARNING); return 0; }
    if (!askSave()) return 0;
    wcscpy(PROJ, full); CUR[0] = 0; DIRTY = 0;
    COLORING = 1; SetWindowTextW(hEdit, L""); COLORING = 0;
    treeReload(); rememberProj();
    /* main 파일: project.jbs 의 "main = …" (없으면 main.jb) */
    wchar_t mainp[MAX_PATH]; _snwprintf(mainp, MAX_PATH, L"%ls\\main.jb", PROJ); mainp[MAX_PATH - 1] = 0;
    DWORD n; char *b = readFileA(jbs, &n);
    if (b) {
        for (char *ln = b; ln && *ln; ) {
            char *nx = strchr(ln, '\n'); if (nx) *nx = 0;
            char *q = ln; while (*q == ' ' || *q == '\t') q++;
            if (!strncmp(q, "main", 4)) { q += 4; while (*q == ' ' || *q == '\t') q++; if (*q == '=') { q++; while (*q == ' ' || *q == '\t') q++;
                char *e = q + strlen(q); while (e > q && (e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
                wchar_t wm[MAX_PATH]; int m = MultiByteToWideChar(CP_UTF8, 0, q, -1, wm, MAX_PATH); if (m > 1) { for (wchar_t *c = wm; *c; c++) if (*c == L'/') *c = L'\\'; _snwprintf(mainp, MAX_PATH, L"%ls\\%ls", PROJ, wm); mainp[MAX_PATH - 1] = 0; } } }
            ln = nx ? nx + 1 : NULL;
        }
        free(b);
    }
    if (fileExists(mainp)) openFile(mainp);
    setTitle();
    for (int i = 0; i < 6; i++) EnableWindow(hBtn[i], i == 4 ? childProc != NULL : TRUE);
    return 1;
}
static void cmdOpen(void) {
    BROWSEINFOW bi; ZeroMemory(&bi, sizeof bi); bi.hwndOwner = hMain; bi.lpszTitle = T(S_PICKDIR); bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pl = SHBrowseForFolderW(&bi); if (!pl) return;
    wchar_t p[MAX_PATH]; if (SHGetPathFromIDListW(pl, p)) openProject(p);
    CoTaskMemFree(pl);
}

/* ================= 실행 (run.bat · jdh) — 출력은 로그 칸으로 ================= */
static DWORD WINAPI readerThread(LPVOID arg) {
    HANDLE rd = (HANDLE)arg; char buf[4096]; char carry[8]; int nc = 0; DWORD got;
    while (ReadFile(rd, buf + 0, sizeof buf - 8, &got, NULL) && got) {
        /* UTF-8 이 조각 사이에서 끊기면 남은 바이트를 다음에 붙임 */
        char *all = malloc(got + nc + 1); memcpy(all, carry, nc); memcpy(all + nc, buf, got); int n = got + nc; nc = 0;
        int cut = n; for (int k = 1; k <= 3 && k <= n; k++) { unsigned char c = (unsigned char)all[n - k]; if ((c & 0xC0) == 0x80) continue; if (c >= 0xC0) { int need = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2; if (need > k) cut = n - k; } break; }
        nc = n - cut; memcpy(carry, all + cut, nc);
        int m = MultiByteToWideChar(CP_UTF8, 0, all, cut, NULL, 0); wchar_t *w = malloc((m * 2 + 1) * sizeof(wchar_t)), *t = malloc((m + 1) * sizeof(wchar_t));
        MultiByteToWideChar(CP_UTF8, 0, all, cut, t, m); free(all);
        int o = 0; for (int i = 0; i < m; i++) { if (t[i] == L'\r') continue; if (t[i] == L'\n') w[o++] = L'\r'; w[o++] = t[i]; } w[o] = 0; free(t);
        PostMessageW(hMain, WM_APP_OUT, 0, (LPARAM)w);
    }
    CloseHandle(rd);
    DWORD code = 1; if (childProc) { WaitForSingleObject(childProc, INFINITE); GetExitCodeProcess(childProc, &code); }
    PostMessageW(hMain, WM_APP_END, code, 0);
    return 0;
}
/* 프로젝트 폴더에서 명령을 띄움 (창 없이 · 출력은 파이프) — 자식까지 같이 끄려고 작업 개체 (Job) 에 넣음 */
static int startChild(const wchar_t *cmdline, const wchar_t *label) {
    if (childProc) { status(L"%ls", T(S_BUSY)); return 0; }
    SECURITY_ATTRIBUTES sa = {sizeof sa, NULL, TRUE}; HANDLE rd, wr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return 0;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    HANDLE nul = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    STARTUPINFOW si; ZeroMemory(&si, sizeof si); si.cb = sizeof si; si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = wr; si.hStdInput = nul;
    PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
    /* 자식이 jdh 를 찾도록 PATH 앞에 bin 폴더 */
    static wchar_t envPath[32768]; wchar_t old[30000]; DWORD on = GetEnvironmentVariableW(L"PATH", old, 30000); if (on >= 30000) on = 0; old[on] = 0;
    _snwprintf(envPath, 32768, L"%ls;%ls", BINDIR, old); envPath[32767] = 0; SetEnvironmentVariableW(L"PATH", envPath);
    wchar_t *cl = _wcsdup(cmdline);
    BOOL ok = CreateProcessW(NULL, cl, NULL, NULL, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED, NULL, PROJ, &si, &pi);
    free(cl); SetEnvironmentVariableW(L"PATH", old);
    CloseHandle(wr); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    if (!ok) { CloseHandle(rd); logLine(T(S_FAILRUN), cmdline); return 0; }
    childJob = CreateJobObjectW(NULL, NULL);
    if (childJob) { JOBOBJECT_EXTENDED_LIMIT_INFORMATION li; ZeroMemory(&li, sizeof li); li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE; SetInformationJobObject(childJob, JobObjectExtendedLimitInformation, &li, sizeof li); AssignProcessToJobObject(childJob, pi.hProcess); }
    ResumeThread(pi.hThread); CloseHandle(pi.hThread);
    childProc = pi.hProcess; childStart = GetTickCount();
    logLine(T(S_RUNNING), label);
    EnableWindow(hBtn[3], FALSE); EnableWindow(hBtn[5], FALSE); EnableWindow(hBtn[4], TRUE);
    HANDLE th = CreateThread(NULL, 0, readerThread, rd, 0, NULL); if (th) CloseHandle(th);
    return 1;
}
static void jdhPath(wchar_t *out) { _snwprintf(out, MAX_PATH, L"%ls\\jdh.exe", BINDIR); out[MAX_PATH - 1] = 0; }
static void cmdRun(void) {
    if (!PROJ[0]) { status(L"%ls", T(S_NOPROJ)); return; }
    if (!saveCur()) return;
    wchar_t bat[MAX_PATH], cmd[MAX_PATH * 3], jdh[MAX_PATH];
    _snwprintf(bat, MAX_PATH, L"%ls\\run.bat", PROJ); bat[MAX_PATH - 1] = 0; jdhPath(jdh);
    if (fileExists(bat)) { wchar_t cmdexe[MAX_PATH]; GetSystemDirectoryW(cmdexe, MAX_PATH); wcscat(cmdexe, L"\\cmd.exe"); _snwprintf(cmd, MAX_PATH * 3, L"\"%ls\" /c \"\"%ls\"\"", cmdexe, bat); }
    else { if (!fileExists(jdh)) { logLine(L"%ls", T(S_NOJDH)); return; } _snwprintf(cmd, MAX_PATH * 3, L"\"%ls\" run \"%ls\"", jdh, PROJ); }
    cmd[MAX_PATH * 3 - 1] = 0;
    startChild(cmd, fileExists(bat) ? L"run.bat" : L"jdh run");
}
static void cmdExe(void) {
    if (!PROJ[0]) { status(L"%ls", T(S_NOPROJ)); return; }
    if (!saveCur()) return;
    wchar_t jdh[MAX_PATH], cmd[MAX_PATH * 3]; jdhPath(jdh);
    if (!fileExists(jdh)) { logLine(L"%ls", T(S_NOJDH)); return; }
    _snwprintf(cmd, MAX_PATH * 3, L"\"%ls\" build exe \"%ls\"", jdh, PROJ); cmd[MAX_PATH * 3 - 1] = 0;
    startChild(cmd, L"jdh build exe");
}
static void cmdStop(void) {
    if (!childProc) return;
    if (childJob) TerminateJobObject(childJob, 1); else TerminateProcess(childProc, 1);
    logLine(L"%ls", T(S_STOPPED));
}
static void cmdNewProject(void) {
    wchar_t file[MAX_PATH] = L"MyGame"; OPENFILENAMEW o; ZeroMemory(&o, sizeof o);
    o.lStructSize = sizeof o; o.hwndOwner = hMain; o.lpstrFile = file; o.nMaxFile = MAX_PATH; o.lpstrTitle = T(S_NEWNAME);
    o.lpstrFilter = L"Jawbreaker\0*.\0\0"; o.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
    if (!GetSaveFileNameW(&o)) return;
    if (dirExists(file) || fileExists(file)) { wchar_t m[MAX_PATH + 64]; _snwprintf(m, MAX_PATH + 64, T(S_EXISTS), file); m[MAX_PATH + 63] = 0; MessageBoxW(hMain, m, L"JawBreaker Dev.", MB_OK | MB_ICONWARNING); return; }
    wchar_t jdh[MAX_PATH], cmd[MAX_PATH * 3]; jdhPath(jdh);
    if (!fileExists(jdh)) { logLine(L"%ls", T(S_NOJDH)); return; }
    _snwprintf(cmd, MAX_PATH * 3, L"\"%ls\" new project \"%ls\"", jdh, file); cmd[MAX_PATH * 3 - 1] = 0;
    STARTUPINFOW si; ZeroMemory(&si, sizeof si); si.cb = sizeof si; PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { logLine(T(S_FAILRUN), cmd); return; }
    WaitForSingleObject(pi.hProcess, 30000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    openProject(file);
}

/* ================= 창 ================= */
static void layout(void) {
    RECT r; GetClientRect(hMain, &r);
    int W = r.right, H = r.bottom, bh = SC(32), sh = SC(22), tw = W / 5 < SC(180) ? SC(180) : W / 5, lh = H / 4 < SC(120) ? SC(120) : H / 4;
    for (int i = 0; i < 6; i++) MoveWindow(hBtn[i], SC(4) + i * SC(116), SC(3), SC(112), bh - SC(6), TRUE);
    MoveWindow(hStatus, 0, H - sh, W, sh, TRUE);
    MoveWindow(hTree, 0, bh, tw, H - bh - sh, TRUE);
    MoveWindow(hEdit, tw + 3, bh, W - tw - 3, H - bh - sh - lh - 3, TRUE);
    MoveWindow(hLog, tw + 3, H - sh - lh, W - tw - 3, lh, TRUE);
}
/* 창 그림을 BMP 로 (시험용) */
static void saveShot(const wchar_t *path) {
    RECT r; GetWindowRect(hMain, &r); int w = r.right - r.left, h = r.bottom - r.top;
    HDC sc = GetDC(NULL), mdc = CreateCompatibleDC(sc); HBITMAP bm = CreateCompatibleBitmap(sc, w, h); HGDIOBJ old = SelectObject(mdc, bm);
    if (!PrintWindow(hMain, mdc, 2)) BitBlt(mdc, 0, 0, w, h, sc, r.left, r.top, SRCCOPY);
    BITMAPINFOHEADER bi; ZeroMemory(&bi, sizeof bi); bi.biSize = sizeof bi; bi.biWidth = w; bi.biHeight = h; bi.biPlanes = 1; bi.biBitCount = 24; bi.biCompression = BI_RGB;
    DWORD row = ((w * 3 + 3) & ~3), sz = row * h; char *px = malloc(sz);
    GetDIBits(mdc, bm, 0, h, px, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    BITMAPFILEHEADER fh; ZeroMemory(&fh, sizeof fh); fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof bi; fh.bfSize = fh.bfOffBits + sz;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(f, &fh, sizeof fh, &wr, NULL); WriteFile(f, &bi, sizeof bi, &wr, NULL); WriteFile(f, px, sz, &wr, NULL); CloseHandle(f); }
    free(px); SelectObject(mdc, old); DeleteObject(bm); DeleteDC(mdc); ReleaseDC(NULL, sc);
}
static void selftestFinish(void) {
    wchar_t p[MAX_PATH + 8];
    _snwprintf(p, MAX_PATH + 8, L"%ls.bmp", SELFOUT); p[MAX_PATH + 7] = 0; saveShot(p);
    int L = GetWindowTextLengthW(hLog); wchar_t *w = malloc((L + 1) * sizeof(wchar_t)); GetWindowTextW(hLog, w, L + 1);
    int m = WideCharToMultiByte(CP_UTF8, 0, w, L, NULL, 0, NULL, NULL); char *b = malloc(m + 1); WideCharToMultiByte(CP_UTF8, 0, w, L, b, m, NULL, NULL);
    _snwprintf(p, MAX_PATH + 8, L"%ls.log", SELFOUT); p[MAX_PATH + 7] = 0; writeFileA(p, b, (DWORD)m);
    free(w); free(b);
    DestroyWindow(hMain);
}
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_SIZE: layout(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_NEWP: cmdNewProject(); return 0;
        case ID_OPEN: cmdOpen(); return 0;
        case ID_SAVE: saveCur(); return 0;
        case ID_RUN: cmdRun(); return 0;
        case ID_STOP: cmdStop(); return 0;
        case ID_EXE: cmdExe(); return 0;
        case ID_EDIT:
            if (HIWORD(wp) == EN_CHANGE && !COLORING) { if (!DIRTY) { DIRTY = 1; setTitle(); } SetTimer(h, TIMER_COLOR, 250, NULL); }
            return 0;
        }
        break;
    case WM_TIMER: if (wp == TIMER_COLOR) { KillTimer(h, TIMER_COLOR); recolor(); } return 0;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR*)lp;
        if (nh->idFrom == ID_TREE && nh->code == TVN_SELCHANGEDW) {
            NMTREEVIEWW *tv = (NMTREEVIEWW*)lp; int k = (int)tv->itemNew.lParam;
            if (k >= 0 && k < NTP && fileExists(TPATHS[k]) && _wcsicmp(TPATHS[k], CUR)) { if (askSave()) openFile(TPATHS[k]); }
        }
        return 0;
    }
    case WM_APP_OUT: { wchar_t *w = (wchar_t*)lp; logAppend(w); free(w); return 0; }
    case WM_APP_END: {
        logLine(T(S_DONE), (unsigned long)wp, (GetTickCount() - childStart) / 1000.0);
        if (childJob) { CloseHandle(childJob); childJob = NULL; }
        if (childProc) { CloseHandle(childProc); childProc = NULL; }
        EnableWindow(hBtn[3], TRUE); EnableWindow(hBtn[5], TRUE); EnableWindow(hBtn[4], FALSE);
        treeReload();
        if (SELFTEST) selftestFinish();
        return 0;
    }
    case WM_CLOSE: if (!askSave()) return 0; cmdStop(); DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
static void createUI(HINSTANCE hi) {
    LoadLibraryW(L"Msftedit.dll");
    { HDC dc = GetDC(NULL); DPI = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc); if (DPI < 96) DPI = 96; }
    INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = L"JawBreakerDev"; wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    hMain = CreateWindowExW(0, L"JawBreakerDev", L"JawBreaker Dev.", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, SC(1280), SC(820), NULL, NULL, hi, NULL);
    NONCLIENTMETRICSW nm = {sizeof nm}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof nm, &nm, 0); hFontUI = CreateFontIndirectW(&nm.lfMessageFont);
    hFontCode = CreateFontW(-SC(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    const int ids[6] = {ID_NEWP, ID_OPEN, ID_SAVE, ID_RUN, ID_STOP, ID_EXE}; const int sk[6] = {S_NEWP, S_OPEN, S_SAVE, S_RUN, S_STOP, S_EXE};
    for (int i = 0; i < 6; i++) { hBtn[i] = CreateWindowExW(0, L"BUTTON", T(sk[i]), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10, hMain, (HMENU)(INT_PTR)ids[i], hi, NULL); SendMessageW(hBtn[i], WM_SETFONT, (WPARAM)hFontUI, TRUE); }
    hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 10, 10, hMain, (HMENU)ID_TREE, hi, NULL);
    SendMessageW(hTree, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL | ES_WANTRETURN, 0, 0, 10, 10, hMain, (HMENU)ID_EDIT, hi, NULL);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFontCode, TRUE); SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE); SendMessageW(hEdit, EM_EXLIMITTEXT, 0, 64 * 1024 * 1024);
    SendMessageW(hEdit, EM_SETTEXTMODE, TM_PLAINTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);
    { PARAFORMAT2 pf; ZeroMemory(&pf, sizeof pf); pf.cbSize = sizeof pf; pf.dwMask = PFM_TABSTOPS; pf.cTabCount = 32; for (int i = 0; i < 32; i++) pf.rgxTabs[i] = (i + 1) * 4 * 120; SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf); }
    SendMessageW(hEdit, EM_SETREADONLY, TRUE, 0);
    hLog = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 10, 10, hMain, (HMENU)ID_LOG, hi, NULL);
    SendMessageW(hLog, WM_SETFONT, (WPARAM)hFontCode, TRUE); SendMessageW(hLog, EM_EXLIMITTEXT, 0, 16 * 1024 * 1024);
    hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hMain, (HMENU)ID_STATUS, hi, NULL);
    SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    for (int i = 0; i < 6; i++) EnableWindow(hBtn[i], i == 0 || i == 1);
    layout(); setTitle(); status(L"%ls", T(S_NOPROJ));
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int show) {
    (void)hp; (void)cl;
    pickLang();
    wchar_t exe[MAX_PATH]; DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) { fail(L"프로그램 위치를 알 수 없습니다."); return 1; }
    wcscpy(BINDIR, exe); { wchar_t *sl = wcsrchr(BINDIR, L'\\'); if (sl) *sl = 0; }
    int argc = 0; wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    const wchar_t *openArg = NULL; int noUpdate = 0;
    for (int i = 1; i < argc; i++) {
        if (!wcscmp(argv[i], L"--no-update")) noUpdate = 1;
        else if (!wcscmp(argv[i], L"--selftest") && i + 2 < argc) { SELFTEST = 1; noUpdate = 1; openArg = argv[i + 1]; wcsncpy(SELFOUT, argv[i + 2], MAX_PATH - 1); i += 2; }
        else openArg = argv[i];
    }
    /* 새 버전 확인 — 업데이트했으면 (이 파일도 바뀌었을 수 있으니) 새 jbdev.exe 로 다시 시작 */
    if (!noUpdate && checkUpdate(BINDIR)) {
        static wchar_t again[MAX_PATH + 32]; _snwprintf(again, MAX_PATH + 32, L"\"%ls\" --no-update", exe); again[MAX_PATH + 31] = 0;
        STARTUPINFOW s2; PROCESS_INFORMATION p2; ZeroMemory(&s2, sizeof s2); s2.cb = sizeof s2; ZeroMemory(&p2, sizeof p2);
        if (CreateProcessW(exe, again, NULL, NULL, FALSE, 0, NULL, NULL, &s2, &p2)) { CloseHandle(p2.hThread); CloseHandle(p2.hProcess); return 0; }
    }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    createUI(hi);
    ShowWindow(hMain, SELFTEST ? SW_SHOWNORMAL : show); UpdateWindow(hMain);
    wchar_t last[MAX_PATH];
    if (openArg) openProject(openArg);
    else if (lastProj(last) && dirExists(last)) openProject(last);
    if (SELFTEST) { if (!PROJ[0]) { DestroyWindow(hMain); return 2; } cmdRun(); if (!childProc) selftestFinish(); }
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_KEYDOWN) {
            int ctrl = GetKeyState(VK_CONTROL) < 0;
            if (m.wParam == VK_F5) { cmdRun(); continue; }
            if (ctrl && m.wParam == 'S') { saveCur(); continue; }
            if (ctrl && m.wParam == 'O') { cmdOpen(); continue; }
            if (m.wParam == VK_TAB && m.hwnd == hEdit && !ctrl) { SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"    "); continue; }   /* 탭 = 빈칸 4개 */
        }
        TranslateMessage(&m); DispatchMessageW(&m);
    }
    LocalFree(argv);
    return 0;
}
