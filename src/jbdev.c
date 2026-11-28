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
static const wchar_t *LANGCODE[5] = {L"ko", L"en", L"ja", L"zh-CN", L"zh-TW"};
enum { S_NEWP, S_OPEN, S_SAVE, S_CHECK, S_RUN, S_STOP, S_EXE, S_HTML, S_NOPROJ, S_NOTPROJ, S_ASKSAVE, S_BINARY, S_RUNNING, S_DONE, S_FAILRUN, S_PICKDIR,
       S_NEWNAME, S_SAVED, S_STOPPED, S_BUSY, S_NOJDH, S_EXISTS, S_TITLE_NONE, S_DOCS, S_NEWFILE, S_NEWDIR, S_RENAME, S_DELETE, S_EXPLORE,
       S_ASKDEL, S_NAME, S_OKB, S_CANCELB, S_NOTFOUND, S_REPLACED, S_READONLY, S_BADNAME, S_FAILFILE, S_N };
static const wchar_t *TR[S_N][5] = {
    {L"새 프로젝트", L"New project", L"新規プロジェクト", L"新建项目", L"新建專案"},
    {L"폴더 열기", L"Open folder", L"フォルダを開く", L"打开文件夹", L"開啟資料夾"},
    {L"저장", L"Save", L"保存", L"保存", L"儲存"},
    {L"검사", L"Check", L"検査", L"检查", L"檢查"},
    {L"▶ 실행", L"▶ Run", L"▶ 実行", L"▶ 运行", L"▶ 執行"},
    {L"■ 정지", L"■ Stop", L"■ 停止", L"■ 停止", L"■ 停止"},
    {L"EXE 만들기", L"Build EXE", L"EXE を作成", L"生成 EXE", L"產生 EXE"},
    {L"HTML 만들기", L"Build HTML", L"HTML を作成", L"生成 HTML", L"產生 HTML"},
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
    {L"내장 설명서", L"Built-in reference", L"組み込みリファレンス", L"内置参考文档", L"內建參考文件"},
    {L"새 파일", L"New file", L"新規ファイル", L"新建文件", L"新建檔案"},
    {L"새 폴더", L"New folder", L"新規フォルダー", L"新建文件夹", L"新建資料夾"},
    {L"이름 바꾸기", L"Rename", L"名前の変更", L"重命名", L"重新命名"},
    {L"지우기 (휴지통)", L"Delete (Recycle Bin)", L"削除 (ごみ箱)", L"删除 (回收站)", L"刪除 (資源回收筒)"},
    {L"탐색기에서 열기", L"Open in Explorer", L"エクスプローラーで開く", L"在资源管理器中打开", L"在檔案總管中開啟"},
    {L"'%ls' 을(를) 휴지통으로 보낼까요?", L"Send '%ls' to the Recycle Bin?", L"'%ls' をごみ箱に移動しますか?", L"要把 '%ls' 移到回收站吗?", L"要把 '%ls' 移到資源回收筒嗎?"},
    {L"이름:", L"Name:", L"名前:", L"名称:", L"名稱:"},
    {L"확인", L"OK", L"OK", L"确定", L"確定"},
    {L"취소", L"Cancel", L"キャンセル", L"取消", L"取消"},
    {L"찾지 못했습니다: %ls", L"Not found: %ls", L"見つかりません: %ls", L"未找到: %ls", L"找不到: %ls"},
    {L"%d개를 바꿨습니다", L"Replaced %d", L"%d 件置き換えました", L"已替换 %d 处", L"已取代 %d 處"},
    {L"읽기 전용 (내장 설명서)", L"Read-only (built-in reference)", L"読み取り専用 (組み込みリファレンス)", L"只读 (内置参考文档)", L"唯讀 (內建參考文件)"},
    {L"쓸 수 없는 이름입니다: %ls", L"That name cannot be used: %ls", L"使えない名前です: %ls", L"无法使用该名称: %ls", L"無法使用該名稱: %ls"},
    {L"파일 작업에 실패했습니다: %ls", L"File operation failed: %ls", L"ファイル操作に失敗しました: %ls", L"文件操作失败: %ls", L"檔案操作失敗: %ls"},
};
#define T(k) TR[k][LANG]
static void pickLang(void) {
    LANGID id = GetUserDefaultUILanguage(); WORD pri = PRIMARYLANGID(id), sub = SUBLANGID(id);
    LANG = pri == LANG_KOREAN ? L_KO : pri == LANG_JAPANESE ? L_JA : pri == LANG_CHINESE ? (sub == SUBLANG_CHINESE_SIMPLIFIED || sub == SUBLANG_CHINESE_SINGAPORE ? L_ZHCN : L_ZHTW) : L_EN;
    wchar_t e[16]; DWORD n = GetEnvironmentVariableW(L"JDH_LANG", e, 16);   /* 시험용: ko · en · ja · zh-CN · zh-TW */
    if (n && n < 16) for (int i = 0; i < 5; i++) if (!wcscmp(e, LANGCODE[i])) LANG = i;
}

/* ================= 상태 ================= */
enum { B_NEWP, B_OPEN, B_SAVE, B_CHECK, B_RUN, B_STOP, B_EXE, B_HTML, B_N };
enum { ID_BTN = 100, ID_TREE = 200, ID_EDIT, ID_LOG, ID_STATUS, ID_TAB, ID_AC,
       ID_M_NEWFILE = 300, ID_M_NEWDIR, ID_M_RENAME, ID_M_DELETE, ID_M_EXPLORE, ID_IN_EDIT = 400 };
#define WM_APP_OUT (WM_APP + 1)   /* 자식의 출력 (lParam = wchar_t* · 받은 쪽이 free) */
#define WM_APP_END (WM_APP + 2)   /* 자식이 끝남 (wParam = 종료 코드) */
#define TIMER_COLOR 1
static HWND hMain, hTree, hEdit, hLog, hStatus, hTab, hAc, hFind, hBtn[B_N];
static HFONT hFontUI, hFontCode;
static int DPI = 96;
#define SC(x) MulDiv((x), DPI, 96)   /* 화면 배율 (125% · 150% …) 에 맞춘 크기 */
static wchar_t BINDIR[MAX_PATH], NATDIR[MAX_PATH];   /* jbdev.exe · jdh.exe 가 있는 곳 · 설명서 · 자동완성 목록이 있는 곳 */
static wchar_t PROJ[MAX_PATH];                       /* 연 프로젝트 폴더 ("" = 없음) */
static int COLORING;
/* 탐색기 항목: lParam >= 0 → TPATHS · < 0 → 내장 설명서 DOCP[-lParam-1] · LP_DOCROOT = 설명서 묶음 */
#define LP_DOCROOT (-100000)
static wchar_t **TPATHS; static int NTP;
static wchar_t DOCP[16][MAX_PATH]; static int NDOC;
static HANDLE childProc, childJob;
static DWORD childStart;
static int SELFTEST; static wchar_t SELFOUT[MAX_PATH];
static UINT WM_FINDMSG;

/* 열린 파일 (탭) */
typedef struct { wchar_t path[MAX_PATH]; wchar_t *text; int dirty, ro; CHARRANGE sel; POINT scroll; } Doc;
#define MAXDOC 48
static Doc DOCS[MAXDOC]; static int NDOCS, CURD = -1;

static void status(const wchar_t *fmt, ...) { wchar_t b[1024]; va_list ap; va_start(ap, fmt); _vsnwprintf(b, 1024, fmt, ap); va_end(ap); b[1023] = 0; SetWindowTextW(hStatus, b); }
static void logAppend(const wchar_t *t) {
    int n = GetWindowTextLengthW(hLog);
    if (n > 400000) { SendMessageW(hLog, EM_SETSEL, 0, n / 2); SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)L""); n = GetWindowTextLengthW(hLog); }
    SendMessageW(hLog, EM_SETSEL, n, n); SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)t); SendMessageW(hLog, WM_VSCROLL, SB_BOTTOM, 0);
}
static void logLine(const wchar_t *fmt, ...) { wchar_t b[2048]; va_list ap; va_start(ap, fmt); _vsnwprintf(b, 2046, fmt, ap); va_end(ap); b[2045] = 0; wcscat(b, L"\r\n"); logAppend(b); }
static const wchar_t *baseName(const wchar_t *p) { const wchar_t *s = wcsrchr(p, L'\\'); return s ? s + 1 : p; }
static void msgf(UINT icon, const wchar_t *fmt, const wchar_t *arg) { wchar_t m[MAX_PATH + 160]; _snwprintf(m, MAX_PATH + 160, fmt, arg); m[MAX_PATH + 159] = 0; if (SELFTEST) logLine(L"%ls", m); else MessageBoxW(hMain, m, L"JawBreaker Dev.", MB_OK | icon); }

