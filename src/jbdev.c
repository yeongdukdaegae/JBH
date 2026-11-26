/* jbdev — JawBreaker Dev. 를 전용 창으로 엶 (Windows)
 *   Microsoft Edge 의 앱 창 (주소창 · 탭 없음) 으로 editor\jbdev.html 을 띄움.
 *   저장 공간은 %APPDATA%\JDH\dev-profile — 평소 쓰는 브라우저와 따로.
 *   Edge 를 찾지 못하면 기본 브라우저로 엶.
 *   열기 전에 새 버전이 있는지 확인 (jdh update --check · 5초 한도) → 있으면 "업데이트하시겠습니까?" 를 물음.
 *     예 = jdh update --yes (바뀐 파일만 받아 SHA-256 으로 검사한 뒤 바꿈) · 아니요 = 그 버전은 다시 묻지 않음 · 취소 = 다음에 다시 물음
 *     JDH_NO_UPDATE=1 이면 확인하지 않음.
 */
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>
#include <string.h>

static int fileExists(const wchar_t *p) { DWORD a = GetFileAttributesW(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
static void fail(const wchar_t *msg) { MessageBoxW(NULL, msg, L"JawBreaker Dev.", MB_OK | MB_ICONERROR); }

/* 레지스트리의 App Paths 에서 msedge.exe 위치 */
static int edgeFromReg(HKEY root, wchar_t *out, DWORD cch) {
    HKEY k; DWORD type = 0, cb = cch * sizeof(wchar_t);
    if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\App Paths\\msedge.exe", 0, KEY_READ, &k) != ERROR_SUCCESS) return 0;
    LONG r = RegQueryValueExW(k, NULL, NULL, &type, (LPBYTE)out, &cb);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_SZ) return 0;
    out[cch - 1] = 0;
    return fileExists(out);
}
static int edgeFromEnv(const wchar_t *var, wchar_t *out, DWORD cch) {
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(var, base, MAX_PATH);
    if (!n || n >= MAX_PATH) return 0;
    if (_snwprintf(out, cch, L"%ls\\Microsoft\\Edge\\Application\\msedge.exe", base) < 0) return 0;
    out[cch - 1] = 0;
    return fileExists(out);
}
/* 경로 → file:/// 주소 (UTF-8 로 바꿔 안전한 글자 말고는 %XX) */
static void fileUrl(const wchar_t *path, wchar_t *out, size_t cch) {
    char u8[MAX_PATH * 4]; int n = WideCharToMultiByte(CP_UTF8, 0, path, -1, u8, sizeof u8, NULL, NULL);
    size_t w = 0; const wchar_t *pre = L"file:///";
    for (; *pre && w + 1 < cch; pre++) out[w++] = *pre;
    for (int i = 0; i < n - 1 && w + 4 < cch; i++) {
        unsigned char c = (unsigned char)u8[i];
        if (c == '\\') out[w++] = L'/';
        else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~' || c == '/' || c == ':') out[w++] = (wchar_t)c;
        else { static const wchar_t H[] = L"0123456789ABCDEF"; out[w++] = L'%'; out[w++] = H[c >> 4]; out[w++] = H[c & 15]; }
    }
    out[w] = 0;
}

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

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int show) {
    (void)hi; (void)hp; (void)show;
    wchar_t exe[MAX_PATH], dir[MAX_PATH], html[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) { fail(L"프로그램 위치를 알 수 없습니다."); return 1; }
    wcscpy(dir, exe);
    wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
    /* 새 버전 확인 — 업데이트했으면 (이 파일도 바뀌었을 수 있으니) 새 jbdev.exe 로 다시 시작 */
    if (!strstr(cl ? cl : "", "--no-update") && checkUpdate(dir)) {
        static wchar_t again[MAX_PATH + 32]; _snwprintf(again, MAX_PATH + 32, L"\"%ls\" --no-update", exe); again[MAX_PATH + 31] = 0;
        STARTUPINFOW s2; PROCESS_INFORMATION p2; ZeroMemory(&s2, sizeof s2); s2.cb = sizeof s2; ZeroMemory(&p2, sizeof p2);
        if (CreateProcessW(exe, again, NULL, NULL, FALSE, 0, NULL, NULL, &s2, &p2)) { CloseHandle(p2.hThread); CloseHandle(p2.hProcess); return 0; }
    }
    /* 설치 폴더: bin\jbdev.exe · editor\jbdev.html */
    _snwprintf(html, MAX_PATH, L"%ls\\..\\editor\\jbdev.html", dir); html[MAX_PATH - 1] = 0;
    if (!fileExists(html)) { _snwprintf(html, MAX_PATH, L"%ls\\editor\\jbdev.html", dir); html[MAX_PATH - 1] = 0; }
    if (!fileExists(html)) { fail(L"editor\\jbdev.html 을 찾을 수 없습니다. JDH 를 다시 설치해 주세요."); return 1; }
    wchar_t full[MAX_PATH];
    if (!GetFullPathNameW(html, MAX_PATH, full, NULL)) wcscpy(full, html);

    wchar_t edge[MAX_PATH];
    int have = edgeFromReg(HKEY_LOCAL_MACHINE, edge, MAX_PATH) || edgeFromReg(HKEY_CURRENT_USER, edge, MAX_PATH)
            || edgeFromEnv(L"ProgramFiles(x86)", edge, MAX_PATH) || edgeFromEnv(L"ProgramFiles", edge, MAX_PATH)
            || edgeFromEnv(L"LOCALAPPDATA", edge, MAX_PATH);
    if (!have) {   /* Edge 가 없으면 기본 브라우저로 */
        if ((INT_PTR)ShellExecuteW(NULL, L"open", full, NULL, NULL, SW_SHOWNORMAL) <= 32) { fail(L"JawBreaker Dev. 를 열 수 없습니다."); return 1; }
        return 0;
    }
    /* 저장 공간: %APPDATA%\JDH\dev-profile */
    wchar_t app[MAX_PATH], prof[MAX_PATH];
    DWORD an = GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH);
    if (!an || an >= MAX_PATH) { fail(L"사용자 폴더(APPDATA)를 찾을 수 없습니다."); return 1; }
    _snwprintf(prof, MAX_PATH, L"%ls\\JDH", app); prof[MAX_PATH - 1] = 0; CreateDirectoryW(prof, NULL);
    _snwprintf(prof, MAX_PATH, L"%ls\\JDH\\dev-profile", app); prof[MAX_PATH - 1] = 0; CreateDirectoryW(prof, NULL);

    wchar_t url[MAX_PATH * 12]; fileUrl(full, url, sizeof url / sizeof url[0]);
    static wchar_t cmd[MAX_PATH * 16];
    _snwprintf(cmd, sizeof cmd / sizeof cmd[0], L"\"%ls\" --app=\"%ls\" --user-data-dir=\"%ls\" --no-first-run --no-default-browser-check --window-size=1280,820", edge, url, prof);
    cmd[sizeof cmd / sizeof cmd[0] - 1] = 0;
    STARTUPINFOW si; PROCESS_INFORMATION pi; ZeroMemory(&si, sizeof si); si.cb = sizeof si; ZeroMemory(&pi, sizeof pi);
    if (!CreateProcessW(edge, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) { fail(L"JawBreaker Dev. 창을 열 수 없습니다."); return 1; }
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    return 0;
}
