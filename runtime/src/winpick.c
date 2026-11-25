/* Windows 파일 고르기 창 (jaw.exportFile = 저장 창 · jaw.openFile = 열기 창) — 편집기의 내려받기 · 파일 고르기와 같은 역할
   windows.h 의 S_OK 등이 실행기의 이름과 겹쳐서 따로 둔 파일 */
#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#include <string.h>

int jb_win_pick(int save, const char *defName, char *out, size_t outn) {
    wchar_t buf[4096] = L"";
    if (defName && *defName) MultiByteToWideChar(CP_UTF8, 0, defName, -1, buf, 4096);
    OPENFILENAMEW o; memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = GetActiveWindow();
    o.lpstrFile = buf; o.nMaxFile = 4096;
    o.lpstrFilter = L"Jawbreaker (*.jbx;*.json)\0*.jbx;*.json\0*.*\0*.*\0\0";
    o.lpstrDefExt = save ? L"jbx" : NULL;
    o.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | (save ? OFN_OVERWRITEPROMPT : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST));
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    BOOL ok = save ? GetSaveFileNameW(&o) : GetOpenFileNameW(&o);
    if (!ok) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, buf, -1, out, (int)outn, NULL, NULL) > 0;
}
#else
typedef int jb_winpick_unused;
#endif