/* ================= 파일 (UTF-8 ↔ 글자) ================= */
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
/* UTF-8 → 코드 칸 글자 (줄 끝 \r\n) */
static wchar_t *utf8ToEdit(const char *b, int n) {
    int off = (n >= 3 && (unsigned char)b[0] == 0xEF && (unsigned char)b[1] == 0xBB && (unsigned char)b[2] == 0xBF) ? 3 : 0;
    int m = MultiByteToWideChar(CP_UTF8, 0, b + off, n - off, NULL, 0); wchar_t *w = malloc((m * 2 + 1) * sizeof(wchar_t)), *t = malloc((m + 1) * sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, b + off, n - off, t, m); t[m] = 0;
    int o = 0; for (int i = 0; i < m; i++) { if (t[i] == L'\r') continue; if (t[i] == L'\n') w[o++] = L'\r'; w[o++] = t[i]; } w[o] = 0; free(t);
    return w;
}
/* 글자 → UTF-8 (줄 끝 \n) */
static char *wideToUtf8(const wchar_t *src, DWORD *n) {
    size_t L = wcslen(src); wchar_t *w = malloc((L + 1) * sizeof(wchar_t)); int o = 0;
    for (size_t i = 0; i < L; i++) { if (src[i] == L'\r') { w[o++] = L'\n'; if (src[i + 1] == L'\n') i++; } else w[o++] = src[i]; } w[o] = 0;
    int m = WideCharToMultiByte(CP_UTF8, 0, w, o, NULL, 0, NULL, NULL); char *b = malloc(m + 1);
    WideCharToMultiByte(CP_UTF8, 0, w, o, b, m, NULL, NULL); b[m] = 0; free(w); *n = (DWORD)m; return b;
}
static wchar_t *editText(void) { int L = GetWindowTextLengthW(hEdit); wchar_t *w = malloc((L + 1) * sizeof(wchar_t)); GetWindowTextW(hEdit, w, L + 1); return w; }

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
static wchar_t *editTextRE(int *len) {   /* RichEdit 위치와 같은 글자 (\r 하나가 한 칸) */
    int L = GetWindowTextLengthW(hEdit); wchar_t *w = malloc((L + 1) * sizeof(wchar_t)); GETTEXTEX gt = {(DWORD)((L + 1) * sizeof(wchar_t)), GT_DEFAULT, 1200, NULL, NULL};
    L = (int)SendMessageW(hEdit, EM_GETTEXTEX, (WPARAM)&gt, (LPARAM)w); w[L] = 0; *len = L; return w;
}
static void recolor(void) {
    if (!hEdit) return;
    if (GetWindowTextLengthW(hEdit) > 400000) return;   /* 아주 큰 파일은 색칠하지 않음 */
    int L; wchar_t *w = editTextRE(&L);
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

/* ================= 탭 · 열기 · 저장 ================= */
static void setTitle(void) {
    wchar_t t[MAX_PATH * 2];
    if (!PROJ[0]) _snwprintf(t, MAX_PATH * 2, L"JawBreaker Dev. — %ls", T(S_TITLE_NONE));
    else _snwprintf(t, MAX_PATH * 2, L"JawBreaker Dev. — %ls%ls%ls", baseName(PROJ), CURD >= 0 ? L" · " : L"", CURD >= 0 ? baseName(DOCS[CURD].path) : L"");
    t[MAX_PATH * 2 - 1] = 0; SetWindowTextW(hMain, t);
}
static void tabLabel(int i) {
    wchar_t t[MAX_PATH + 8]; _snwprintf(t, MAX_PATH + 8, L"%ls%ls", baseName(DOCS[i].path), DOCS[i].dirty ? L" •" : DOCS[i].ro ? L" 🔒" : L""); t[MAX_PATH + 7] = 0;
    TCITEMW ti; ti.mask = TCIF_TEXT; ti.pszText = t; SendMessageW(hTab, TCM_SETITEMW, i, (LPARAM)&ti);
}
/* 지금 탭의 글자 · 선택 · 스크롤을 DOCS 에 기억 */
static void docStash(void) {
    if (CURD < 0) return;
    Doc *d = &DOCS[CURD]; free(d->text); d->text = editText();
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&d->sel); SendMessageW(hEdit, EM_GETSCROLLPOS, 0, (LPARAM)&d->scroll);
}
static void docShow(int i) {
    CURD = i; SendMessageW(hTab, TCM_SETCURSEL, i < 0 ? (WPARAM)-1 : (WPARAM)i, 0);
    COLORING = 1;
    if (i < 0) { SetWindowTextW(hEdit, L""); SendMessageW(hEdit, EM_SETREADONLY, TRUE, 0); COLORING = 0; setTitle(); return; }
    Doc *d = &DOCS[i];
    SetWindowTextW(hEdit, d->text ? d->text : L""); SendMessageW(hEdit, EM_SETREADONLY, d->ro, 0);
    SendMessageW(hEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    COLORING = 0;
    recolor();
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&d->sel); SendMessageW(hEdit, EM_SETSCROLLPOS, 0, (LPARAM)&d->scroll);
    setTitle(); status(L"%ls%ls%ls", d->path, d->ro ? L" — " : L"", d->ro ? T(S_READONLY) : L"");
    SetFocus(hEdit);
}
static int saveDoc(int i) {
    if (i < 0 || DOCS[i].ro || !DOCS[i].dirty) return 1;
    if (i == CURD) docStash();
    DWORD n; char *b = wideToUtf8(DOCS[i].text, &n); int ok = writeFileA(DOCS[i].path, b, n); free(b);
    if (ok) { DOCS[i].dirty = 0; tabLabel(i); status(T(S_SAVED), baseName(DOCS[i].path)); }
    else msgf(MB_ICONERROR, T(S_FAILFILE), DOCS[i].path);
    return ok;
}
static int saveAll(void) { for (int i = 0; i < NDOCS; i++) if (!saveDoc(i)) return 0; return 1; }
/* 고친 것이 있으면 저장할지 물음 — 0 = 취소 */
static int askSaveDoc(int i) {
    if (i < 0 || !DOCS[i].dirty) return 1;
    if (SELFTEST) return saveDoc(i);
    wchar_t m[MAX_PATH + 64]; _snwprintf(m, MAX_PATH + 64, T(S_ASKSAVE), baseName(DOCS[i].path)); m[MAX_PATH + 63] = 0;
    int r = MessageBoxW(hMain, m, L"JawBreaker Dev.", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return 0;
    if (r == IDYES) return saveDoc(i);
    DOCS[i].dirty = 0; return 1;
}
static int askSaveAll(void) { if (CURD >= 0) docStash(); for (int i = 0; i < NDOCS; i++) if (!askSaveDoc(i)) return 0; return 1; }
static void closeDoc(int i) {
    if (i < 0 || i >= NDOCS) return;
    if (i == CURD) docStash();
    if (!askSaveDoc(i)) return;
    free(DOCS[i].text); memmove(DOCS + i, DOCS + i + 1, sizeof(Doc) * (NDOCS - i - 1)); NDOCS--;
    SendMessageW(hTab, TCM_DELETEITEM, i, 0);
    int nx = CURD == i ? (i < NDOCS ? i : NDOCS - 1) : CURD > i ? CURD - 1 : CURD;
    CURD = -1; docShow(nx);
}
static void closeAll(void) { for (int i = 0; i < NDOCS; i++) free(DOCS[i].text); NDOCS = 0; CURD = -1; SendMessageW(hTab, TCM_DELETEALLITEMS, 0, 0); docShow(-1); }
static int findDoc(const wchar_t *p) { for (int i = 0; i < NDOCS; i++) if (!_wcsicmp(DOCS[i].path, p)) return i; return -1; }
static int openFile(const wchar_t *p, int ro) {
    int k = findDoc(p); if (k >= 0) { if (k != CURD) { docStash(); docShow(k); } return k; }
    if (!isTextName(p)) { wchar_t m[MAX_PATH + 64]; _snwprintf(m, MAX_PATH + 64, T(S_BINARY), baseName(p)); m[MAX_PATH + 63] = 0; status(L"%ls", m); return -1; }
    if (NDOCS >= MAXDOC) closeDoc(0);
    DWORD n; char *b = readFileA(p, &n); if (!b) { msgf(MB_ICONERROR, T(S_FAILFILE), p); return -1; }
    docStash();
    Doc *d = &DOCS[NDOCS]; ZeroMemory(d, sizeof *d);
    wcsncpy(d->path, p, MAX_PATH - 1); d->text = utf8ToEdit(b, (int)n); d->ro = ro; free(b);
    TCITEMW ti; ti.mask = TCIF_TEXT; ti.pszText = (wchar_t*)baseName(p); SendMessageW(hTab, TCM_INSERTITEMW, NDOCS, (LPARAM)&ti);
    NDOCS++; tabLabel(NDOCS - 1); docShow(NDOCS - 1);
    return NDOCS - 1;
}
static void gotoLine(int line) {   /* 1 부터 */
    int c = (int)SendMessageW(hEdit, EM_LINEINDEX, line > 0 ? line - 1 : 0, 0); if (c < 0) return;
    int e = c + (int)SendMessageW(hEdit, EM_LINELENGTH, c, 0);
    CHARRANGE cr = {c, e}; SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr); SendMessageW(hEdit, EM_SCROLLCARET, 0, 0); SetFocus(hEdit);
}

