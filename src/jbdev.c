/* jbdev — JawBreaker Dev. 를 전용 창으로 엶 (Windows)
 *   Microsoft Edge 의 앱 창 (주소창 · 탭 없음) 으로 editor\jbdev.html 을 띄움.
 *   저장 공간은 %APPDATA%\JDH\dev-profile — 평소 쓰는 브라우저와 따로.
 *   Edge 를 찾지 못하면 기본 브라우저로 엶.
 */
#include <windows.h>
#include <shellapi.h>
#include <wchar.h>

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

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cl, int show) {
    (void)hi; (void)hp; (void)cl; (void)show;
    wchar_t exe[MAX_PATH], dir[MAX_PATH], html[MAX_PATH];
    DWORD n = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (!n || n >= MAX_PATH) { fail(L"프로그램 위치를 알 수 없습니다."); return 1; }
    wcscpy(dir, exe);
    wchar_t *sl = wcsrchr(dir, L'\\'); if (sl) *sl = 0;
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