/* ================= 탐색기 ================= */
static void treeAdd(HTREEITEM parent, const wchar_t *dir, int depth) {
    if (depth > 12) return;
    wchar_t pat[MAX_PATH]; _snwprintf(pat, MAX_PATH, L"%ls\\*", dir); pat[MAX_PATH - 1] = 0;
    for (int pass = 0; pass < 2; pass++) {   /* 폴더 먼저 */
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return;
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
        FindClose(h);
    }
}
static void findDocs(void) {   /* 내장 설명서: NATDIR\docs\<언어>\*.jbl (없으면 ko) */
    NDOC = 0; wchar_t dir[MAX_PATH], pat[MAX_PATH];
    _snwprintf(dir, MAX_PATH, L"%ls\\docs\\%ls", NATDIR, LANGCODE[LANG]); dir[MAX_PATH - 1] = 0;
    if (!dirExists(dir)) { _snwprintf(dir, MAX_PATH, L"%ls\\docs\\ko", NATDIR); dir[MAX_PATH - 1] = 0; }
    static const wchar_t *ORDER[] = {L"jawbreaker.jbl", L"DisPlayer.jbl", L"colliders.jbl", L"Canvas.jbl", L"Magellan.jbl", L"soundMaster.jbl", L"SeongBin.jbl", NULL};
    for (int i = 0; ORDER[i] && NDOC < 16; i++) { _snwprintf(pat, MAX_PATH, L"%ls\\%ls", dir, ORDER[i]); pat[MAX_PATH - 1] = 0; if (fileExists(pat)) wcscpy(DOCP[NDOC++], pat); }
}
static void treeReload(void) {
    SendMessageW(hTree, WM_SETREDRAW, FALSE, 0);
    SendMessageW(hTree, TVM_DELETEITEM, 0, (LPARAM)TVI_ROOT);
    for (int i = 0; i < NTP; i++) free(TPATHS[i]); free(TPATHS); TPATHS = NULL; NTP = 0;
    if (PROJ[0]) treeAdd(TVI_ROOT, PROJ, 0);
    if (NDOC) {
        TVINSERTSTRUCTW ti; ZeroMemory(&ti, sizeof ti); ti.hParent = TVI_ROOT; ti.hInsertAfter = TVI_LAST;
        ti.item.mask = TVIF_TEXT | TVIF_PARAM; ti.item.pszText = (wchar_t*)T(S_DOCS); ti.item.lParam = LP_DOCROOT;
        HTREEITEM r = (HTREEITEM)SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&ti);
        for (int i = 0; i < NDOC; i++) { ti.hParent = r; ti.item.pszText = (wchar_t*)baseName(DOCP[i]); ti.item.lParam = -(i + 1); SendMessageW(hTree, TVM_INSERTITEMW, 0, (LPARAM)&ti); }
        if (!PROJ[0]) SendMessageW(hTree, TVM_EXPAND, TVE_EXPAND, (LPARAM)r);
    }
    SendMessageW(hTree, WM_SETREDRAW, TRUE, 0);
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
static void enableButtons(void) {
    int run = childProc != NULL, p = PROJ[0] != 0;
    EnableWindow(hBtn[B_SAVE], p); EnableWindow(hBtn[B_CHECK], p && !run); EnableWindow(hBtn[B_RUN], p && !run);
    EnableWindow(hBtn[B_STOP], run); EnableWindow(hBtn[B_EXE], p && !run); EnableWindow(hBtn[B_HTML], p && !run);
}
static int openProject(const wchar_t *dir) {
    wchar_t full[MAX_PATH], jbs[MAX_PATH];
    if (!GetFullPathNameW(dir, MAX_PATH, full, NULL)) return 0;
    size_t L = wcslen(full); while (L > 3 && full[L - 1] == L'\\') full[--L] = 0;
    _snwprintf(jbs, MAX_PATH, L"%ls\\project.jbs", full); jbs[MAX_PATH - 1] = 0;
    if (!fileExists(jbs)) { msgf(MB_ICONWARNING, L"%ls", T(S_NOTPROJ)); return 0; }
    if (!askSaveAll()) return 0;
    closeAll();
    wcscpy(PROJ, full); treeReload(); rememberProj();
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
    if (fileExists(mainp)) openFile(mainp, 0);
    setTitle(); enableButtons();
    return 1;
}
static void cmdOpen(void) {
    BROWSEINFOW bi; ZeroMemory(&bi, sizeof bi); bi.hwndOwner = hMain; bi.lpszTitle = T(S_PICKDIR); bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE pl = SHBrowseForFolderW(&bi); if (!pl) return;
    wchar_t p[MAX_PATH]; if (SHGetPathFromIDListW(pl, p)) openProject(p);
    CoTaskMemFree(pl);
}

/* ================= 이름 입력 창 (작은 모달) ================= */
static wchar_t *inBuf; static int inDone;
static LRESULT CALLBACK InProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) { GetDlgItemTextW(h, ID_IN_EDIT, inBuf, MAX_PATH); inDone = 1; DestroyWindow(h); return 0; }
        if (LOWORD(wp) == IDCANCEL) { inDone = -1; DestroyWindow(h); return 0; }
        break;
    case WM_CLOSE: inDone = -1; DestroyWindow(h); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
/* 1 = 확인 (out 에 글자) */
static int inputBox(const wchar_t *title, const wchar_t *init, wchar_t *out) {
    static int reg; HINSTANCE hi = GetModuleHandleW(NULL);
    if (!reg) { WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = InProc; wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = L"JBDevInput"; RegisterClassExW(&wc); reg = 1; }
    RECT r; GetWindowRect(hMain, &r); int w = SC(380), hh = SC(140);
    HWND h = CreateWindowExW(WS_EX_DLGMODALFRAME, L"JBDevInput", title, WS_POPUP | WS_CAPTION | WS_SYSMENU, r.left + (r.right - r.left - w) / 2, r.top + (r.bottom - r.top - hh) / 3, w, hh, hMain, NULL, hi, NULL);
    HWND st = CreateWindowExW(0, L"STATIC", T(S_NAME), WS_CHILD | WS_VISIBLE, SC(12), SC(12), SC(80), SC(20), h, NULL, hi, NULL);
    HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", init, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, SC(12), SC(34), w - SC(40), SC(24), h, (HMENU)ID_IN_EDIT, hi, NULL);
    HWND ok = CreateWindowExW(0, L"BUTTON", T(S_OKB), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, w - SC(200), SC(68), SC(84), SC(26), h, (HMENU)IDOK, hi, NULL);
    HWND ca = CreateWindowExW(0, L"BUTTON", T(S_CANCELB), WS_CHILD | WS_VISIBLE | WS_TABSTOP, w - SC(108), SC(68), SC(84), SC(26), h, (HMENU)IDCANCEL, hi, NULL);
    HWND ctl[4] = {st, ed, ok, ca}; for (int i = 0; i < 4; i++) SendMessageW(ctl[i], WM_SETFONT, (WPARAM)hFontUI, TRUE);
    SendMessageW(ed, EM_SETSEL, 0, -1); const wchar_t *dot = wcsrchr(init, L'.'); if (dot && dot != init) SendMessageW(ed, EM_SETSEL, 0, dot - init);
    inBuf = out; inDone = 0; EnableWindow(hMain, FALSE); ShowWindow(h, SW_SHOW); SetFocus(ed);
    MSG m;
    while (!inDone && GetMessageW(&m, NULL, 0, 0)) { if (!IsDialogMessageW(h, &m)) { TranslateMessage(&m); DispatchMessageW(&m); } }
    EnableWindow(hMain, TRUE); SetForegroundWindow(hMain);
    return inDone == 1 && out[0];
}
static int okName(const wchar_t *n) { if (!n[0] || !wcscmp(n, L".") || !wcscmp(n, L"..")) return 0; for (const wchar_t *c = n; *c; c++) if (wcschr(L"\\/:*?\"<>|", *c)) return 0; return 1; }

/* ================= 탐색기 메뉴 (새 파일 · 새 폴더 · 이름 바꾸기 · 지우기) ================= */
static wchar_t MENUPATH[MAX_PATH];   /* 오른쪽 단추로 누른 항목 ("" = 프로젝트 최상위) */
static void menuDir(wchar_t *dir) { if (!MENUPATH[0]) wcscpy(dir, PROJ); else if (dirExists(MENUPATH)) wcscpy(dir, MENUPATH); else { wcscpy(dir, MENUPATH); wchar_t *s = wcsrchr(dir, L'\\'); if (s) *s = 0; } }
static void cmdNewFile(int folder) {
    if (!PROJ[0]) return;
    wchar_t dir[MAX_PATH], name[MAX_PATH] = L"", full[MAX_PATH]; menuDir(dir);
    if (!inputBox(T(folder ? S_NEWDIR : S_NEWFILE), folder ? L"" : L"new.jb", name)) return;
    if (!okName(name)) { msgf(MB_ICONWARNING, T(S_BADNAME), name); return; }
    _snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, name); full[MAX_PATH - 1] = 0;
    if (fileExists(full) || dirExists(full)) { msgf(MB_ICONWARNING, T(S_EXISTS), name); return; }
    int ok;
    if (folder) ok = CreateDirectoryW(full, NULL);
    else { const char *t = ""; const wchar_t *x = wcsrchr(name, L'.');
        if (x && !_wcsicmp(x, L".jb")) t = "Use jawbreaker call jaw;\n\nrun{\n    \n}\n";
        ok = writeFileA(full, t, (DWORD)strlen(t)); }
    if (!ok) { msgf(MB_ICONERROR, T(S_FAILFILE), full); return; }
    treeReload(); if (!folder) openFile(full, 0);
}
static void cmdRename(void) {
    if (!MENUPATH[0]) return;
    wchar_t name[MAX_PATH] = L"", full[MAX_PATH], dir[MAX_PATH];
    if (!inputBox(T(S_RENAME), baseName(MENUPATH), name)) return;
    if (!okName(name)) { msgf(MB_ICONWARNING, T(S_BADNAME), name); return; }
    wcscpy(dir, MENUPATH); { wchar_t *s = wcsrchr(dir, L'\\'); if (s) *s = 0; }
    _snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, name); full[MAX_PATH - 1] = 0;
    if (!_wcsicmp(full, MENUPATH)) return;
    if (fileExists(full) || dirExists(full)) { msgf(MB_ICONWARNING, T(S_EXISTS), name); return; }
    if (!MoveFileW(MENUPATH, full)) { msgf(MB_ICONERROR, T(S_FAILFILE), MENUPATH); return; }
    size_t L = wcslen(MENUPATH);   /* 열린 탭의 경로도 바꿈 */
    for (int i = 0; i < NDOCS; i++) if (!_wcsnicmp(DOCS[i].path, MENUPATH, L) && (DOCS[i].path[L] == 0 || DOCS[i].path[L] == L'\\')) {
        wchar_t np[MAX_PATH]; _snwprintf(np, MAX_PATH, L"%ls%ls", full, DOCS[i].path + L); np[MAX_PATH - 1] = 0; wcscpy(DOCS[i].path, np); tabLabel(i);
    }
    treeReload(); setTitle();
}
static void cmdDelete(void) {
    if (!MENUPATH[0]) return;
    wchar_t m[MAX_PATH + 64]; _snwprintf(m, MAX_PATH + 64, T(S_ASKDEL), baseName(MENUPATH)); m[MAX_PATH + 63] = 0;
    if (MessageBoxW(hMain, m, L"JawBreaker Dev.", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    size_t L = wcslen(MENUPATH);
    for (int i = NDOCS - 1; i >= 0; i--) if (!_wcsnicmp(DOCS[i].path, MENUPATH, L) && (DOCS[i].path[L] == 0 || DOCS[i].path[L] == L'\\')) { DOCS[i].dirty = 0; closeDoc(i); }
    wchar_t from[MAX_PATH + 2]; ZeroMemory(from, sizeof from); wcscpy(from, MENUPATH);   /* 두 번 0 으로 끝남 */
    SHFILEOPSTRUCTW op; ZeroMemory(&op, sizeof op); op.hwnd = hMain; op.wFunc = FO_DELETE; op.pFrom = from; op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;
    if (SHFileOperationW(&op)) msgf(MB_ICONERROR, T(S_FAILFILE), MENUPATH);
    treeReload();
}
static void treeMenu(void) {
    POINT pt; GetCursorPos(&pt); POINT cp = pt; ScreenToClient(hTree, &cp);
    TVHITTESTINFO ht = {cp, 0, NULL}; HTREEITEM it = (HTREEITEM)SendMessageW(hTree, TVM_HITTEST, 0, (LPARAM)&ht);
    MENUPATH[0] = 0;
    if (it) {
        TVITEMW tv; tv.mask = TVIF_PARAM; tv.hItem = it; SendMessageW(hTree, TVM_GETITEMW, 0, (LPARAM)&tv);
        if (tv.lParam < 0) return;   /* 내장 설명서 */
        if (tv.lParam < NTP) wcscpy(MENUPATH, TPATHS[tv.lParam]);
    }
    if (!PROJ[0]) return;
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, ID_M_NEWFILE, T(S_NEWFILE)); AppendMenuW(m, MF_STRING, ID_M_NEWDIR, T(S_NEWDIR));
    if (MENUPATH[0]) { AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, ID_M_RENAME, T(S_RENAME)); AppendMenuW(m, MF_STRING, ID_M_DELETE, T(S_DELETE)); }
    AppendMenuW(m, MF_SEPARATOR, 0, NULL); AppendMenuW(m, MF_STRING, ID_M_EXPLORE, T(S_EXPLORE));
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hMain, NULL); DestroyMenu(m);
}

/* ================= 찾기 · 바꾸기 (Ctrl+F · Ctrl+H) ================= */
static FINDREPLACEW FR; static wchar_t frWhat[256], frWith[256];
static void cmdFind(int replace) {
    if (CURD < 0) return;
    if (hFind) { SetFocus(hFind); return; }
    ZeroMemory(&FR, sizeof FR); FR.lStructSize = sizeof FR; FR.hwndOwner = hMain; FR.lpstrFindWhat = frWhat; FR.wFindWhatLen = 256; FR.lpstrReplaceWith = frWith; FR.wReplaceWithLen = 256; FR.Flags = FR_DOWN;
    { CHARRANGE cr; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr); if (cr.cpMax > cr.cpMin && cr.cpMax - cr.cpMin < 255) { TEXTRANGEW tr = {cr, frWhat}; SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr); } }
    hFind = replace && !DOCS[CURD].ro ? ReplaceTextW(&FR) : FindTextW(&FR);
}
static int findNext(DWORD flags) {
    CHARRANGE cr; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    FINDTEXTEXW ft; ft.lpstrText = frWhat;
    if (flags & FR_DOWN) { ft.chrg.cpMin = cr.cpMax; ft.chrg.cpMax = -1; } else { ft.chrg.cpMin = cr.cpMin; ft.chrg.cpMax = 0; }
    DWORD f = (flags & (FR_DOWN | FR_MATCHCASE | FR_WHOLEWORD));
    LRESULT r = SendMessageW(hEdit, EM_FINDTEXTEXW, f, (LPARAM)&ft);
    if (r < 0 && (flags & FR_DOWN)) { ft.chrg.cpMin = 0; ft.chrg.cpMax = -1; r = SendMessageW(hEdit, EM_FINDTEXTEXW, f, (LPARAM)&ft); }   /* 처음부터 다시 */
    if (r < 0) { status(T(S_NOTFOUND), frWhat); return 0; }
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&ft.chrgText); SendMessageW(hEdit, EM_SCROLLCARET, 0, 0); return 1;
}
static void onFindMsg(FINDREPLACEW *fr) {
    if (fr->Flags & FR_DIALOGTERM) { hFind = NULL; return; }
    if (CURD < 0) return;
    if (fr->Flags & FR_FINDNEXT) findNext(fr->Flags);
    else if ((fr->Flags & FR_REPLACE) && !DOCS[CURD].ro) {
        CHARRANGE cr; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
        wchar_t cur[256] = L""; if (cr.cpMax > cr.cpMin && cr.cpMax - cr.cpMin < 255) { TEXTRANGEW tr = {cr, cur}; SendMessageW(hEdit, EM_GETTEXTRANGE, 0, (LPARAM)&tr); }
        if ((fr->Flags & FR_MATCHCASE) ? !wcscmp(cur, frWhat) : !_wcsicmp(cur, frWhat)) SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)frWith);
        findNext(fr->Flags | FR_DOWN);
    } else if ((fr->Flags & FR_REPLACEALL) && !DOCS[CURD].ro && frWhat[0]) {
        int n = 0; CHARRANGE z = {0, 0}; SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&z);
        for (;;) {
            FINDTEXTEXW ft; ft.lpstrText = frWhat; CHARRANGE cr; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr); ft.chrg.cpMin = cr.cpMax; ft.chrg.cpMax = -1;
            if (SendMessageW(hEdit, EM_FINDTEXTEXW, (fr->Flags & (FR_MATCHCASE | FR_WHOLEWORD)) | FR_DOWN, (LPARAM)&ft) < 0) break;
            SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&ft.chrgText); SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)frWith);
            if (++n > 100000) break;
        }
        status(T(S_REPLACED), n);
    }
}

/* ================= 자동완성 (Ctrl+Space · 별칭 뒤 '.') ================= */
typedef struct { wchar_t *lib, *label, *kind, *detail, *ins, *doc; } AcItem;
static AcItem *ACI; static int NACI;
static int acShown[400], nAcShown, acStart;   /* 보이는 항목 · 고칠 글자가 시작하는 위치 */
static wchar_t acLib[64];
static wchar_t *acField(wchar_t **p) {   /* \t 로 나뉜 칸 하나 (\\n · \\t · \\\\ 를 풂) */
    wchar_t *s = *p, *o = s, *r = s;
    while (*r && *r != L'\t' && *r != L'\n') { if (*r == L'\\' && r[1]) { r++; *o++ = *r == L'n' ? L'\n' : *r == L't' ? L'\t' : *r; r++; } else *o++ = *r++; }
    wchar_t end = *r; *o = 0; *p = end ? r + 1 : r; if (end == L'\n') *p = r; return s;
}
static void acLoad(void) {
    wchar_t p[MAX_PATH]; _snwprintf(p, MAX_PATH, L"%ls\\ac-%ls.txt", NATDIR, LANGCODE[LANG]); p[MAX_PATH - 1] = 0;
    if (!fileExists(p)) { _snwprintf(p, MAX_PATH, L"%ls\\ac-ko.txt", NATDIR); p[MAX_PATH - 1] = 0; }
    DWORD n; char *b = readFileA(p, &n); if (!b) return;
    int m = MultiByteToWideChar(CP_UTF8, 0, b, (int)n, NULL, 0); wchar_t *w = malloc((m + 1) * sizeof(wchar_t)); MultiByteToWideChar(CP_UTF8, 0, b, (int)n, w, m); w[m] = 0; free(b);
    for (wchar_t *q = w; *q; ) {
        wchar_t *ln = q; while (*q && *q != L'\n') q++; int last = !*q; *q = 0; if (!last) q++;
        if (!*ln) continue;
        wchar_t *c = ln; AcItem it; it.lib = acField(&c); it.label = acField(&c); it.kind = acField(&c); it.detail = acField(&c); it.ins = acField(&c); it.doc = acField(&c);
        ACI = realloc(ACI, sizeof(AcItem) * (NACI + 1)); ACI[NACI++] = it;
    }
}
static void acClose(void) { if (hAc && IsWindowVisible(hAc)) ShowWindow(hAc, SW_HIDE); }
static int acOpen(void) { return hAc && IsWindowVisible(hAc); }
/* 지금 파일의 "Use 라이브러리 call 별칭;" 에서 별칭 → 라이브러리 */
static int aliasLib(const wchar_t *alias, int an, wchar_t *lib) {
    int L; wchar_t *w = editTextRE(&L); int found = 0;
    for (wchar_t *p = w; (p = wcsstr(p, L"Use ")) || 0; p += 4) {
        wchar_t *q = p + 4; while (*q == L' ') q++; wchar_t *ls = q; while (iswalnum(*q) || *q == L'_') q++; int ll = (int)(q - ls);
        while (*q == L' ') q++; if (wcsncmp(q, L"call", 4)) continue; q += 4; while (*q == L' ') q++;
        wchar_t *as = q; while (iswalnum(*q) || *q == L'_') q++;
        if ((int)(q - as) == an && !wcsncmp(as, alias, an) && ll < 63) { wcsncpy(lib, ls, ll); lib[ll] = 0; found = 1; break; }
    }
    free(w); return found;
}
static void acFill(const wchar_t *prefix) {
    SendMessageW(hAc, LB_RESETCONTENT, 0, 0); nAcShown = 0; size_t pl = wcslen(prefix);
    for (int pass = 0; pass < 2; pass++)   /* 앞이 맞는 것 먼저 · 그다음 안에 든 것 */
        for (int i = 0; i < NACI && nAcShown < 400; i++) {
            AcItem *a = &ACI[i]; int want = !wcscmp(acLib, L"*") ? (!wcscmp(a->lib, L"jawbreaker") || !wcscmp(a->lib, L"@stmt")) : !wcscmp(a->lib, acLib);
            if (!want) continue;
            int pre = !_wcsnicmp(a->label, prefix, pl), in = !pre && pl && wcsstr(a->label, prefix);
            if (pass == 0 ? !pre : !in) continue;
            int dup = 0; for (int k = 0; k < nAcShown; k++) if (!wcscmp(ACI[acShown[k]].label, a->label) && !wcscmp(ACI[acShown[k]].detail, a->detail)) dup = 1;
            if (dup) continue;
            wchar_t row[300]; _snwprintf(row, 300, L"%ls   %ls", a->label, a->detail); row[299] = 0;
            SendMessageW(hAc, LB_ADDSTRING, 0, (LPARAM)row); acShown[nAcShown++] = i;
        }
    if (!nAcShown) { acClose(); return; }
    SendMessageW(hAc, LB_SETCURSEL, 0, 0);
    AcItem *a = &ACI[acShown[0]]; status(L"%ls %ls — %ls", a->label, a->detail, a->doc);
}
static void acShow(const wchar_t *lib, int start) {
    if (CURD < 0 || DOCS[CURD].ro) return;
    wcsncpy(acLib, lib, 63); acStart = start;
    CHARRANGE cr; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    int L; wchar_t *w = editTextRE(&L); wchar_t pre[128] = L""; int n = cr.cpMax - start; if (n > 0 && n < 127 && start + n <= L) { wcsncpy(pre, w + start, n); pre[n] = 0; } free(w);
    acFill(pre); if (!nAcShown) return;
    POINTL pt; SendMessageW(hEdit, EM_POSFROMCHAR, (WPARAM)&pt, start); POINT p = {pt.x, pt.y + SC(20)}; ClientToScreen(hEdit, &p);
    SetWindowPos(hAc, HWND_TOPMOST, p.x, p.y, SC(460), SC(220), SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
static void acRefilter(void) {
    if (!acOpen()) return;
    CHARRANGE cr; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    if (cr.cpMax < acStart) { acClose(); return; }
    int L; wchar_t *w = editTextRE(&L); wchar_t pre[128] = L""; int n = cr.cpMax - acStart;
    int okp = n >= 0 && n < 127; if (okp) { wcsncpy(pre, w + acStart, n); pre[n] = 0; for (int i = 0; i < n; i++) if (!(iswalnum(pre[i]) || pre[i] == L'_' || pre[i] == L' ')) okp = 0; }
    free(w);
    if (!okp) { acClose(); return; }
    acFill(pre);
}
static void acAccept(void) {
    int k = (int)SendMessageW(hAc, LB_GETCURSEL, 0, 0); if (k < 0 || k >= nAcShown) { acClose(); return; }
    AcItem *a = &ACI[acShown[k]]; acClose();
    CHARRANGE cr; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    /* 지금 줄의 들여쓰기 */
    int line = (int)SendMessageW(hEdit, EM_EXLINEFROMCHAR, 0, acStart), ls = (int)SendMessageW(hEdit, EM_LINEINDEX, line, 0);
    int L; wchar_t *w = editTextRE(&L); wchar_t ind[64]; int ni = 0; for (int i = ls; i < L && ni < 63 && (w[i] == L' ' || w[i] == L'\t'); i++) ind[ni++] = w[i]; ind[ni] = 0; free(w);
    const wchar_t *src = a->ins && a->ins[0] ? a->ins : a->label; size_t sl = wcslen(src);
    wchar_t *out = malloc((sl * (ni + 2) + 2) * sizeof(wchar_t)); int o = 0, caret = -1;
    for (size_t i = 0; i < sl; i++) {
        if (src[i] == L'$' && src[i + 1] == L'0') { caret = o; i++; continue; }
        if (src[i] == L'\n') { out[o++] = L'\r'; for (int q = 0; q < ni; q++) out[o++] = ind[q]; continue; }
        out[o++] = src[i];
    }
    out[o] = 0;
    CHARRANGE rr = {acStart, cr.cpMax}; SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&rr); SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)out);
    if (caret >= 0) { CHARRANGE c2 = {acStart + caret, acStart + caret}; SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&c2); }
    free(out);
    if (a->kind && !wcscmp(a->kind, L"keyword") && src[sl - 1] == L'.') { /* obj. · cam. 다음은 이어서 */ }
}
/* Ctrl+Space: 지금 단어 (와 앞의 "별칭.") 를 보고 목록을 띄움 */
static void acManual(void) {
    if (CURD < 0) return;
    CHARRANGE cr; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    int L; wchar_t *w = editTextRE(&L); int s = cr.cpMax; while (s > 0 && (iswalnum(w[s - 1]) || w[s - 1] == L'_')) s--;
    wchar_t lib[64] = L"*";
    if (s > 0 && w[s - 1] == L'.') { int a = s - 1; while (a > 0 && (iswalnum(w[a - 1]) || w[a - 1] == L'_')) a--; if (!aliasLib(w + a, s - 1 - a, lib)) wcscpy(lib, L"*"); }
    else { int ls = s; while (ls > 0 && w[ls - 1] != L'\r') ls--; while (w[ls] == L' ' || w[ls] == L'\t') ls++; if (!wcsncmp(w + ls, L"Use ", 4) && s == ls + 4 + (int)0) wcscpy(lib, L"@lib"); else if (!wcsncmp(w + ls, L"Use ", 4) && ls + 4 <= s) wcscpy(lib, L"@lib"); }
    free(w);
    acShow(lib, s);
}
/* 글자를 친 뒤: '.' 앞이 별칭이면 그 라이브러리 목록 */
static void acAfterChar(wchar_t ch) {
    if (acOpen()) { acRefilter(); return; }
    if (ch != L'.') return;
    CHARRANGE cr; SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&cr);
    int L; wchar_t *w = editTextRE(&L); int d = cr.cpMax - 1, a = d; while (a > 0 && (iswalnum(w[a - 1]) || w[a - 1] == L'_')) a--;
    wchar_t lib[64]; int ok = d > a && aliasLib(w + a, d - a, lib); free(w);
    if (ok) acShow(lib, cr.cpMax);
}

/* ================= 실행 (run.bat · jdh) — 출력은 로그 칸으로 ================= */
static DWORD WINAPI readerThread(LPVOID arg) {
    HANDLE rd = (HANDLE)arg; char buf[4096]; char carry[8]; int nc = 0; DWORD got;
    while (ReadFile(rd, buf, sizeof buf - 8, &got, NULL) && got) {
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
    /* 자식이 jdh 를 찾도록 PATH 앞에 bin 폴더 · 언어 */
    static wchar_t envPath[32768]; static wchar_t old[30000]; DWORD on = GetEnvironmentVariableW(L"PATH", old, 30000); if (on >= 30000) on = 0; old[on] = 0;
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
    enableButtons();
    HANDLE th = CreateThread(NULL, 0, readerThread, rd, 0, NULL); if (th) CloseHandle(th);
    return 1;
}
static int jdhPath(wchar_t *out) { _snwprintf(out, MAX_PATH, L"%ls\\jdh.exe", BINDIR); out[MAX_PATH - 1] = 0; if (!fileExists(out)) { logLine(L"%ls", T(S_NOJDH)); return 0; } return 1; }
static int prep(void) { if (!PROJ[0]) { status(L"%ls", T(S_NOPROJ)); return 0; } acClose(); return saveAll(); }
static void runJdhCmd(const wchar_t *args, const wchar_t *label) {
    if (!prep()) return;
    wchar_t jdh[MAX_PATH], cmd[MAX_PATH * 3]; if (!jdhPath(jdh)) return;
    _snwprintf(cmd, MAX_PATH * 3, L"\"%ls\" %ls \"%ls\"", jdh, args, PROJ); cmd[MAX_PATH * 3 - 1] = 0;
    startChild(cmd, label);
}
static void cmdRun(void) {
    if (!prep()) return;
    wchar_t bat[MAX_PATH], cmd[MAX_PATH * 3], cmdexe[MAX_PATH];
    _snwprintf(bat, MAX_PATH, L"%ls\\run.bat", PROJ); bat[MAX_PATH - 1] = 0;
    if (!fileExists(bat)) { runJdhCmd(L"run", L"jdh run"); return; }
    GetSystemDirectoryW(cmdexe, MAX_PATH); wcscat(cmdexe, L"\\cmd.exe");
    _snwprintf(cmd, MAX_PATH * 3, L"\"%ls\" /c \"\"%ls\"\"", cmdexe, bat); cmd[MAX_PATH * 3 - 1] = 0;
    startChild(cmd, L"run.bat");
}
static void cmdHtml(void) { wchar_t a[64]; _snwprintf(a, 64, L"build html --lang %ls", LANGCODE[LANG]); a[63] = 0; runJdhCmd(a, L"jdh build html"); }
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
    if (dirExists(file) || fileExists(file)) { msgf(MB_ICONWARNING, T(S_EXISTS), file); return; }
    wchar_t jdh[MAX_PATH], cmd[MAX_PATH * 3]; if (!jdhPath(jdh)) return;
    _snwprintf(cmd, MAX_PATH * 3, L"\"%ls\" new project \"%ls\"", jdh, file); cmd[MAX_PATH * 3 - 1] = 0;
    STARTUPINFOW si; ZeroMemory(&si, sizeof si); si.cb = sizeof si; PROCESS_INFORMATION pi; ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessW(NULL, cmd, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi)) { logLine(T(S_FAILRUN), cmd); return; }
    WaitForSingleObject(pi.hProcess, 30000); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    openProject(file);
}
/* 로그 칸의 "✖ 파일:줄" 을 두 번 누르면 그 파일 · 그 줄로 */
static int logJump(int line) {
    wchar_t buf[2048]; *(WORD*)buf = 2040; int n = (int)SendMessageW(hLog, EM_GETLINE, line, (LPARAM)buf); if (n <= 0) return 0; buf[n] = 0;
    wchar_t *p = wcsstr(buf, L"✖"); if (!p) return 0; p++; while (*p == L' ') p++;
    wchar_t *colon = NULL; for (wchar_t *q = p; *q; q++) if (*q == L':' && iswdigit(q[1])) { wchar_t *e = q + 1; while (iswdigit(*e)) e++; if (*e == L' ' || *e == 0 || *e == L'\r') { colon = q; break; } }
    if (!colon || colon == p) return 0;
    int ln = _wtoi(colon + 1); *colon = 0;
    wchar_t full[MAX_PATH]; for (wchar_t *c = p; *c; c++) if (*c == L'/') *c = L'\\';
    if (p[1] == L':') wcsncpy(full, p, MAX_PATH - 1); else _snwprintf(full, MAX_PATH, L"%ls\\%ls", PROJ, p);
    full[MAX_PATH - 1] = 0;
    if (!fileExists(full)) return 0;
    if (openFile(full, 0) < 0) return 0;
    gotoLine(ln); return 1;
}

/* ================= 창 ================= */
static void layout(void) {
    RECT r; GetClientRect(hMain, &r);
    int W = r.right, H = r.bottom, bh = SC(32), sh = SC(22), th = SC(26), tw = W / 5 < SC(180) ? SC(180) : W / 5, lh = H / 4 < SC(120) ? SC(120) : H / 4;
    for (int i = 0; i < B_N; i++) MoveWindow(hBtn[i], SC(4) + i * SC(108), SC(3), SC(104), bh - SC(6), TRUE);
    MoveWindow(hStatus, 0, H - sh, W, sh, TRUE);
    MoveWindow(hTree, 0, bh, tw, H - bh - sh, TRUE);
    MoveWindow(hTab, tw + 3, bh, W - tw - 3, th, TRUE);
    MoveWindow(hEdit, tw + 3, bh + th, W - tw - 3, H - bh - th - sh - lh - 3, TRUE);
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
/* --selftest: 열기 → 검사 → 실행 → HTML 만들기 → 자동완성 → 설명서 → 끝 (차례대로 · 자식이 끝날 때마다 다음) */
static int stStep;
static void selftestNext(void) {
    wchar_t p[MAX_PATH + 8];
    switch (stStep++) {
    case 0: logLine(L"[selftest] check"); runJdhCmd(L"check", L"jdh check"); return;
    case 1: logLine(L"[selftest] run"); cmdRun(); return;
    case 2: logLine(L"[selftest] html"); cmdHtml(); return;
    case 3: {   /* 자동완성: 끝에 "jaw." 를 치고 목록을 띄움 */
        int L = GetWindowTextLengthW(hEdit); CHARRANGE cr = {L, L}; SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"\r// jaw."); acAfterChar(L'.');
        logLine(L"[selftest] ac items=%d first=%ls", nAcShown, nAcShown ? ACI[acShown[0]].label : L"-");
        _snwprintf(p, MAX_PATH + 8, L"%ls-ac.bmp", SELFOUT); p[MAX_PATH + 7] = 0; saveShot(p);
        acClose(); SendMessageW(hEdit, EM_UNDO, 0, 0); DOCS[CURD].dirty = 0;
        /* 설명서 */
        if (NDOC) { int k = openFile(DOCP[0], 1); logLine(L"[selftest] doc %ls lines=%d ro=%d", baseName(DOCP[0]), (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0), k >= 0 ? DOCS[k].ro : -1); }
        logLine(L"[selftest] tabs=%d", NDOCS);
        _snwprintf(p, MAX_PATH + 8, L"%ls.bmp", SELFOUT); p[MAX_PATH + 7] = 0; saveShot(p);
        int Lg = GetWindowTextLengthW(hLog); wchar_t *w = malloc((Lg + 1) * sizeof(wchar_t)); GetWindowTextW(hLog, w, Lg + 1);
        DWORD m; char *b = wideToUtf8(w, &m); _snwprintf(p, MAX_PATH + 8, L"%ls.log", SELFOUT); p[MAX_PATH + 7] = 0; writeFileA(p, b, m); free(w); free(b);
        for (int i = 0; i < NDOCS; i++) DOCS[i].dirty = 0;
        DestroyWindow(hMain); return; }
    }
}
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_FINDMSG && WM_FINDMSG) { onFindMsg((FINDREPLACEW*)lp); return 0; }
    switch (msg) {
    case WM_SIZE: layout(); return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= ID_BTN && id < ID_BTN + B_N) {
            switch (id - ID_BTN) {
            case B_NEWP: cmdNewProject(); break; case B_OPEN: cmdOpen(); break; case B_SAVE: saveAll(); break;
            case B_CHECK: runJdhCmd(L"check", L"jdh check"); break; case B_RUN: cmdRun(); break; case B_STOP: cmdStop(); break;
            case B_EXE: runJdhCmd(L"build exe", L"jdh build exe"); break; case B_HTML: cmdHtml(); break;
            }
            return 0;
        }
        switch (id) {
        case ID_M_NEWFILE: cmdNewFile(0); return 0;
        case ID_M_NEWDIR: cmdNewFile(1); return 0;
        case ID_M_RENAME: cmdRename(); return 0;
        case ID_M_DELETE: cmdDelete(); return 0;
        case ID_M_EXPLORE: { wchar_t d[MAX_PATH]; menuDir(d); ShellExecuteW(hMain, L"explore", d, NULL, NULL, SW_SHOWNORMAL); return 0; }
        case ID_EDIT:
            if (HIWORD(wp) == EN_CHANGE && !COLORING && CURD >= 0) { if (!DOCS[CURD].dirty) { DOCS[CURD].dirty = 1; tabLabel(CURD); } SetTimer(h, TIMER_COLOR, 250, NULL); }
            return 0;
        case ID_AC: if (HIWORD(wp) == LBN_DBLCLK) acAccept(); else if (HIWORD(wp) == LBN_SELCHANGE) { int k = (int)SendMessageW(hAc, LB_GETCURSEL, 0, 0); if (k >= 0 && k < nAcShown) { AcItem *a = &ACI[acShown[k]]; status(L"%ls %ls — %ls", a->label, a->detail, a->doc); } } return 0;
        }
        break;
    }
    case WM_TIMER: if (wp == TIMER_COLOR) { KillTimer(h, TIMER_COLOR); recolor(); } return 0;
    case WM_NOTIFY: {
        NMHDR *nh = (NMHDR*)lp;
        if (nh->idFrom == ID_TREE && nh->code == TVN_SELCHANGEDW) {
            NMTREEVIEWW *tv = (NMTREEVIEWW*)lp; int k = (int)tv->itemNew.lParam;
            if (k >= 0 && k < NTP && fileExists(TPATHS[k])) openFile(TPATHS[k], 0);
            else if (k < 0 && k != LP_DOCROOT && -k - 1 < NDOC) openFile(DOCP[-k - 1], 1);
        } else if (nh->idFrom == ID_TREE && nh->code == NM_RCLICK) { treeMenu(); return 1; }
        else if (nh->idFrom == ID_TAB && nh->code == TCN_SELCHANGE) { int k = (int)SendMessageW(hTab, TCM_GETCURSEL, 0, 0); if (k != CURD) { docStash(); docShow(k); } }
        return 0;
    }
    case WM_APP_OUT: { wchar_t *w = (wchar_t*)lp; logAppend(w); free(w); return 0; }
    case WM_APP_END: {
        logLine(T(S_DONE), (unsigned long)wp, (GetTickCount() - childStart) / 1000.0);
        if (childJob) { CloseHandle(childJob); childJob = NULL; }
        if (childProc) { CloseHandle(childProc); childProc = NULL; }
        enableButtons(); treeReload();
        if (SELFTEST) selftestNext();
        return 0;
    }
    case WM_ACTIVATE: if (LOWORD(wp) == WA_INACTIVE) acClose(); break;
    case WM_CLOSE: if (!askSaveAll()) return 0; cmdStop(); DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
static void createUI(HINSTANCE hi) {
    LoadLibraryW(L"Msftedit.dll");
    { HDC dc = GetDC(NULL); DPI = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc); if (DPI < 96) DPI = 96; }
    INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_TAB_CLASSES}; InitCommonControlsEx(&ic);
    WM_FINDMSG = RegisterWindowMessageW(FINDMSGSTRINGW);
    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); wc.lpszClassName = L"JawBreakerDev"; wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    hMain = CreateWindowExW(0, L"JawBreakerDev", L"JawBreaker Dev.", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, SC(1280), SC(820), NULL, NULL, hi, NULL);
    NONCLIENTMETRICSW nm = {sizeof nm}; SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof nm, &nm, 0); hFontUI = CreateFontIndirectW(&nm.lfMessageFont);
    hFontCode = CreateFontW(-SC(15), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, L"Consolas");
    const int sk[B_N] = {S_NEWP, S_OPEN, S_SAVE, S_CHECK, S_RUN, S_STOP, S_EXE, S_HTML};
    for (int i = 0; i < B_N; i++) { hBtn[i] = CreateWindowExW(0, L"BUTTON", T(sk[i]), WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 10, 10, hMain, (HMENU)(INT_PTR)(ID_BTN + i), hi, NULL); SendMessageW(hBtn[i], WM_SETFONT, (WPARAM)hFontUI, TRUE); }
    hTree = CreateWindowExW(WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | TVS_HASLINES | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS, 0, 0, 10, 10, hMain, (HMENU)ID_TREE, hi, NULL);
    SendMessageW(hTree, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    hTab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_FOCUSNEVER, 0, 0, 10, 10, hMain, (HMENU)ID_TAB, hi, NULL);
    SendMessageW(hTab, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL | ES_WANTRETURN, 0, 0, 10, 10, hMain, (HMENU)ID_EDIT, hi, NULL);
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFontCode, TRUE); SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE); SendMessageW(hEdit, EM_EXLIMITTEXT, 0, 64 * 1024 * 1024);
    SendMessageW(hEdit, EM_SETTEXTMODE, TM_PLAINTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);
    { PARAFORMAT2 pf; ZeroMemory(&pf, sizeof pf); pf.cbSize = sizeof pf; pf.dwMask = PFM_TABSTOPS; pf.cTabCount = 32; for (int i = 0; i < 32; i++) pf.rgxTabs[i] = (i + 1) * 4 * 120; SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf); }
    SendMessageW(hEdit, EM_SETREADONLY, TRUE, 0);
    hLog = CreateWindowExW(WS_EX_CLIENTEDGE, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 10, 10, hMain, (HMENU)ID_LOG, hi, NULL);
    SendMessageW(hLog, WM_SETFONT, (WPARAM)hFontCode, TRUE); SendMessageW(hLog, EM_EXLIMITTEXT, 0, 16 * 1024 * 1024);
    hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hMain, (HMENU)ID_STATUS, hi, NULL);
    SendMessageW(hStatus, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    hAc = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"LISTBOX", L"", WS_POPUP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, 0, 0, 10, 10, hMain, NULL, hi, NULL);
    SetWindowLongPtrW(hAc, GWLP_ID, ID_AC);
    SendMessageW(hAc, WM_SETFONT, (WPARAM)hFontCode, TRUE);
    enableButtons(); layout(); setTitle(); status(L"%ls", T(S_NOPROJ));
}
/* 설명서 · 자동완성 목록이 있는 곳: 설치본 = bin\..\editor\native · 개발 중 = 저장소의 editor\native */
static void findNatDir(void) {
    const wchar_t *cand[] = {L"%ls\\..\\editor\\native", L"%ls\\editor\\native", L"%ls\\..\\..\\editor\\native", NULL};
    for (int i = 0; cand[i]; i++) { wchar_t p[MAX_PATH], f[MAX_PATH]; _snwprintf(p, MAX_PATH, cand[i], BINDIR); p[MAX_PATH - 1] = 0; if (dirExists(p) && GetFullPathNameW(p, MAX_PATH, f, NULL)) { wcscpy(NATDIR, f); return; } }
    NATDIR[0] = 0;
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int show) {
    (void)hp; (void)cl;
    pickLang();
    wchar_t exe[MAX_PATH]; DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) { fail(L"프로그램 위치를 알 수 없습니다."); return 1; }
    wcscpy(BINDIR, exe); { wchar_t *sl = wcsrchr(BINDIR, L'\\'); if (sl) *sl = 0; }
    findNatDir();
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
    if (NATDIR[0]) { findDocs(); acLoad(); }
    createUI(hi);
    ShowWindow(hMain, SELFTEST ? SW_SHOWNORMAL : show); UpdateWindow(hMain);
    treeReload();
    wchar_t last[MAX_PATH];
    if (openArg) openProject(openArg);
    else if (lastProj(last) && dirExists(last)) openProject(last);
    if (SELFTEST) { if (!PROJ[0]) { DestroyWindow(hMain); return 2; } logLine(L"[selftest] natdir=%ls docs=%d ac=%d", NATDIR, NDOC, NACI); selftestNext(); }
    MSG m;
    while (GetMessageW(&m, NULL, 0, 0)) {
        if (hFind && IsDialogMessageW(hFind, &m)) continue;
        if (m.message == WM_KEYDOWN) {
            int ctrl = GetKeyState(VK_CONTROL) < 0;
            if (acOpen() && m.hwnd == hEdit) {
                int k = (int)SendMessageW(hAc, LB_GETCURSEL, 0, 0);
                if (m.wParam == VK_DOWN) { SendMessageW(hAc, LB_SETCURSEL, k + 1 < nAcShown ? k + 1 : k, 0); SendMessageW(hMain, WM_COMMAND, MAKEWPARAM(ID_AC, LBN_SELCHANGE), 0); continue; }
                if (m.wParam == VK_UP) { SendMessageW(hAc, LB_SETCURSEL, k > 0 ? k - 1 : 0, 0); SendMessageW(hMain, WM_COMMAND, MAKEWPARAM(ID_AC, LBN_SELCHANGE), 0); continue; }
                if (m.wParam == VK_RETURN || m.wParam == VK_TAB) { acAccept(); continue; }
                if (m.wParam == VK_ESCAPE) { acClose(); continue; }
            }
            if (m.wParam == VK_F5) { cmdRun(); continue; }
            if (m.wParam == VK_F7) { runJdhCmd(L"check", L"jdh check"); continue; }
            if (ctrl && m.wParam == 'S') { saveAll(); continue; }
            if (ctrl && m.wParam == 'O') { cmdOpen(); continue; }
            if (ctrl && m.wParam == 'F') { cmdFind(0); continue; }
            if (ctrl && m.wParam == 'H') { cmdFind(1); continue; }
            if (ctrl && m.wParam == 'W') { closeDoc(CURD); continue; }
            if (ctrl && m.wParam == VK_SPACE && m.hwnd == hEdit) { acManual(); continue; }
            if (m.wParam == VK_F3 && frWhat[0]) { findNext(GetKeyState(VK_SHIFT) < 0 ? 0 : FR_DOWN); continue; }
            if (ctrl && m.wParam == VK_TAB && NDOCS > 1) { int nx = (CURD + (GetKeyState(VK_SHIFT) < 0 ? NDOCS - 1 : 1)) % NDOCS; docStash(); docShow(nx); continue; }
            if (m.wParam == VK_TAB && m.hwnd == hEdit && !ctrl) { SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"    "); continue; }   /* 탭 = 빈칸 4개 */
        }
        if (m.message == WM_CHAR && m.hwnd == hEdit && m.wParam == L' ' && GetKeyState(VK_CONTROL) < 0) continue;   /* Ctrl+Space 의 빈칸은 넣지 않음 */
        if (m.message == WM_MBUTTONUP && m.hwnd == hTab) {   /* 가운데 단추 = 탭 닫기 */
            TCHITTESTINFO ht = {{GET_X_LPARAM(m.lParam), GET_Y_LPARAM(m.lParam)}, 0}; int k = (int)SendMessageW(hTab, TCM_HITTEST, 0, (LPARAM)&ht); if (k >= 0) closeDoc(k); continue;
        }
        if ((m.message == WM_LBUTTONDOWN || m.message == WM_MOUSEWHEEL) && m.hwnd != hAc) acClose();
        TranslateMessage(&m); DispatchMessageW(&m);
        if (m.message == WM_CHAR && m.hwnd == hEdit) acAfterChar((wchar_t)m.wParam);
        if (m.message == WM_KEYDOWN && m.hwnd == hEdit && (m.wParam == VK_BACK || m.wParam == VK_DELETE)) acRefilter();
        if (m.message == WM_LBUTTONDBLCLK && m.hwnd == hLog) { CHARRANGE cr; SendMessageW(hLog, EM_EXGETSEL, 0, (LPARAM)&cr); logJump((int)SendMessageW(hLog, EM_EXLINEFROMCHAR, 0, cr.cpMin)); }
    }
    LocalFree(argv);
    return 0;
}
