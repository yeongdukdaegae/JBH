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
#define JDH_VER_W L"0.8.1"   /* src/jdh.c 의 JDH_VERSION 과 같게 */

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
typedef struct { int lang, ram, mono, askRun; } Cfg;
static Cfg CFG = {-1, 0, 0, 0};
enum { S_NEWP, S_OPEN, S_SAVE, S_CHECK, S_RUN, S_STOP, S_EXE, S_HTML, S_NOPROJ, S_NOTPROJ, S_ASKSAVE, S_BINARY, S_RUNNING, S_DONE, S_FAILRUN, S_PICKDIR,
       S_NEWNAME, S_SAVED, S_STOPPED, S_BUSY, S_NOJDH, S_EXISTS, S_TITLE_NONE, S_DOCS, S_NEWFILE, S_NEWDIR, S_RENAME, S_DELETE, S_EXPLORE,
       S_ASKDEL, S_NAME, S_OKB, S_CANCELB, S_NOTFOUND, S_REPLACED, S_READONLY, S_BADNAME, S_FAILFILE,
       S_M_HTML, S_M_EXE, S_M_APK, S_RUNASK, S_SETTINGS, S_SET_TITLE, S_SET_LANG, S_SET_RAM, S_RAM_AUTO, S_RAM_UNL, S_RAM_HINT, S_SET_UI, S_UI_COLOR, S_UI_MONO, S_CLOSE, S_APK_TITLE, S_APK_DESC, S_APK_LABEL, S_APK_PKG, S_APK_VCODE, S_APK_VNAME, S_APK_ORI, S_ORI_AUTO, S_ORI_LAND, S_ORI_PORT, S_APK_ICON, S_ICON_DEF, S_KEY_HAVE, S_KEY_NONE, S_KEY_EXPORT, S_KEY_IMPORT, S_APK_BUILD, S_APK_BADPKG, S_APK_BADVC, S_KEY_DONE, S_KEY_FAIL, S_KEY_SAVED, S_LOG, S_CLEAR, S_PROJECT, S_ADDFILE, S_ADDDIR, S_UNDO, S_REDO, S_SAVETIP, S_FILESTIP, S_MENUTIP, S_ASKRUN, S_VERSION, S_N };
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
    {L"파일 작업에 실패했습니다: %ls", L"File operation failed: %ls", L"ファイル操作に失敗しました: %ls", L"文件操作失败: %ls", L"檔案操作失敗: %ls"},    {L"HTML로 내보내기 (실행 파일)", L"Export as HTML (runnable)", L"HTML でエクスポート (実行ファイル)", L"导出为 HTML (可运行文件)", L"匯出為 HTML (可執行檔案)"},
    {L"EXE로 변환 (Windows)", L"Convert to EXE (Windows)", L"EXE に変換 (Windows)", L"转为 EXE (Windows)", L"轉為 EXE (Windows)"},
    {L"APK로 변환 (Android)", L"Convert to APK (Android)", L"APK に変換 (Android)", L"转为 APK (Android)", L"轉為 APK (Android)"},
    {L"실행 전 저장 묻기", L"Ask to save before run", L"実行前に保存を確認", L"运行前询问是否保存", L"執行前詢問是否儲存"},
    {L"설정 / Settings", L"Settings / 설정", L"設定 / Settings", L"设置 / Settings", L"設定 / Settings"},
    {L"⚙ 설정", L"⚙ Settings", L"⚙ 設定", L"⚙ 设置", L"⚙ 設定"},
    {L"언어", L"Language", L"言語", L"语言", L"語言"},
    {L"램 한도", L"RAM limit", L"RAM 上限", L"内存上限", L"記憶體上限"},
    {L"자동 (기기에 맞춤)", L"Auto (fits this device)", L"自動 (端末に合わせる)", L"自动 (按设备调整)", L"自動 (按裝置調整)"},
    {L"무제한", L"Unlimited", L"無制限", L"无限制", L"無限制"},
    {L"게임 실행 중 풀어 둔 이미지 · 소리 · 글자 그림이 쓰는 램의 상한입니다. 넘으면 오래 안 쓴 것부터 비우고, 다시 쓰일 때 다시 읽습니다. HTML · APK 로 내보낸 게임에도 들어갑니다 (EXE 제외).", L"Caps the RAM used by decoded images · sounds · text bitmaps while a game runs. When it is exceeded, the least recently used ones are cleared and reloaded when needed. It is also built into games exported as HTML · APK (not EXE).", L"ゲーム実行中に展開した画像 · サウンド · 文字画像が使う RAM の上限です。超えると長く使っていないものから解放し、再び使うときに読み込み直します。HTML · APK でエクスポートしたゲームにも適用されます (EXE は除く)。", L"游戏运行时已解码的图片 · 声音 · 文本位图所占内存的上限。超过后会先释放最久未使用的内容，再次用到时重新读取。导出为 HTML · APK 的游戏也会带上此设置 (EXE 除外)。", L"遊戲執行時已解碼的圖片 · 聲音 · 文字點陣圖所佔記憶體的上限。超過後會先釋放最久未使用的內容，再次用到時重新讀取。匯出為 HTML · APK 的遊戲也會帶上此設定 (EXE 除外)。"},
    {L"UI", L"UI", L"UI", L"UI", L"UI"},
    {L"컬러", L"Color", L"カラー", L"彩色", L"彩色"},
    {L"흑백", L"Black & white", L"白黒", L"黑白", L"黑白"},
    {L"닫기", L"Close", L"閉じる", L"关闭", L"關閉"},
    {L"🤖 APK로 변환 (Android)", L"🤖 Convert to APK (Android)", L"🤖 APK に変換 (Android)", L"🤖 转为 APK (Android)", L"🤖 轉為 APK (Android)"},
    {L"Android 7.0 이상 · 설치할 때 \"출처를 알 수 없는 앱\" 허용이 필요합니다.\n같은 앱을 업데이트하려면 같은 패키지 이름 · 같은 서명 키 · 더 큰 버전 번호로 만드세요.", L"Android 7.0+ · installing needs \"unknown apps\" allowed.\nTo update an installed app, build with the same package name, the same signing key and a larger version code.", L"Android 7.0 以上 · インストール時に「提供元不明のアプリ」の許可が必要です。\n同じアプリをアップデートするには、同じパッケージ名 · 同じ署名鍵 · より大きいバージョンコードでビルドしてください。", L"Android 7.0 及以上 · 安装时需要允许“未知来源的应用”。\n要更新同一个应用，请使用相同的包名 · 相同的签名密钥 · 更大的版本号来构建。", L"Android 7.0 及以上 · 安裝時需要允許“未知來源的應用”。\n要更新同一個應用，請使用相同的套件名稱 · 相同的簽名金鑰 · 更大的版本號來建置。"},
    {L"앱 이름", L"App name", L"アプリ名", L"应用名称", L"應用名稱"},
    {L"패키지 이름", L"Package name", L"パッケージ名", L"包名", L"套件名稱"},
    {L"버전 번호 (업데이트마다 더 큰 수)", L"Version code (larger for every update)", L"バージョンコード (アップデートのたびに大きい数)", L"版本号 (每次更新都要更大)", L"版本號 (每次更新都要更大)"},
    {L"버전 이름", L"Version name", L"バージョン名", L"版本名称", L"版本名稱"},
    {L"화면 방향", L"Orientation", L"画面の向き", L"屏幕方向", L"螢幕方向"},
    {L"자동 회전", L"Auto-rotate", L"自動回転", L"自动旋转", L"自動旋轉"},
    {L"가로", L"Landscape", L"横向き", L"横屏", L"橫向"},
    {L"세로", L"Portrait", L"縦向き", L"竖屏", L"直向"},
    {L"아이콘 (프로젝트의 PNG 그림)", L"Icon (a PNG image in the project)", L"アイコン (プロジェクト内の PNG 画像)", L"图标 (项目中的 PNG 图片)", L"圖示 (專案中的 PNG 圖片)"},
    {L"기본 아이콘", L"Default icon", L"デフォルトのアイコン", L"默认图标", L"預設圖示"},
    {L"🔑 서명 키: 이 PC 에 저장됨 (%ls)", L"🔑 Signing key: saved on this PC (%ls)", L"🔑 署名鍵: この PC に保存済み (%ls)", L"🔑 签名密钥: 已保存在此电脑 (%ls)", L"🔑 簽名金鑰: 已儲存在此電腦 (%ls)"},
    {L"🔑 서명 키: 없음 — 처음 만들 때 새로 만듦", L"🔑 Signing key: none — a new one is made on first build", L"🔑 署名鍵: なし — 最初のビルド時に新しく生成します", L"🔑 签名密钥: 无 — 首次构建时新建", L"🔑 簽名金鑰: 無 — 首次建置時新建"},
    {L"키 내보내기", L"Export key", L"鍵をエクスポート", L"导出密钥", L"匯出金鑰"},
    {L"키 가져오기", L"Import key", L"鍵をインポート", L"导入密钥", L"匯入金鑰"},
    {L"APK 만들기", L"Build APK", L"APK をビルド", L"构建 APK", L"建置 APK"},
    {L"✖ 패키지 이름은 영문 · 숫자 · _ 로 된 두 마디 이상 (예: com.example.game)", L"✖ The package name needs two or more parts of letters · digits · _ (e.g. com.example.game)", L"✖ パッケージ名は英字 · 数字 · _ からなる 2 つ以上の部分が必要です (例: com.example.game)", L"✖ 包名需由英文字母 · 数字 · _ 组成，且至少两段 (例: com.example.game)", L"✖ 套件名稱需由英文字母 · 數字 · _ 組成，且至少兩段 (例: com.example.game)"},
    {L"✖ 버전 번호는 1 이상의 정수", L"✖ The version code must be an integer ≥ 1", L"✖ バージョンコードは 1 以上の整数", L"✖ 版本号必须是 1 以上的整数", L"✖ 版本號必須是 1 以上的整數"},
    {L"🔑 서명 키를 가져왔습니다", L"🔑 Signing key imported", L"🔑 署名鍵をインポートしました", L"🔑 已导入签名密钥", L"🔑 已匯入簽名金鑰"},
    {L"키를 가져오지 못했습니다 (키 파일이 아님)", L"Couldn't import the key (not a key file)", L"鍵をインポートできませんでした (鍵ファイルではありません)", L"无法导入密钥 (不是密钥文件)", L"無法匯入金鑰 (不是金鑰檔案)"},
    {L"서명 키를 내보냈습니다: %ls", L"Signing key exported: %ls", L"署名鍵をエクスポートしました: %ls", L"已导出签名密钥: %ls", L"已匯出簽名金鑰: %ls"},
    {L"로그", L"Log", L"ログ", L"日志", L"日誌"},
    {L"지우기", L"Clear", L"クリア", L"清空", L"清空"},
    {L"📦 프로젝트", L"📦 Project", L"📦 プロジェクト", L"📦 项目", L"📦 專案"},
    {L"＋ 파일", L"＋ File", L"＋ ファイル", L"＋ 文件", L"＋ 檔案"},
    {L"＋ 폴더", L"＋ Folder", L"＋ フォルダ", L"＋ 文件夹", L"＋ 資料夾"},
    {L"되돌리기 (Ctrl+Z)", L"Undo (Ctrl+Z)", L"元に戻す (Ctrl+Z)", L"撤销 (Ctrl+Z)", L"撤銷 (Ctrl+Z)"},
    {L"다시 하기 (Ctrl+Y)", L"Redo (Ctrl+Y)", L"やり直す (Ctrl+Y)", L"重做 (Ctrl+Y)", L"重做 (Ctrl+Y)"},
    {L"저장 (Ctrl+S)", L"Save (Ctrl+S)", L"保存 (Ctrl+S)", L"保存 (Ctrl+S)", L"儲存 (Ctrl+S)"},
    {L"파일 · 폴더 관리", L"Files & folders", L"ファイル · フォルダ管理", L"文件 · 文件夹管理", L"檔案 · 資料夾管理"},
    {L"프로젝트 메뉴", L"Project menu", L"プロジェクトメニュー", L"项目菜单", L"專案選單"},
    {L"고친 파일을 저장하고 실행할까요?", L"Save the changed files and run?", L"変更したファイルを保存して実行しますか?", L"要保存修改的文件并运行吗?", L"要儲存修改的檔案並執行嗎?"},
    {L"JawBreaker Dev. v%ls · JDH %ls", L"JawBreaker Dev. v%ls · JDH %ls", L"JawBreaker Dev. v%ls · JDH %ls", L"JawBreaker Dev. v%ls · JDH %ls", L"JawBreaker Dev. v%ls · JDH %ls"},
};
#define T(k) TR[k][LANG]
static void pickLang(void) {
    LANGID id = GetUserDefaultUILanguage(); WORD pri = PRIMARYLANGID(id), sub = SUBLANGID(id);
    LANG = pri == LANG_KOREAN ? L_KO : pri == LANG_JAPANESE ? L_JA : pri == LANG_CHINESE ? (sub == SUBLANG_CHINESE_SIMPLIFIED || sub == SUBLANG_CHINESE_SINGAPORE ? L_ZHCN : L_ZHTW) : L_EN;
    wchar_t e[16]; DWORD n = GetEnvironmentVariableW(L"JDH_LANG", e, 16);   /* 시험용: ko · en · ja · zh-CN · zh-TW */
    if (CFG.lang >= 0) LANG = CFG.lang;
    if (n && n < 16) for (int i = 0; i < 5; i++) if (!wcscmp(e, LANGCODE[i])) LANG = i;
}

/* ================= 상태 ================= */
/* 단추: 머리 줄 (▶ 실행 · ■ · 검사 · ☰) · 탐색기 (＋ 파일 · ＋ 폴더) · 로그 (지우기) — 나머지는 ☰ 메뉴 */
enum { B_RUN, B_STOP, B_CHECK, B_MENU, B_ADDFILE, B_ADDDIR, B_CLEAR, B_N };
enum { ID_BTN = 100, ID_TREE = 200, ID_EDIT, ID_LOG, ID_STATUS, ID_TAB, ID_AC, ID_PATH, ID_GUT,
       ID_M_NEWFILE = 300, ID_M_NEWDIR, ID_M_RENAME, ID_M_DELETE, ID_M_EXPLORE, ID_IN_EDIT = 400,
       ID_C_NEWFILE = 500, ID_C_NEWP, ID_C_OPEN, ID_C_SAVE, ID_C_CHECK, ID_C_HTML, ID_C_EXE, ID_C_APK, ID_C_RUNASK, ID_C_SETTINGS, ID_C_EXPLORE,
       ID_D_FIRST = 600 };
#define WM_APP_OUT (WM_APP + 1)   /* 자식의 출력 (lParam = wchar_t* · 받은 쪽이 free) */
#define WM_APP_END (WM_APP + 2)   /* 자식이 끝남 (wParam = 종료 코드) */
#define TIMER_COLOR 1
static HWND hMain, hTree, hEdit, hLog, hTabs, hPath, hGut, hAc, hFind, hBtn[B_N];
static HFONT hFontUI, hFontCode, hFontBold, hFontBrand, hFontSmall, hFontLog, hFontPath, hFontEmoji;
static wchar_t STATUS[512];                          /* 경로 줄 오른쪽의 알림 */
static int EXPL = 1, LOGH = 220;                     /* 탐색기 보임 · 로그 칸 높이 (화면 배율 전) */
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

/* ================= 모양 — 웹 판 에디터와 같은 색 (컬러 · 흑백) ================= */
typedef struct { COLORREF bg, panel, panel2, line, text, dim, accent, deep, accent2; } Theme;
static const Theme TH_COLOR = {RGB(0, 0, 12), RGB(1, 1, 20), RGB(3, 3, 30), RGB(11, 11, 56), RGB(233, 230, 245), RGB(140, 135, 166), RGB(92, 104, 255), RGB(0, 0, 159), RGB(42, 52, 224)};
static const Theme TH_MONO = {RGB(0, 0, 0), RGB(11, 11, 11), RGB(23, 23, 23), RGB(46, 46, 46), RGB(233, 230, 245), RGB(140, 135, 166), RGB(255, 255, 255), RGB(42, 42, 42), RGB(90, 90, 90)};
static Theme TH;
#define CR_MINT RGB(77, 226, 180)
#define CR_RED RGB(255, 107, 107)
#define CR_PINK RGB(255, 93, 143)
#define CR_YELLOW RGB(255, 209, 102)
#define CR_VIOLET RGB(182, 156, 255)
#define CR_BLUE RGB(108, 182, 255)
#define CR_ORANGE RGB(255, 166, 107)
#define CR_TS RGB(93, 88, 121)
#define CR_COM RGB(111, 106, 138)
static HBRUSH brBg, brPanel, brPanel2;
static COLORREF mix(COLORREF a, COLORREF b, int t) {   /* a 에서 b 쪽으로 t/256 */
    return RGB((GetRValue(a) * (256 - t) + GetRValue(b) * t) >> 8, (GetGValue(a) * (256 - t) + GetGValue(b) * t) >> 8, (GetBValue(a) * (256 - t) + GetBValue(b) * t) >> 8);
}
/* 설정 (☰ → 설정): 언어 · 램 한도 · UI · 실행 전 저장 묻기 — %APPDATA%\JDH\dev-settings.txt */
static const wchar_t *RAMV[6] = {L"auto", L"256", L"512", L"1024", L"2048", L"0"};
static const wchar_t *LANGNAME[5] = {L"한국어", L"English", L"日本語", L"简体中文", L"繁體中文"};
static int appPath(const wchar_t *name, wchar_t *out) {
    wchar_t app[MAX_PATH]; DWORD an = GetEnvironmentVariableW(L"APPDATA", app, MAX_PATH); if (!an || an >= MAX_PATH - 60) return 0;
    _snwprintf(out, MAX_PATH, L"%ls\\JDH", app); CreateDirectoryW(out, NULL);
    _snwprintf(out, MAX_PATH, L"%ls\\JDH\\%ls", app, name); out[MAX_PATH - 1] = 0; return 1;
}
static void cfgLoad(void) {
    wchar_t p[MAX_PATH]; if (!appPath(L"dev-settings.txt", p)) return;
    HANDLE f = CreateFileW(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL); if (f == INVALID_HANDLE_VALUE) return;
    char b[1024]; DWORD n = 0; ReadFile(f, b, sizeof b - 1, &n, NULL); CloseHandle(f); b[n] = 0;
    for (char *ln = strtok(b, "\r\n"); ln; ln = strtok(NULL, "\r\n")) {
        char *eq = strchr(ln, '='); if (!eq) continue; *eq++ = 0;
        if (!strcmp(ln, "lang")) { for (int i = 0; i < 5; i++) { char c[8]; WideCharToMultiByte(CP_UTF8, 0, LANGCODE[i], -1, c, 8, NULL, NULL); if (!strcmp(eq, c)) CFG.lang = i; } }
        else if (!strcmp(ln, "ram")) { for (int i = 0; i < 6; i++) { char c[8]; WideCharToMultiByte(CP_UTF8, 0, RAMV[i], -1, c, 8, NULL, NULL); if (!strcmp(eq, c)) CFG.ram = i; } }
        else if (!strcmp(ln, "ui")) CFG.mono = !strcmp(eq, "mono");
        else if (!strcmp(ln, "runask")) CFG.askRun = atoi(eq) != 0;
    }
}
static void cfgSave(void) {
    wchar_t p[MAX_PATH]; if (!appPath(L"dev-settings.txt", p)) return;
    char b[256], lc[8] = "", rv[8]; if (CFG.lang >= 0) WideCharToMultiByte(CP_UTF8, 0, LANGCODE[CFG.lang], -1, lc, 8, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, RAMV[CFG.ram], -1, rv, 8, NULL, NULL);
    int n = snprintf(b, sizeof b, "lang=%s\r\nram=%s\r\nui=%s\r\nrunask=%d\r\n", lc, rv, CFG.mono ? "mono" : "color", CFG.askRun);
    HANDLE f = CreateFileW(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL); if (f == INVALID_HANDLE_VALUE) return;
    DWORD w; WriteFile(f, b, (DWORD)n, &w, NULL); CloseHandle(f);
}
static void themeSet(int mono) {
    TH = mono ? TH_MONO : TH_COLOR;
    if (brBg) { DeleteObject(brBg); DeleteObject(brPanel); DeleteObject(brPanel2); }
    brBg = CreateSolidBrush(TH.bg); brPanel = CreateSolidBrush(TH.panel); brPanel2 = CreateSolidBrush(TH.panel2);
}
/* 창 제목 줄도 어둡게 (Windows 10 1809+ · 없으면 그대로) */
static void darkTitle(HWND h) {
    typedef HRESULT (WINAPI *DwmSet)(HWND, DWORD, LPCVOID, DWORD);
    static DwmSet f; static int tried; if (!tried) { HMODULE m = LoadLibraryW(L"dwmapi.dll"); if (m) f = (DwmSet)(void *)GetProcAddress(m, "DwmSetWindowAttribute"); tried = 1; }
    BOOL on = TRUE; if (f && f(h, 20, &on, sizeof on) != S_OK) f(h, 19, &on, sizeof on);
}
static void darkScroll(HWND h) {   /* 어두운 스크롤 막대 (Windows 10+) */
    typedef HRESULT (WINAPI *SetTheme)(HWND, LPCWSTR, LPCWSTR);
    static SetTheme f; static int tried; if (!tried) { HMODULE m = LoadLibraryW(L"uxtheme.dll"); if (m) f = (SetTheme)(void *)GetProcAddress(m, "SetWindowTheme"); tried = 1; }
    if (f) f(h, L"DarkMode_Explorer", NULL);
}
static void fillRect(HDC dc, int x, int y, int w, int h, COLORREF c) { RECT r = {x, y, x + w, y + h}; HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); }
static void roundRect(HDC dc, int x, int y, int w, int h, int rad, COLORREF fill, COLORREF edge) {
    HBRUSH b = CreateSolidBrush(fill); HPEN pn = CreatePen(PS_SOLID, 1, edge); HGDIOBJ ob = SelectObject(dc, b), op = SelectObject(dc, pn);
    RoundRect(dc, x, y, x + w, y + h, rad, rad); SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(b); DeleteObject(pn);
}
static void textAt(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, const wchar_t *t, UINT fmt) {
    HGDIOBJ o = SelectObject(dc, f); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c); RECT r = {x, y, x + w, y + h};
    DrawTextW(dc, t, -1, &r, fmt | DT_NOPREFIX); SelectObject(dc, o);
}
static int textW(HDC dc, HFONT f, const wchar_t *t) { HGDIOBJ o = SelectObject(dc, f); SIZE z; GetTextExtentPoint32W(dc, t, (int)wcslen(t), &z); SelectObject(dc, o); return z.cx; }

static void status(const wchar_t *fmt, ...) { va_list ap; va_start(ap, fmt); _vsnwprintf(STATUS, 512, fmt, ap); va_end(ap); STATUS[511] = 0; if (hPath) InvalidateRect(hPath, NULL, FALSE); }
/* 로그 칸 — 웹 판처럼 줄마다 시각 (흐리게) · 색 (✖ = 빨강 · 편집기 알림 = 민트 · 프로그램 출력 = 글자색) */
static int LOGBOL = 1; static COLORREF LOGCOL;
static void logPut(const wchar_t *t, int n, COLORREF c) {
    if (n <= 0) return;
    int L = GetWindowTextLengthW(hLog); SendMessageW(hLog, EM_SETSEL, L, L);
    CHARFORMAT2W cf; ZeroMemory(&cf, sizeof cf); cf.cbSize = sizeof cf; cf.dwMask = CFM_COLOR; cf.crTextColor = c; SendMessageW(hLog, EM_SETCHARFORMAT, SCF_SELECTION, (LPARAM)&cf);
    wchar_t *b = malloc((n + 1) * sizeof(wchar_t)); memcpy(b, t, n * sizeof(wchar_t)); b[n] = 0; SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)b); free(b);
}
static COLORREF logKind(const wchar_t *t) {
    while (*t == L' ') t++;
    if (*t == L'✖') return CR_RED;
    if (*t == L'⚠') return CR_ORANGE;
    static const wchar_t *SYS[] = {L"▶", L"■", L"✔", L"⧉", L"🔑", L"🤖", L"🪟", L"🌐", L"[selftest]", L"  ", NULL};
    for (int i = 0; SYS[i]; i++) if (!wcsncmp(t, SYS[i], wcslen(SYS[i])) && i < 9) return CR_MINT;
    return TH.text;
}
static void logAppend(const wchar_t *t) {
    int n = GetWindowTextLengthW(hLog);
    if (n > 400000) { SendMessageW(hLog, EM_SETSEL, 0, n / 2); SendMessageW(hLog, EM_REPLACESEL, FALSE, (LPARAM)L""); }
    while (*t) {
        const wchar_t *e = t; while (*e && *e != L'\r' && *e != L'\n') e++;
        if (LOGBOL && (e > t || *e)) {
            SYSTEMTIME st; GetLocalTime(&st); wchar_t ts[16]; _snwprintf(ts, 16, L"%02d:%02d:%02d  ", st.wHour, st.wMinute, st.wSecond); ts[15] = 0;
            logPut(ts, (int)wcslen(ts), CR_TS); LOGCOL = logKind(t); LOGBOL = 0;
        }
        logPut(t, (int)(e - t), LOGCOL);
        if (*e) { int k = (e[0] == L'\r' && e[1] == L'\n') ? 2 : 1; logPut(L"\r", 1, LOGCOL); LOGBOL = 1; e += k; }
        t = e;
    }
    SendMessageW(hLog, WM_VSCROLL, SB_BOTTOM, 0);
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
/* 웹 판 #hl 의 색: 키워드 분홍 (굵게) · 형식 민트 · 문자열 노랑 · 숫자 보라 · 주석 흐린 보라 (기울임) */
static COLORREF colOf(int c) { return c == C_KW ? CR_PINK : c == C_TYPE ? CR_MINT : c == C_STR ? CR_YELLOW : c == C_NUM ? CR_VIOLET : c == C_COM ? CR_COM : TH.text; }
static int inList(const wchar_t *w, int n, const wchar_t **L) { for (int i = 0; L[i]; i++) if ((int)wcslen(L[i]) == n && !wcsncmp(L[i], w, n)) return 1; return 0; }
static void paintRange(int a, int b, int c) {
    CHARFORMAT2W cf; ZeroMemory(&cf, sizeof cf); cf.cbSize = sizeof cf; cf.dwMask = CFM_COLOR | CFM_ITALIC | CFM_BOLD; cf.crTextColor = colOf(c); cf.dwEffects = (c == C_COM ? CFE_ITALIC : 0) | (c == C_KW ? CFE_BOLD : 0);
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
static void tabsChanged(void) { if (hTabs) InvalidateRect(hTabs, NULL, FALSE); if (hPath) InvalidateRect(hPath, NULL, FALSE); if (hGut) InvalidateRect(hGut, NULL, FALSE); }
static void tabLabel(int i) { (void)i; tabsChanged(); }
/* 지금 탭의 글자 · 선택 · 스크롤을 DOCS 에 기억 */
static void docStash(void) {
    if (CURD < 0) return;
    Doc *d = &DOCS[CURD]; free(d->text); d->text = editText();
    SendMessageW(hEdit, EM_EXGETSEL, 0, (LPARAM)&d->sel); SendMessageW(hEdit, EM_GETSCROLLPOS, 0, (LPARAM)&d->scroll);
}
static void docShow(int i) {
    CURD = i; tabsChanged();
    COLORING = 1;
    if (i < 0) { SetWindowTextW(hEdit, L""); SendMessageW(hEdit, EM_SETREADONLY, TRUE, 0); COLORING = 0; setTitle(); return; }
    Doc *d = &DOCS[i];
    SetWindowTextW(hEdit, d->text ? d->text : L""); SendMessageW(hEdit, EM_SETREADONLY, d->ro, 0);
    { CHARRANGE all = {0, -1}; SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&all); PARAFORMAT2 pf; ZeroMemory(&pf, sizeof pf); pf.cbSize = sizeof pf; pf.dwMask = PFM_LINESPACING; pf.bLineSpacingRule = 4; pf.dyLineSpacing = 330; SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf); }
    SendMessageW(hEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    COLORING = 0;
    recolor();
    SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&d->sel); SendMessageW(hEdit, EM_SETSCROLLPOS, 0, (LPARAM)&d->scroll);
    setTitle(); status(L"");
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
    int nx = CURD == i ? (i < NDOCS ? i : NDOCS - 1) : CURD > i ? CURD - 1 : CURD;
    CURD = -1; docShow(nx);
}
static void closeAll(void) { for (int i = 0; i < NDOCS; i++) free(DOCS[i].text); NDOCS = 0; CURD = -1; docShow(-1); }
static int findDoc(const wchar_t *p) { for (int i = 0; i < NDOCS; i++) if (!_wcsicmp(DOCS[i].path, p)) return i; return -1; }
static int openFile(const wchar_t *p, int ro) {
    int k = findDoc(p); if (k >= 0) { if (k != CURD) { docStash(); docShow(k); } return k; }
    if (!isTextName(p)) { wchar_t m[MAX_PATH + 64]; _snwprintf(m, MAX_PATH + 64, T(S_BINARY), baseName(p)); m[MAX_PATH + 63] = 0; status(L"%ls", m); return -1; }
    if (NDOCS >= MAXDOC) closeDoc(0);
    DWORD n; char *b = readFileA(p, &n); if (!b) { msgf(MB_ICONERROR, T(S_FAILFILE), p); return -1; }
    docStash();
    Doc *d = &DOCS[NDOCS]; ZeroMemory(d, sizeof *d);
    wcsncpy(d->path, p, MAX_PATH - 1); d->text = utf8ToEdit(b, (int)n); d->ro = ro; free(b);
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
    EnableWindow(hBtn[B_CHECK], p && !run); EnableWindow(hBtn[B_RUN], p && !run); EnableWindow(hBtn[B_STOP], run);
    EnableWindow(hBtn[B_ADDFILE], p); EnableWindow(hBtn[B_ADDDIR], p);
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

/* ================= 단추 · 메뉴 · 창 (어두운 모양 · 직접 그림) ================= */
/* 단추 모양 (GWLP_USERDATA): 0 보통 · 1 주 (그라데이션) · 2 글자만 · 3 고르기 (꺼짐) · 4 고르기 (켜짐) */
enum { BS_J_PLAIN, BS_J_PRIMARY, BS_J_GHOST, BS_J_SEGOFF, BS_J_SEGON };
static void drawBtn(DRAWITEMSTRUCT *d) {
    HDC dc = d->hDC; RECT r = d->rcItem; int w = r.right - r.left, h = r.bottom - r.top;
    int st = (int)GetWindowLongPtrW(d->hwndItem, GWLP_USERDATA), dis = (d->itemState & ODS_DISABLED) != 0, dn = (d->itemState & ODS_SELECTED) != 0;
    fillRect(dc, 0, 0, w, h, TH.panel);
    int rad = SC(9), oy = dn ? 1 : 0;
    if (st == BS_J_PRIMARY) {
        HRGN rg = CreateRoundRectRgn(0, oy, w + 1, h + 1 + oy, rad, rad); SelectClipRgn(dc, rg);
        for (int x = 0; x < w; x++) { COLORREF c = mix(TH.deep, TH.accent2, x * 256 / (w > 1 ? w : 1)); fillRect(dc, x, oy, 1, h, dis ? mix(c, TH.panel, 150) : c); }
        SelectClipRgn(dc, NULL); DeleteObject(rg);
        HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH); HPEN pn = CreatePen(PS_SOLID, 1, mix(TH.accent, TH.panel, 110)); HGDIOBJ ob = SelectObject(dc, nb), op = SelectObject(dc, pn);
        RoundRect(dc, 0, oy, w, h + oy - 1, rad, rad); SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(pn);
    } else if (st == BS_J_GHOST) { if (dn) roundRect(dc, 0, 0, w, h, rad, TH.panel2, TH.panel2); }
    else roundRect(dc, 0, oy, w, h - 1, rad, TH.panel2, st == BS_J_SEGON ? TH.accent : TH.line);
    if (st == BS_J_SEGON) { HBRUSH nb = (HBRUSH)GetStockObject(NULL_BRUSH); HPEN pn = CreatePen(PS_SOLID, 1, TH.accent); HGDIOBJ ob = SelectObject(dc, nb), op = SelectObject(dc, pn); RoundRect(dc, 1, oy + 1, w - 1, h - 2 + oy, rad, rad); SelectObject(dc, ob); SelectObject(dc, op); DeleteObject(pn); }
    wchar_t t[128]; GetWindowTextW(d->hwndItem, t, 128);
    COLORREF tc = st == BS_J_PRIMARY ? RGB(255, 255, 255) : st == BS_J_GHOST || st == BS_J_SEGOFF ? TH.dim : TH.text;
    if (dis) tc = mix(tc, TH.panel, 150);
    textAt(dc, st == BS_J_GHOST ? hFontUI : hFontBold, tc, 0, oy, w, h, t, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
static HWND mkBtn(HWND parent, int id, const wchar_t *text, int style) {
    HWND b = CreateWindowExW(0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SetWindowLongPtrW(b, GWLP_USERDATA, style); SendMessageW(b, WM_SETFONT, (WPARAM)hFontBold, TRUE); return b;
}
/* 메뉴 (☰ · 탐색기 오른쪽 단추) — 어두운 바탕 · 아이콘 · 단축키 */
typedef struct { wchar_t icon[8], text[160], hint[24]; int check, sep; } MItem;
static MItem MIT[40]; static int NMIT;
static void menuAdd(HMENU m, int id, const wchar_t *icon, const wchar_t *text, const wchar_t *hint, int check) {
    if (NMIT >= 40) return; MItem *it = &MIT[NMIT++]; ZeroMemory(it, sizeof *it);
    wcsncpy(it->icon, icon ? icon : L"", 7); wcsncpy(it->text, text, 159); wcsncpy(it->hint, hint ? hint : L"", 23); it->check = check;
    AppendMenuW(m, MF_OWNERDRAW, id, (LPCWSTR)it);
}
static void menuSep(HMENU m) { if (NMIT >= 40) return; MItem *it = &MIT[NMIT++]; ZeroMemory(it, sizeof *it); it->sep = 1; AppendMenuW(m, MF_OWNERDRAW | MF_DISABLED, 0, (LPCWSTR)it); }
static HMENU menuNew(void) { NMIT = 0; HMENU m = CreatePopupMenu(); MENUINFO mi; ZeroMemory(&mi, sizeof mi); mi.cbSize = sizeof mi; mi.fMask = MIM_BACKGROUND; mi.hbrBack = brPanel2; SetMenuInfo(m, &mi); return m; }
static void menuMeasure(MEASUREITEMSTRUCT *m) {
    MItem *it = (MItem *)m->itemData; if (!it) return;
    if (it->sep) { m->itemWidth = SC(10); m->itemHeight = SC(11); return; }
    HDC dc = GetDC(hMain); int w = textW(dc, hFontUI, it->text) + (it->hint[0] ? textW(dc, hFontSmall, it->hint) + SC(24) : 0); ReleaseDC(hMain, dc);
    m->itemWidth = SC(44) + w + SC(16); if ((int)m->itemWidth < SC(210)) m->itemWidth = SC(210); m->itemHeight = SC(34);
}
static void menuDraw(DRAWITEMSTRUCT *d) {
    MItem *it = (MItem *)d->itemData; HDC dc = d->hDC; RECT r = d->rcItem; int w = r.right - r.left, h = r.bottom - r.top;
    fillRect(dc, r.left, r.top, w, h, TH.panel2); if (!it) return;
    if (it->sep) { fillRect(dc, r.left + SC(6), r.top + h / 2, w - SC(12), 1, TH.line); return; }
    if (d->itemState & ODS_SELECTED) roundRect(dc, r.left + SC(4), r.top + SC(2), w - SC(8), h - SC(4), SC(8), TH.line, TH.line);
    textAt(dc, hFontEmoji, TH.text, r.left + SC(10), r.top, SC(26), h, it->icon, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    textAt(dc, hFontUI, TH.text, r.left + SC(44), r.top, w - SC(56), h, it->text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (it->hint[0]) textAt(dc, hFontSmall, TH.dim, r.left, r.top, w - SC(12), h, it->hint, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    if (it->check) textAt(dc, hFontBold, TH.accent, r.left, r.top, w - SC(12), h, L"✓", DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}
/* ----- 작은 창 (설정 · APK · 이름 입력) ----- */
typedef struct { int done, result; int (*onCmd)(HWND d, int id, int code); } DlgCtx;
static LRESULT CALLBACK DlgProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    DlgCtx *c = (DlgCtx *)GetWindowLongPtrW(h, GWLP_USERDATA);
    switch (msg) {
    case WM_CTLCOLORSTATIC: { HDC dc = (HDC)wp; LONG_PTR k = GetWindowLongPtrW((HWND)lp, GWLP_USERDATA); SetBkColor(dc, TH.panel); SetTextColor(dc, k == 1 ? TH.dim : k == 2 ? CR_RED : k == 3 ? CR_MINT : TH.text); return (LRESULT)brPanel; }
    case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: { HDC dc = (HDC)wp; SetBkColor(dc, TH.panel2); SetTextColor(dc, TH.text); return (LRESULT)brPanel2; }
    case WM_DRAWITEM: if (((DRAWITEMSTRUCT *)lp)->CtlType == ODT_BUTTON) { drawBtn((DRAWITEMSTRUCT *)lp); return TRUE; } break;
    case WM_ERASEBKGND: { RECT r; GetClientRect(h, &r); FillRect((HDC)wp, &r, brPanel); return 1; }
    case WM_COMMAND:
        if (c && c->onCmd && c->onCmd(h, LOWORD(wp), HIWORD(wp))) { c->done = 1; c->result = LOWORD(wp); return 0; }
        if (c && LOWORD(wp) == IDCANCEL) { c->done = 1; c->result = IDCANCEL; }
        return 0;
    case WM_CLOSE: if (c) { c->done = 1; c->result = IDCANCEL; } return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
static HWND dlgNew(const wchar_t *title, int w, int h, DlgCtx *c) {
    static int reg; HINSTANCE hi = GetModuleHandleW(NULL);
    if (!reg) { WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = DlgProc; wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = L"JBDevDlg"; RegisterClassExW(&wc); reg = 1; }
    RECT rr = {0, 0, w, h}; AdjustWindowRectEx(&rr, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
    int ww = rr.right - rr.left, hh = rr.bottom - rr.top;
    RECT r; GetWindowRect(hMain, &r);
    HWND d = CreateWindowExW(WS_EX_DLGMODALFRAME, L"JBDevDlg", title, WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, r.left + (r.right - r.left - ww) / 2, r.top + (r.bottom - r.top - hh) / 3, ww, hh, hMain, NULL, hi, NULL);
    SetWindowLongPtrW(d, GWLP_USERDATA, (LONG_PTR)c); darkTitle(d); return d;
}
/* kind: 0 글자 · 1 흐림 · 2 빨강 · 3 민트 */
static HWND dlgLabel(HWND d, int x, int y, int w, int h, const wchar_t *t, int kind) {
    HWND s = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE | SS_NOPREFIX, x, y, w, h, d, NULL, GetModuleHandleW(NULL), NULL);
    SetWindowLongPtrW(s, GWLP_USERDATA, kind); SendMessageW(s, WM_SETFONT, (WPARAM)(kind == 1 ? hFontSmall : hFontUI), TRUE); return s;
}
static HWND dlgEdit(HWND d, int id, int x, int y, int w, const wchar_t *t) {
    HWND e = CreateWindowExW(0, L"EDIT", t, WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x, y, w, SC(30), d, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(e, WM_SETFONT, (WPARAM)hFontUI, TRUE); SendMessageW(e, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(SC(8), SC(8))); return e;
}
static HWND dlgCombo(HWND d, int id, int x, int y, int w) {
    HWND c = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, x, y, w, SC(260), d, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)hFontUI, TRUE);
    { typedef HRESULT (WINAPI *SetTheme)(HWND, LPCWSTR, LPCWSTR); HMODULE m = LoadLibraryW(L"uxtheme.dll"); SetTheme f = m ? (SetTheme)(void *)GetProcAddress(m, "SetWindowTheme") : NULL; if (f) f(c, L"DarkMode_CFD", NULL); }
    return c;
}
static HWND dlgButton(HWND d, int id, int x, int y, int w, int h, const wchar_t *t, int style) { HWND b = mkBtn(d, id, t, style); MoveWindow(b, x, y, w, h, FALSE); return b; }
static int dlgRun(HWND d, DlgCtx *c, HWND focus) {
    EnableWindow(hMain, FALSE); ShowWindow(d, SW_SHOW); if (focus) SetFocus(focus);
    MSG m;
    while (!c->done && GetMessageW(&m, NULL, 0, 0)) {
        if (m.message == WM_KEYDOWN && m.wParam == VK_ESCAPE) { c->done = 1; c->result = IDCANCEL; break; }
        if (!IsDialogMessageW(d, &m)) { TranslateMessage(&m); DispatchMessageW(&m); }
    }
    EnableWindow(hMain, TRUE); DestroyWindow(d); SetForegroundWindow(hMain);
    return c->result;
}
/* ----- 이름 입력 ----- */
static wchar_t *inBuf;
static int inCmd(HWND d, int id, int code) { (void)code; if (id == IDOK) { GetDlgItemTextW(d, ID_IN_EDIT, inBuf, MAX_PATH); return 1; } return 0; }
static int inputBox(const wchar_t *title, const wchar_t *init, wchar_t *out) {
    DlgCtx c = {0, 0, inCmd}; int W = SC(400), M = SC(20);
    HWND d = dlgNew(title, W, SC(150), &c);
    dlgLabel(d, M, SC(16), W - 2 * M, SC(20), T(S_NAME), 1);
    HWND ed = dlgEdit(d, ID_IN_EDIT, M, SC(40), W - 2 * M, init);
    dlgButton(d, IDOK, W - M - SC(200), SC(92), SC(96), SC(36), T(S_OKB), BS_J_PRIMARY);
    dlgButton(d, IDCANCEL, W - M - SC(96), SC(92), SC(96), SC(36), T(S_CANCELB), BS_J_PLAIN);
    SendMessageW(ed, EM_SETSEL, 0, -1); const wchar_t *dot = wcsrchr(init, L'.'); if (dot && dot != init) SendMessageW(ed, EM_SETSEL, 0, dot - init);
    inBuf = out; out[0] = 0;
    return dlgRun(d, &c, ed) == IDOK && out[0];
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
    HMENU m = menuNew();
    menuAdd(m, ID_M_NEWFILE, L"📄", T(S_NEWFILE), NULL, 0); menuAdd(m, ID_M_NEWDIR, L"📁", T(S_NEWDIR), NULL, 0);
    if (MENUPATH[0]) { menuSep(m); menuAdd(m, ID_M_RENAME, L"✎", T(S_RENAME), NULL, 0); menuAdd(m, ID_M_DELETE, L"🗑", T(S_DELETE), NULL, 0); }
    menuSep(m); menuAdd(m, ID_M_EXPLORE, L"🔎", T(S_EXPLORE), NULL, 0);
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
            wchar_t row[300]; _snwprintf(row, 300, L"%ls\t%ls", a->label, a->detail); row[299] = 0;
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
    SetWindowPos(hAc, HWND_TOPMOST, p.x, p.y + SC(4), SC(480), SC(26) * (nAcShown < 9 ? nAcShown : 9) + SC(4), SWP_NOACTIVATE | SWP_SHOWWINDOW);
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

/* ================= 탭 줄 (직접 그림 · 웹 판 .tabbar) · 경로 줄 · 줄 번호 ================= */
static int TX[MAXDOC], TWD[MAXDOC], TSCROLL;
enum { TI_UNDO, TI_REDO, TI_SAVE, TI_FILES, TI_N };
static int tabIconX(int i, int W) { return W - SC(40) * (TI_N - i); }
static void tabsMeasure(HDC dc) {
    int x = 0;
    for (int i = 0; i < NDOCS; i++) { TX[i] = x; TWD[i] = SC(14) + textW(dc, hFontPath, baseName(DOCS[i].path)) + SC(8) + SC(14) + SC(12); x += TWD[i]; }
}
static void tabsEnsure(int W) {   /* 지금 탭이 보이게 */
    if (CURD < 0) { TSCROLL = 0; return; }
    int area = W - SC(40) * TI_N;
    if (TX[CURD] - TSCROLL < 0) TSCROLL = TX[CURD];
    if (TX[CURD] + TWD[CURD] - TSCROLL > area) TSCROLL = TX[CURD] + TWD[CURD] - area;
    if (TSCROLL < 0) TSCROLL = 0;
}
static int anyDirty(void) { for (int i = 0; i < NDOCS; i++) if (DOCS[i].dirty) return 1; return 0; }
static LRESULT CALLBACK TabsProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT r; GetClientRect(h, &r); int W = r.right, H = r.bottom;
        HDC dc = CreateCompatibleDC(wdc); HBITMAP bm = CreateCompatibleBitmap(wdc, W, H); HGDIOBJ ob = SelectObject(dc, bm);
        fillRect(dc, 0, 0, W, H, TH.panel);
        tabsMeasure(dc); tabsEnsure(W);
        int area = W - SC(40) * TI_N;
        HRGN clip = CreateRectRgn(0, 0, area, H); SelectClipRgn(dc, clip);
        for (int i = 0; i < NDOCS; i++) {
            int x = TX[i] - TSCROLL, w = TWD[i]; if (x + w < 0 || x > area) continue;
            int on = i == CURD;
            if (on) { fillRect(dc, x, 0, w, H, TH.bg); fillRect(dc, x, H - SC(2), w, SC(2), TH.accent); }
            textAt(dc, hFontPath, on ? TH.text : TH.dim, x + SC(14), 0, w, H - 1, baseName(DOCS[i].path), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            int xx = x + w - SC(26);
            if (DOCS[i].dirty) textAt(dc, hFontSmall, TH.accent, xx - SC(10), 0, SC(10), H - 1, L"●", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            textAt(dc, hFontSmall, TH.dim, xx, 0, SC(14), H - 1, DOCS[i].ro ? L"🔒" : L"✕", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectClipRgn(dc, NULL); DeleteObject(clip);
        static const wchar_t *IC[TI_N] = {L"↶", L"↷", L"💾", L"📁"};
        for (int i = 0; i < TI_N; i++) {
            int x = tabIconX(i, W), dis = (i == TI_UNDO && !(CURD >= 0 && SendMessageW(hEdit, EM_CANUNDO, 0, 0))) || (i == TI_REDO && !(CURD >= 0 && SendMessageW(hEdit, EM_CANREDO, 0, 0)));
            textAt(dc, i < 2 ? hFontBrand : hFontEmoji, dis ? mix(TH.dim, TH.panel, 140) : (i == TI_FILES && EXPL ? TH.text : TH.dim), x, 0, SC(40), H - 1, IC[i], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (i == TI_SAVE && anyDirty()) { HBRUSH b = CreateSolidBrush(TH.accent); HGDIOBJ o2 = SelectObject(dc, b); HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN)); Ellipse(dc, x + SC(26), SC(8), x + SC(33), SC(15)); SelectObject(dc, op); SelectObject(dc, o2); DeleteObject(b); }
        }
        fillRect(dc, 0, H - 1, W, 1, TH.line);
        BitBlt(wdc, 0, 0, W, H, dc, 0, 0, SRCCOPY); SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc);
        EndPaint(h, &ps); return 0;
    }
    case WM_LBUTTONDOWN: case WM_MBUTTONUP: {
        RECT r; GetClientRect(h, &r); int x = GET_X_LPARAM(lp), W = r.right;
        if (msg == WM_LBUTTONDOWN && x >= tabIconX(0, W)) {
            int i = (x - tabIconX(0, W)) / SC(40);
            if (i == TI_UNDO) SendMessageW(hEdit, EM_UNDO, 0, 0); else if (i == TI_REDO) SendMessageW(hEdit, EM_REDO, 0, 0);
            else if (i == TI_SAVE) saveAll(); else if (i == TI_FILES) { EXPL = !EXPL; SendMessageW(hMain, WM_SIZE, 0, 0); InvalidateRect(hMain, NULL, TRUE); }
            InvalidateRect(h, NULL, FALSE); return 0;
        }
        for (int i = 0; i < NDOCS; i++) {
            int tx = TX[i] - TSCROLL; if (x < tx || x >= tx + TWD[i]) continue;
            if (msg == WM_MBUTTONUP || x >= tx + TWD[i] - SC(28)) closeDoc(i); else if (i != CURD) { docStash(); docShow(i); }
            return 0;
        }
        return 0;
    }
    case WM_MOUSEWHEEL: TSCROLL -= GET_WHEEL_DELTA_WPARAM(wp) / 2; if (TSCROLL < 0) TSCROLL = 0; InvalidateRect(h, NULL, FALSE); return 0;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
/* 경로 줄 (웹 판 .pathbar): 폴더는 흐리게 · 파일 이름은 굵게 · 오른쪽에 알림 · 읽기 전용 표시 — 누르면 탐색기 보이기 · 숨기기 */
static LRESULT CALLBACK PathProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); RECT r; GetClientRect(h, &r); int W = r.right, H = r.bottom;
        fillRect(dc, 0, 0, W, H, TH.bg); fillRect(dc, 0, H - 1, W, 1, TH.line);
        int x = SC(12);
        if (CURD >= 0) {
            const wchar_t *p = DOCS[CURD].path; size_t pl = wcslen(PROJ);
            const wchar_t *rel = DOCS[CURD].ro ? baseName(p) : (!_wcsnicmp(p, PROJ, pl) && p[pl] == L'\\' ? p + pl + 1 : p);
            const wchar_t *bn = baseName(rel);
            wchar_t dir[MAX_PATH]; int dn = (int)(bn - rel); wcsncpy(dir, rel, dn); dir[dn] = 0; for (wchar_t *q = dir; *q; q++) if (*q == L'\\') *q = L'/';
            if (dn) { textAt(dc, hFontPath, TH.dim, x, 0, W, H - 1, dir, DT_LEFT | DT_VCENTER | DT_SINGLELINE); x += textW(dc, hFontPath, dir); }
            HFONT bf = hFontPath; LOGFONTW lf; GetObjectW(hFontPath, sizeof lf, &lf); lf.lfWeight = FW_SEMIBOLD; bf = CreateFontIndirectW(&lf);
            textAt(dc, bf, TH.text, x, 0, W, H - 1, bn, DT_LEFT | DT_VCENTER | DT_SINGLELINE); x += textW(dc, bf, bn) + SC(12); DeleteObject(bf);
        } else textAt(dc, hFontPath, TH.dim, x, 0, W, H - 1, PROJ[0] ? baseName(PROJ) : T(S_TITLE_NONE), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int rx = W - SC(12);
        if (CURD >= 0 && DOCS[CURD].ro) { int bw = textW(dc, hFontSmall, T(S_READONLY)) + SC(12); roundRect(dc, rx - bw, SC(4), bw, H - SC(9), SC(8), TH.bg, TH.line); textAt(dc, hFontSmall, TH.dim, rx - bw, SC(4), bw, H - SC(9), T(S_READONLY), DT_CENTER | DT_VCENTER | DT_SINGLELINE); rx -= bw + SC(10); }
        if (STATUS[0] && rx > x) textAt(dc, hFontSmall, TH.dim, x, 0, rx - x, H - 1, STATUS, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        EndPaint(h, &ps); return 0;
    }
    case WM_LBUTTONDOWN: EXPL = !EXPL; SendMessageW(hMain, WM_SIZE, 0, 0); InvalidateRect(hMain, NULL, TRUE); if (hTabs) InvalidateRect(hTabs, NULL, FALSE); return 0;
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
/* 줄 번호 (웹 판 .gutter) — 오류 줄은 빨강 바탕 */
static wchar_t ERRPATH[MAX_PATH]; static int ERRLINE;
static LRESULT CALLBACK GutProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT r; GetClientRect(h, &r); int W = r.right, H = r.bottom;
        HDC dc = CreateCompatibleDC(wdc); HBITMAP bm = CreateCompatibleBitmap(wdc, W, H); HGDIOBJ ob = SelectObject(dc, bm);
        fillRect(dc, 0, 0, W, H, TH.panel); fillRect(dc, W - 1, 0, 1, H, TH.line);
        if (CURD >= 0) {
            int first = (int)SendMessageW(hEdit, EM_GETFIRSTVISIBLELINE, 0, 0), n = (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0), lh = SC(22);
            int err = ERRLINE > 0 && !_wcsicmp(ERRPATH, DOCS[CURD].path) ? ERRLINE - 1 : -1;
            for (int i = first; i < n; i++) {
                int ci = (int)SendMessageW(hEdit, EM_LINEINDEX, i, 0); if (ci < 0) break;
                POINTL pt; SendMessageW(hEdit, EM_POSFROMCHAR, (WPARAM)&pt, ci); if (pt.y > H) break;
                wchar_t t[16]; _snwprintf(t, 16, L"%d", i + 1); t[15] = 0;
                if (i == err) { RECT er = {0, pt.y, W - SC(1), pt.y + lh}; HBRUSH b = CreateSolidBrush(CR_RED); FillRect(dc, &er, b); DeleteObject(b); }
                textAt(dc, hFontCode, i == err ? RGB(255, 255, 255) : TH.dim, 0, pt.y, W - SC(8), lh, t, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            }
        }
        BitBlt(wdc, 0, 0, W, H, dc, 0, 0, SRCCOPY); SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc);
        EndPaint(h, &ps); return 0;
    }
    case WM_ERASEBKGND: return 1;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
/* "✖ 파일:줄" → 파일 전체 경로 · 줄 (없으면 0) */
static int parseErr(const wchar_t *line, wchar_t *full, int *ln) {
    wchar_t buf[2048]; wcsncpy(buf, line, 2047); buf[2047] = 0;
    wchar_t *p = wcsstr(buf, L"✖"); if (!p) return 0; p++; while (*p == L' ') p++;
    wchar_t *colon = NULL; for (wchar_t *q = p; *q; q++) if (*q == L':' && iswdigit(q[1])) { wchar_t *e = q + 1; while (iswdigit(*e)) e++; if (*e == L' ' || *e == 0 || *e == L'\r' || *e == L'\n') { colon = q; break; } }
    if (!colon || colon == p) return 0;
    *ln = _wtoi(colon + 1); *colon = 0;
    for (wchar_t *c = p; *c; c++) if (*c == L'/') *c = L'\\';
    if (p[1] == L':') wcsncpy(full, p, MAX_PATH - 1); else _snwprintf(full, MAX_PATH, L"%ls\\%ls", PROJ, p);
    full[MAX_PATH - 1] = 0; return fileExists(full);
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
    childProc = pi.hProcess; childStart = GetTickCount(); ERRLINE = 0; ERRPATH[0] = 0; if (hGut) InvalidateRect(hGut, NULL, FALSE);
    logLine(T(S_RUNNING), label);
    enableButtons();
    HANDLE th = CreateThread(NULL, 0, readerThread, rd, 0, NULL); if (th) CloseHandle(th);
    return 1;
}
static int anyDirtyDoc(void) { if (CURD >= 0) docStash(); for (int i = 0; i < NDOCS; i++) if (DOCS[i].dirty) return 1; return 0; }
static int jdhPath(wchar_t *out) { _snwprintf(out, MAX_PATH, L"%ls\\jdh.exe", BINDIR); out[MAX_PATH - 1] = 0; if (!fileExists(out)) { logLine(L"%ls", T(S_NOJDH)); return 0; } return 1; }
static int prep(void) { if (!PROJ[0]) { status(L"%ls", T(S_NOPROJ)); return 0; } acClose(); return saveAll(); }
static void runJdhCmd(const wchar_t *args, const wchar_t *label) {
    if (!prep()) return;
    wchar_t jdh[MAX_PATH], cmd[MAX_PATH * 3]; if (!jdhPath(jdh)) return;
    _snwprintf(cmd, MAX_PATH * 3, L"\"%ls\" %ls \"%ls\"", jdh, args, PROJ); cmd[MAX_PATH * 3 - 1] = 0;
    startChild(cmd, label);
}
static void cmdRun(void) {
    if (CFG.askRun && anyDirtyDoc() && !SELFTEST && MessageBoxW(hMain, T(S_ASKRUN), L"JawBreaker Dev.", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) return;
    if (!prep()) return;
    wchar_t bat[MAX_PATH], cmd[MAX_PATH * 3], cmdexe[MAX_PATH];
    _snwprintf(bat, MAX_PATH, L"%ls\\run.bat", PROJ); bat[MAX_PATH - 1] = 0;
    if (!fileExists(bat)) { runJdhCmd(L"run", L"jdh run"); return; }
    GetSystemDirectoryW(cmdexe, MAX_PATH); wcscat(cmdexe, L"\\cmd.exe");
    _snwprintf(cmd, MAX_PATH * 3, L"\"%ls\" /c \"\"%ls\"\"", cmdexe, bat); cmd[MAX_PATH * 3 - 1] = 0;
    startChild(cmd, L"run.bat");
}
/* ================= ☰ → 설정 (웹 판과 같은 항목: 언어 · 램 한도 · UI) ================= */
static void applyLang(void); static void applyTheme(void);
static HWND sLang, sRam, sSegC, sSegM; static int sReopen;
static int setCmd(HWND d, int id, int code) {
    if (id == ID_D_FIRST && code == CBN_SELCHANGE) { int k = (int)SendMessageW(sLang, CB_GETCURSEL, 0, 0); if (k >= 0 && k < 5 && k != LANG) { CFG.lang = k; cfgSave(); applyLang(); sReopen = 1; return 1; } return 0; }
    if (id == ID_D_FIRST + 1 && code == CBN_SELCHANGE) { int k = (int)SendMessageW(sRam, CB_GETCURSEL, 0, 0); if (k >= 0 && k < 6) { CFG.ram = k; cfgSave(); } return 0; }
    if (id == ID_D_FIRST + 2 || id == ID_D_FIRST + 3) {
        CFG.mono = id == ID_D_FIRST + 3; cfgSave(); applyTheme();
        SetWindowLongPtrW(sSegC, GWLP_USERDATA, CFG.mono ? BS_J_SEGOFF : BS_J_SEGON); SetWindowLongPtrW(sSegM, GWLP_USERDATA, CFG.mono ? BS_J_SEGON : BS_J_SEGOFF);
        RedrawWindow(d, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN); return 0;
    }
    return id == IDOK;
}
static HWND buildSettings(DlgCtx *c) {
    int W = SC(420), M = SC(22), y = SC(18), cw = W - 2 * M;
    HWND d = dlgNew(T(S_SET_TITLE), W, SC(452), c);
    dlgLabel(d, M, y, cw, SC(18), T(S_SET_LANG), 1); y += SC(22);
    sLang = dlgCombo(d, ID_D_FIRST, M, y, cw); for (int i = 0; i < 5; i++) SendMessageW(sLang, CB_ADDSTRING, 0, (LPARAM)LANGNAME[i]); SendMessageW(sLang, CB_SETCURSEL, LANG, 0); y += SC(46);
    dlgLabel(d, M, y, cw, SC(18), T(S_SET_RAM), 1); y += SC(22);
    sRam = dlgCombo(d, ID_D_FIRST + 1, M, y, cw);
    const wchar_t *rl[6] = {T(S_RAM_AUTO), L"256 MB", L"512 MB", L"1 GB", L"2 GB", T(S_RAM_UNL)};
    for (int i = 0; i < 6; i++) SendMessageW(sRam, CB_ADDSTRING, 0, (LPARAM)rl[i]); SendMessageW(sRam, CB_SETCURSEL, CFG.ram, 0); y += SC(40);
    dlgLabel(d, M, y, cw, SC(80), T(S_RAM_HINT), 1); y += SC(90);
    dlgLabel(d, M, y, cw, SC(18), T(S_SET_UI), 1); y += SC(22);
    int sw = (cw - SC(8)) / 2;
    sSegC = dlgButton(d, ID_D_FIRST + 2, M, y, sw, SC(38), T(S_UI_COLOR), CFG.mono ? BS_J_SEGOFF : BS_J_SEGON);
    sSegM = dlgButton(d, ID_D_FIRST + 3, M + sw + SC(8), y, sw, SC(38), T(S_UI_MONO), CFG.mono ? BS_J_SEGON : BS_J_SEGOFF); y += SC(54);
    wchar_t v[96]; _snwprintf(v, 96, T(S_VERSION), L"1.6", JDH_VER_W); v[95] = 0;
    HWND vs = dlgLabel(d, M, y, cw, SC(18), v, 1); SetWindowLongPtrW(vs, GWL_STYLE, GetWindowLongPtrW(vs, GWL_STYLE) | SS_CENTER); y += SC(30);
    dlgButton(d, IDOK, M, y, cw, SC(40), T(S_CLOSE), BS_J_PRIMARY);
    return d;
}
static void cmdSettings(void) {
    do { sReopen = 0; DlgCtx c = {0, 0, setCmd}; HWND d = buildSettings(&c); dlgRun(d, &c, NULL); } while (sReopen);
}
static const wchar_t *ramArg(void) { static wchar_t a[24]; if (!CFG.ram) return L""; _snwprintf(a, 24, L" --ram %ls", RAMV[CFG.ram]); a[23] = 0; return a; }
static void cmdHtml(void) { wchar_t a[96]; _snwprintf(a, 96, L"build html --lang %ls%ls", LANGCODE[LANG], ramArg()); a[95] = 0; runJdhCmd(a, L"jdh build html"); }

/* ================= ☰ → APK 로 변환 (웹 판 exportApk 와 같은 칸 · jdh build apk) ================= */
typedef struct { wchar_t label[128], pkg[128], vname[32], ori[16], icon[MAX_PATH]; long vcode; } ApkCfg;
static ApkCfg AC;
static void apkCfgPath(wchar_t *out) { wchar_t n[MAX_PATH]; _snwprintf(n, MAX_PATH, L"apk-%ls.cfg", baseName(PROJ)); n[MAX_PATH - 1] = 0; if (!appPath(n, out)) out[0] = 0; }
static void projName(wchar_t *out) {   /* project.jbs 의 name (없으면 폴더 이름) */
    wcsncpy(out, baseName(PROJ), 127); out[127] = 0;
    wchar_t jbs[MAX_PATH]; _snwprintf(jbs, MAX_PATH, L"%ls\\project.jbs", PROJ); jbs[MAX_PATH - 1] = 0;
    DWORD n; char *b = readFileA(jbs, &n); if (!b) return;
    for (char *ln = strtok(b, "\r\n"); ln; ln = strtok(NULL, "\r\n")) { char *q = ln; while (*q == ' ' || *q == '\t') q++;
        if (!strncmp(q, "name", 4) && (q[4] == ' ' || q[4] == '=' || q[4] == '\t')) { char *e = strchr(q, '='); if (e) { e++; while (*e == ' ' || *e == '\t') e++; char *z = e + strlen(e); while (z > e && (z[-1] == ' ' || z[-1] == '\t')) *--z = 0; if (*e) MultiByteToWideChar(CP_UTF8, 0, e, -1, out, 128); } } }
    free(b);
}
static void apkPkgOf(const wchar_t *name, wchar_t *out) {   /* 웹 판 apkPkgOf 와 같은 규칙 */
    wchar_t s[40]; int n = 0;
    for (const wchar_t *p = name; *p && n < 30; p++) { wchar_t c = *p; if (c >= L'A' && c <= L'Z') c += 32; if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) s[n++] = c; }
    s[n] = 0;
    if (n && s[0] >= L'a' && s[0] <= L'z') { _snwprintf(out, 128, L"com.jawbreaker.%ls", s); return; }
    unsigned h = 0; for (const wchar_t *p = name; *p; p++) { unsigned cp = *p; if (cp >= 0xD800 && cp < 0xDC00 && p[1] >= 0xDC00 && p[1] < 0xE000) { cp = 0x10000 + ((cp - 0xD800) << 10) + (p[1] - 0xDC00); p++; } h = h * 31 + cp; }
    wchar_t b36[16]; int k = 0; do { int dd = h % 36; b36[k++] = (wchar_t)(dd < 10 ? L'0' + dd : L'a' + dd - 10); h /= 36; } while (h); b36[k] = 0;
    for (int i = 0; i < k / 2; i++) { wchar_t t = b36[i]; b36[i] = b36[k - 1 - i]; b36[k - 1 - i] = t; }
    _snwprintf(out, 128, L"com.jawbreaker.game%ls", b36);
}
static void apkCfgLoad(void) {
    wchar_t nm[128]; projName(nm); ZeroMemory(&AC, sizeof AC);
    wcscpy(AC.label, nm); apkPkgOf(nm, AC.pkg); wcscpy(AC.vname, L"1.0"); wcscpy(AC.ori, L"auto"); AC.vcode = 0;
    wchar_t p[MAX_PATH]; apkCfgPath(p); if (!p[0]) return;
    DWORD n; char *b = readFileA(p, &n); if (!b) return;
    for (char *ln = strtok(b, "\r\n"); ln; ln = strtok(NULL, "\r\n")) {
        char *eq = strchr(ln, '='); if (!eq) continue; *eq++ = 0; wchar_t v[MAX_PATH]; MultiByteToWideChar(CP_UTF8, 0, eq, -1, v, MAX_PATH);
        if (!strcmp(ln, "label")) wcsncpy(AC.label, v, 127); else if (!strcmp(ln, "pkg")) wcsncpy(AC.pkg, v, 127); else if (!strcmp(ln, "vname")) wcsncpy(AC.vname, v, 31);
        else if (!strcmp(ln, "ori")) wcsncpy(AC.ori, v, 15); else if (!strcmp(ln, "icon")) wcsncpy(AC.icon, v, MAX_PATH - 1); else if (!strcmp(ln, "vcode")) AC.vcode = atol(eq);
    }
    free(b);
}
static void apkCfgSave(void) {
    wchar_t p[MAX_PATH]; apkCfgPath(p); if (!p[0]) return;
    wchar_t t[2048]; _snwprintf(t, 2048, L"label=%ls\r\npkg=%ls\r\nvcode=%ld\r\nvname=%ls\r\nori=%ls\r\nicon=%ls\r\n", AC.label, AC.pkg, AC.vcode, AC.vname, AC.ori, AC.icon); t[2047] = 0;
    DWORD n; char *b = wideToUtf8(t, &n); writeFileA(p, b, n); free(b);
}
static int pkgOk(const wchar_t *p) {
    int parts = 0;
    for (;;) { if (!((*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z'))) return 0; while ((*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z') || (*p >= L'0' && *p <= L'9') || *p == L'_') p++; parts++; if (!*p) break; if (*p != L'.') return 0; p++; }
    return parts >= 2;
}
static void keyPath(wchar_t *out) { DWORD n = GetEnvironmentVariableW(L"JDH_APK_KEY", out, MAX_PATH); if (n && n < MAX_PATH) return; if (!appPath(L"apk-key.json", out)) out[0] = 0; }
static void keyInfo(wchar_t *out, int n) {   /* 서명 키가 있는지 · 만든 날 */
    wchar_t p[MAX_PATH]; keyPath(p); DWORD len; char *b = p[0] ? readFileA(p, &len) : NULL;
    if (!b) { wcsncpy(out, T(S_KEY_NONE), n - 1); out[n - 1] = 0; return; }
    wchar_t day[32] = L"-"; char *m = strstr(b, "\"made\"");
    if (m && (m = strchr(m, ':'))) { double ms = atof(m + 1); if (ms > 0) { ULONGLONG ft = (ULONGLONG)(ms * 10000.0) + 116444736000000000ULL; FILETIME f = {(DWORD)ft, (DWORD)(ft >> 32)}, lf; SYSTEMTIME st; FileTimeToLocalFileTime(&f, &lf); FileTimeToSystemTime(&lf, &st); _snwprintf(day, 32, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay); } }
    free(b); _snwprintf(out, n, T(S_KEY_HAVE), day); out[n - 1] = 0;
}
static wchar_t ICONS[64][MAX_PATH]; static int NICON;
static void findPngs(const wchar_t *dir, const wchar_t *rel, int depth) {
    if (depth > 6 || NICON >= 64) return;
    wchar_t pat[MAX_PATH]; _snwprintf(pat, MAX_PATH, L"%ls\\*", dir); pat[MAX_PATH - 1] = 0;
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(pat, &fd); if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == L'.') continue;
        wchar_t full[MAX_PATH], r2[MAX_PATH]; _snwprintf(full, MAX_PATH, L"%ls\\%ls", dir, fd.cFileName); _snwprintf(r2, MAX_PATH, L"%ls%ls%ls", rel, rel[0] ? L"/" : L"", fd.cFileName); full[MAX_PATH - 1] = r2[MAX_PATH - 1] = 0;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { if (!(depth == 0 && (!_wcsicmp(fd.cFileName, L"build") || !_wcsicmp(fd.cFileName, L"ext")))) findPngs(full, r2, depth + 1); }
        else { size_t L = wcslen(fd.cFileName); if (L > 4 && !_wcsicmp(fd.cFileName + L - 4, L".png") && NICON < 64) wcscpy(ICONS[NICON++], r2); }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static HWND aLabel, aPkg, aCode, aVer, aOri, aIcon, aKey, aErr;
static void apkStart(void) {   /* AC 대로 jdh build apk */
    wchar_t lab[128]; wcscpy(lab, AC.label); for (wchar_t *q = lab; *q; q++) if (*q == L'"') *q = L'\'';
    wchar_t a[1600]; _snwprintf(a, 1600, L"build apk --label \"%ls\" --pkg %ls --version-code %ld --version-name \"%ls\" --orientation %ls%ls%ls%ls --lang %ls%ls",
        lab, AC.pkg, AC.vcode, AC.vname, AC.ori, AC.icon[0] ? L" --icon \"" : L"", AC.icon, AC.icon[0] ? L"\"" : L"", LANGCODE[LANG], ramArg()); a[1599] = 0;
    runJdhCmd(a, L"jdh build apk");
}
static int apkCmd(HWND d, int id, int code) {
    (void)code;
    if (id == ID_D_FIRST + 10 || id == ID_D_FIRST + 11) {   /* 키 내보내기 · 가져오기 */
        wchar_t kp[MAX_PATH]; keyPath(kp);
        wchar_t f[MAX_PATH] = L"jawbreaker-apk-key.json"; OPENFILENAMEW o; ZeroMemory(&o, sizeof o); o.lStructSize = sizeof o; o.hwndOwner = d; o.lpstrFile = f; o.nMaxFile = MAX_PATH; o.lpstrFilter = L"JSON\0*.json\0\0";
        if (id == ID_D_FIRST + 10) { o.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR; o.lpstrDefExt = L"json"; if (GetSaveFileNameW(&o)) { if (CopyFileW(kp, f, FALSE)) { wchar_t m[MAX_PATH + 64]; _snwprintf(m, MAX_PATH + 64, T(S_KEY_SAVED), f); m[MAX_PATH + 63] = 0; SetWindowLongPtrW(aErr, GWLP_USERDATA, 3); SetWindowTextW(aErr, m); } else { SetWindowLongPtrW(aErr, GWLP_USERDATA, 2); SetWindowTextW(aErr, T(S_KEY_FAIL)); } } }
        else { f[0] = 0; o.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR; if (GetOpenFileNameW(&o)) {
            wchar_t args[MAX_PATH + 32]; _snwprintf(args, MAX_PATH + 32, L"apk-key import \"%ls\"", f); args[MAX_PATH + 31] = 0; DWORD rc = 1; char out[512];
            int ok = runJdh(BINDIR, args, 0, 20000, out, sizeof out, &rc) && rc == 0;
            SetWindowLongPtrW(aErr, GWLP_USERDATA, ok ? 3 : 2); SetWindowTextW(aErr, ok ? T(S_KEY_DONE) : T(S_KEY_FAIL));
            wchar_t ki[160]; keyInfo(ki, 160); SetWindowTextW(aKey, ki); } }
        InvalidateRect(aErr, NULL, TRUE); return 0;
    }
    if (id != IDOK) return 0;
    ApkCfg n = AC; wchar_t t[64];
    GetWindowTextW(aLabel, n.label, 128); GetWindowTextW(aPkg, n.pkg, 128); GetWindowTextW(aVer, n.vname, 32); GetWindowTextW(aCode, t, 64);
    wchar_t *e; n.vcode = wcstol(t, &e, 10); if (*e) n.vcode = -1;
    static const wchar_t *ORI[3] = {L"auto", L"landscape", L"portrait"}; int oi = (int)SendMessageW(aOri, CB_GETCURSEL, 0, 0); wcscpy(n.ori, ORI[oi >= 0 && oi < 3 ? oi : 0]);
    int ii = (int)SendMessageW(aIcon, CB_GETCURSEL, 0, 0); n.icon[0] = 0; if (ii > 0 && ii <= NICON) wcscpy(n.icon, ICONS[ii - 1]);
    if (!n.label[0]) projName(n.label); if (!n.vname[0]) wcscpy(n.vname, L"1.0");
    if (!pkgOk(n.pkg)) { SetWindowLongPtrW(aErr, GWLP_USERDATA, 2); SetWindowTextW(aErr, T(S_APK_BADPKG)); InvalidateRect(aErr, NULL, TRUE); return 0; }
    if (!(n.vcode >= 1 && n.vcode <= 2100000000)) { SetWindowLongPtrW(aErr, GWLP_USERDATA, 2); SetWindowTextW(aErr, T(S_APK_BADVC)); InvalidateRect(aErr, NULL, TRUE); return 0; }
    AC = n; return 1;
}
static HWND buildApk(DlgCtx *c) {
    apkCfgLoad(); NICON = 0; findPngs(PROJ, L"", 0);
    int W = SC(460), M = SC(22), y = SC(16), cw = W - 2 * M;
    HWND d = dlgNew(T(S_APK_TITLE), W, SC(640), c);
    dlgLabel(d, M, y, cw, SC(48), T(S_APK_DESC), 1); y += SC(56);
    dlgLabel(d, M, y, cw, SC(18), T(S_APK_LABEL), 1); y += SC(20); aLabel = dlgEdit(d, ID_D_FIRST + 1, M, y, cw, AC.label); y += SC(40);
    dlgLabel(d, M, y, cw, SC(18), T(S_APK_PKG), 1); y += SC(20); aPkg = dlgEdit(d, ID_D_FIRST + 2, M, y, cw, AC.pkg); y += SC(40);
    int hw = (cw - SC(10)) / 2; wchar_t vc[24]; _snwprintf(vc, 24, L"%ld", AC.vcode + 1); vc[23] = 0;
    dlgLabel(d, M, y, hw, SC(18), T(S_APK_VCODE), 1); dlgLabel(d, M + hw + SC(10), y, hw, SC(18), T(S_APK_VNAME), 1); y += SC(20);
    aCode = dlgEdit(d, ID_D_FIRST + 3, M, y, hw, vc); aVer = dlgEdit(d, ID_D_FIRST + 4, M + hw + SC(10), y, hw, AC.vname); y += SC(40);
    dlgLabel(d, M, y, hw, SC(18), T(S_APK_ORI), 1); dlgLabel(d, M + hw + SC(10), y, hw, SC(18), T(S_APK_ICON), 1); y += SC(20);
    aOri = dlgCombo(d, ID_D_FIRST + 5, M, y, hw); SendMessageW(aOri, CB_ADDSTRING, 0, (LPARAM)T(S_ORI_AUTO)); SendMessageW(aOri, CB_ADDSTRING, 0, (LPARAM)T(S_ORI_LAND)); SendMessageW(aOri, CB_ADDSTRING, 0, (LPARAM)T(S_ORI_PORT));
    SendMessageW(aOri, CB_SETCURSEL, !wcscmp(AC.ori, L"landscape") ? 1 : !wcscmp(AC.ori, L"portrait") ? 2 : 0, 0);
    aIcon = dlgCombo(d, ID_D_FIRST + 6, M + hw + SC(10), y, hw); SendMessageW(aIcon, CB_ADDSTRING, 0, (LPARAM)T(S_ICON_DEF));
    int sel = 0; for (int i = 0; i < NICON; i++) { SendMessageW(aIcon, CB_ADDSTRING, 0, (LPARAM)ICONS[i]); if (!_wcsicmp(ICONS[i], AC.icon) || (!AC.icon[0] && !sel && !_wcsicmp(baseName(ICONS[i]), L"icon.png"))) sel = i + 1; }
    SendMessageW(aIcon, CB_SETCURSEL, sel, 0); y += SC(46);
    wchar_t ki[160]; keyInfo(ki, 160); aKey = dlgLabel(d, M, y, cw, SC(20), ki, 1); y += SC(24);
    dlgButton(d, ID_D_FIRST + 10, M, y, SC(130), SC(32), T(S_KEY_EXPORT), BS_J_PLAIN); dlgButton(d, ID_D_FIRST + 11, M + SC(138), y, SC(130), SC(32), T(S_KEY_IMPORT), BS_J_PLAIN); y += SC(42);
    aErr = dlgLabel(d, M, y, cw, SC(36), L"", 2); y += SC(40);
    dlgButton(d, IDOK, M, y, cw, SC(40), T(S_APK_BUILD), BS_J_PRIMARY); y += SC(46);
    dlgButton(d, IDCANCEL, M, y, cw, SC(36), T(S_CANCELB), BS_J_PLAIN);
    return d;
}
static void cmdApk(void) {
    if (!PROJ[0]) { status(L"%ls", T(S_NOPROJ)); return; }
    if (childProc) { status(L"%ls", T(S_BUSY)); return; }
    DlgCtx c = {0, 0, apkCmd}; HWND d = buildApk(&c);
    if (dlgRun(d, &c, aLabel) != IDOK) return;
    apkCfgSave(); apkStart();
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
    wchar_t full[MAX_PATH]; int ln; if (!parseErr(buf, full, &ln)) return 0;
    if (openFile(full, 0) < 0) return 0;
    gotoLine(ln); return 1;
}

/* ================= 창 — 웹 판과 같은 배치 ================= */
/*   머리 줄 (로고 · JawBreaker Dev. · ▶ 실행 · ■ · 검사 · ☰) · [탐색기] · 탭 줄 · 경로 줄 · 줄 번호 + 코드 칸 · 로그 머리 · 로그 칸 */
#define HDR SC(46)
#define TABH SC(36)
#define PATHH SC(26)
#define PHEAD SC(32)
#define EXW (EXPL ? SC(260) : 0)
static int logH(int H) { int lh = SC(LOGH); int mx = H - HDR - TABH - PATHH - PHEAD - SC(80); if (lh > mx) lh = mx; if (lh < SC(60)) lh = SC(60); return lh; }
static void editMargins(void) { RECT r; GetClientRect(hEdit, &r); r.left += SC(12); r.top += SC(8); SendMessageW(hEdit, EM_SETRECT, 0, (LPARAM)&r); RECT q; GetClientRect(hLog, &q); q.left += SC(10); q.top += SC(6); SendMessageW(hLog, EM_SETRECT, 0, (LPARAM)&q); }
static void layout(void) {
    RECT r; GetClientRect(hMain, &r); int W = r.right, H = r.bottom, ex = EXW, lh = logH(H);
    int bx = W - SC(10), by = (HDR - SC(32)) / 2;
    const int bw[4] = {SC(88), SC(40), SC(64), SC(40)};   /* ▶ 실행 · ■ · 검사 · ☰ */
    for (int i = 3; i >= 0; i--) { bx -= bw[i]; MoveWindow(hBtn[i], bx, by, bw[i], SC(32), TRUE); bx -= SC(6); }
    ShowWindow(hTree, EXPL ? SW_SHOW : SW_HIDE); ShowWindow(hBtn[B_ADDFILE], EXPL ? SW_SHOW : SW_HIDE); ShowWindow(hBtn[B_ADDDIR], EXPL ? SW_SHOW : SW_HIDE);
    if (EXPL) {
        MoveWindow(hBtn[B_ADDFILE], SC(10), HDR + SC(40), SC(96), SC(30), TRUE); MoveWindow(hBtn[B_ADDDIR], SC(112), HDR + SC(40), SC(104), SC(30), TRUE);
        MoveWindow(hTree, 0, HDR + SC(80), ex - 1, H - HDR - SC(80), TRUE);
    }
    MoveWindow(hTabs, ex, HDR, W - ex, TABH, TRUE);
    MoveWindow(hPath, ex, HDR + TABH, W - ex, PATHH, TRUE);
    int ey = HDR + TABH + PATHH, eh = H - ey - lh - PHEAD;
    MoveWindow(hGut, ex, ey, SC(48), eh, TRUE);
    MoveWindow(hEdit, ex + SC(48), ey, W - ex - SC(48), eh, TRUE);
    MoveWindow(hBtn[B_CLEAR], W - SC(10) - SC(64), H - lh - PHEAD + SC(4), SC(64), SC(26), TRUE);
    MoveWindow(hLog, ex, H - lh, W - ex, lh, TRUE);
    editMargins();
    InvalidateRect(hMain, NULL, FALSE);
}
static void paintMain(HDC dc) {
    RECT r; GetClientRect(hMain, &r); int W = r.right, H = r.bottom, ex = EXW, lh = logH(H);
    /* 머리 줄 */
    fillRect(dc, 0, 0, W, HDR, TH.panel); fillRect(dc, 0, HDR - 1, W, 1, TH.line);
    { int cx = SC(10) + SC(13), cy = HDR / 2, rr = SC(11);
      HPEN p1 = CreatePen(PS_SOLID, SC(3), CFG.mono ? RGB(138, 138, 138) : TH.deep); HGDIOBJ op = SelectObject(dc, p1), ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
      Ellipse(dc, cx - rr, cy - rr, cx + rr, cy + rr); SelectObject(dc, op); DeleteObject(p1);
      HPEN p2 = CreatePen(PS_SOLID, SC(2), CFG.mono ? RGB(255, 255, 255) : RGB(180, 186, 255)); op = SelectObject(dc, p2);
      Arc(dc, cx - rr, cy - rr, cx + rr, cy + rr, cx + rr, cy - rr / 2, cx, cy - rr); SelectObject(dc, op); SelectObject(dc, ob); DeleteObject(p2); }
    int tx = SC(10) + SC(26) + SC(8);
    textAt(dc, hFontBrand, TH.text, tx, 0, W, HDR, L"JawBreaker Dev.", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    tx += textW(dc, hFontBrand, L"JawBreaker Dev.") + SC(8);
    textAt(dc, hFontSmall, TH.dim, tx, SC(3), W, HDR, L"v1.6", DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    /* 탐색기 머리 */
    if (EXPL) {
        fillRect(dc, 0, HDR, ex, H - HDR, TH.panel); fillRect(dc, ex - 1, HDR, 1, H - HDR, TH.line);
        textAt(dc, hFontBold, TH.text, SC(12), HDR, ex, SC(38), T(S_PROJECT), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        int nx = SC(12) + textW(dc, hFontBold, T(S_PROJECT)) + SC(8);
        textAt(dc, hFontSmall, TH.dim, nx, HDR + SC(1), ex - nx - SC(8), SC(38), PROJ[0] ? baseName(PROJ) : L"", DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        fillRect(dc, 0, HDR + SC(38) - 1, ex - 1, 1, TH.line); fillRect(dc, 0, HDR + SC(80) - 1, ex - 1, 1, TH.line);
        fillRect(dc, 0, HDR + SC(38), ex - 1, SC(42) - 1, TH.panel);
    }
    /* 로그 머리 (웹 판 .phead · 위쪽 끝을 끌면 높이가 바뀜) */
    int py = H - lh - PHEAD;
    fillRect(dc, ex, py, W - ex, PHEAD, TH.panel); fillRect(dc, ex, py, W - ex, 1, TH.line);
    fillRect(dc, ex + (W - ex) / 2 - SC(22), py + SC(5), SC(44), SC(3), TH.line);   /* 잡는 곳 */
    int pw = textW(dc, hFontBold, T(S_LOG)) + SC(20);
    roundRect(dc, ex + SC(6), py + SC(6), pw, PHEAD - SC(10), SC(8), TH.panel2, TH.panel2);
    textAt(dc, hFontBold, TH.text, ex + SC(6), py + SC(6), pw, PHEAD - SC(10), T(S_LOG), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    fillRect(dc, ex, H - lh - 1, W - ex, 1, TH.line);
}
static void editColors(void) {
    SendMessageW(hEdit, EM_SETBKGNDCOLOR, 0, TH.bg); SendMessageW(hLog, EM_SETBKGNDCOLOR, 0, TH.bg);
    CHARFORMAT2W cf; ZeroMemory(&cf, sizeof cf); cf.cbSize = sizeof cf; cf.dwMask = CFM_COLOR; cf.crTextColor = TH.text;
    SendMessageW(hEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf); SendMessageW(hLog, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM)&cf);
    SendMessageW(hTree, TVM_SETBKCOLOR, 0, TH.panel); SendMessageW(hTree, TVM_SETTEXTCOLOR, 0, TH.text); SendMessageW(hTree, TVM_SETLINECOLOR, 0, TH.line);
}
static void applyTheme(void) {
    themeSet(CFG.mono); editColors(); recolor();
    RedrawWindow(hMain, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_FRAME);
}
static void btnTexts(void) {
    SetWindowTextW(hBtn[B_RUN], T(S_RUN)); SetWindowTextW(hBtn[B_STOP], L"■"); SetWindowTextW(hBtn[B_CHECK], T(S_CHECK)); SetWindowTextW(hBtn[B_MENU], L"☰");
    SetWindowTextW(hBtn[B_ADDFILE], T(S_ADDFILE)); SetWindowTextW(hBtn[B_ADDDIR], T(S_ADDDIR)); SetWindowTextW(hBtn[B_CLEAR], T(S_CLEAR));
}
static void applyLang(void) {
    if (CFG.lang >= 0) LANG = CFG.lang;
    btnTexts(); findDocs(); NACI = 0; acLoad(); treeReload(); setTitle();
    RedrawWindow(hMain, NULL, NULL, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
/* ☰ 메뉴 (웹 판의 프로젝트 메뉴와 같은 차례) */
static void mainMenu(void) {
    RECT br; GetWindowRect(hBtn[B_MENU], &br);
    HMENU m = menuNew(); int p = PROJ[0] != 0, run = childProc != NULL;
    menuAdd(m, ID_C_NEWFILE, L"📄", T(S_NEWFILE), NULL, 0);
    menuAdd(m, ID_C_NEWP, L"🗂", T(S_NEWP), NULL, 0);
    menuAdd(m, ID_C_OPEN, L"📂", T(S_OPEN), L"Ctrl+O", 0);
    menuAdd(m, ID_C_SAVE, L"💾", T(S_SAVE), L"Ctrl+S", 0);
    menuAdd(m, ID_C_CHECK, L"✔", T(S_CHECK), L"F7", 0);
    menuAdd(m, ID_C_HTML, L"🌐", T(S_M_HTML), NULL, 0);
    menuAdd(m, ID_C_EXE, L"🪟", T(S_M_EXE), NULL, 0);
    menuAdd(m, ID_C_APK, L"🤖", T(S_M_APK), NULL, 0);
    menuSep(m);
    menuAdd(m, ID_C_RUNASK, L"↻", T(S_RUNASK), NULL, CFG.askRun);
    menuAdd(m, ID_C_SETTINGS, L"⚙", T(S_SETTINGS), NULL, 0);
    menuAdd(m, ID_C_EXPLORE, L"🔎", T(S_EXPLORE), NULL, 0);
    if (!p) { EnableMenuItem(m, ID_C_NEWFILE, MF_GRAYED); EnableMenuItem(m, ID_C_SAVE, MF_GRAYED); EnableMenuItem(m, ID_C_EXPLORE, MF_GRAYED); }
    if (!p || run) { EnableMenuItem(m, ID_C_CHECK, MF_GRAYED); EnableMenuItem(m, ID_C_HTML, MF_GRAYED); EnableMenuItem(m, ID_C_EXE, MF_GRAYED); EnableMenuItem(m, ID_C_APK, MF_GRAYED); }
    TrackPopupMenu(m, TPM_RIGHTALIGN | TPM_TOPALIGN, br.right, br.bottom + SC(6), 0, hMain, NULL); DestroyMenu(m);
}
/* 창 그림을 BMP 로 (시험용) */
static void saveShotOf(HWND hw, const wchar_t *path) {
    RECT r; GetWindowRect(hw, &r); int w = r.right - r.left, h = r.bottom - r.top;
    HDC sc = GetDC(NULL), mdc = CreateCompatibleDC(sc); HBITMAP bm = CreateCompatibleBitmap(sc, w, h); HGDIOBJ old = SelectObject(mdc, bm);
    if (!PrintWindow(hw, mdc, 2)) BitBlt(mdc, 0, 0, w, h, sc, r.left, r.top, SRCCOPY);
    BITMAPINFOHEADER bi; ZeroMemory(&bi, sizeof bi); bi.biSize = sizeof bi; bi.biWidth = w; bi.biHeight = h; bi.biPlanes = 1; bi.biBitCount = 24; bi.biCompression = BI_RGB;
    DWORD row = ((w * 3 + 3) & ~3), sz = row * h; char *px = malloc(sz);
    GetDIBits(mdc, bm, 0, h, px, (BITMAPINFO*)&bi, DIB_RGB_COLORS);
    BITMAPFILEHEADER fh; ZeroMemory(&fh, sizeof fh); fh.bfType = 0x4D42; fh.bfOffBits = sizeof fh + sizeof bi; fh.bfSize = fh.bfOffBits + sz;
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) { DWORD wr; WriteFile(f, &fh, sizeof fh, &wr, NULL); WriteFile(f, &bi, sizeof bi, &wr, NULL); WriteFile(f, px, sz, &wr, NULL); CloseHandle(f); }
    free(px); SelectObject(mdc, old); DeleteObject(bm); DeleteDC(mdc); ReleaseDC(NULL, sc);
}
static void saveShot(const wchar_t *path) { saveShotOf(hMain, path); }
/* 시험용: 창을 띄워 그림만 남기고 닫음 (설정 · APK) */
static void shotDialog(HWND d, const wchar_t *suffix) {
    ShowWindow(d, SW_SHOWNOACTIVATE); UpdateWindow(d);
    MSG m; DWORD t0 = GetTickCount(); while (GetTickCount() - t0 < 600) { while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&m); DispatchMessageW(&m); } Sleep(20); }
    wchar_t p[MAX_PATH + 16]; _snwprintf(p, MAX_PATH + 16, L"%ls-%ls.bmp", SELFOUT, suffix); p[MAX_PATH + 15] = 0; saveShotOf(d, p); DestroyWindow(d);
}
/* --selftest: 열기 → 검사 → 실행 → HTML 만들기 → 자동완성 → 설명서 → 끝 (차례대로 · 자식이 끝날 때마다 다음) */
static int stStep;
static void selftestNext(void) {
    wchar_t p[MAX_PATH + 8];
    switch (stStep++) {
    case 0: logLine(L"[selftest] check"); runJdhCmd(L"check", L"jdh check"); return;
    case 1: logLine(L"[selftest] run"); cmdRun(); return;
    case 2: logLine(L"[selftest] html"); cmdHtml(); return;
    case 3: logLine(L"[selftest] apk"); apkCfgLoad(); if (AC.vcode < 1) AC.vcode = 1; apkStart(); return;
    case 4: {   /* 자동완성: 끝에 "jaw." 를 치고 목록을 띄움 */
        int L = GetWindowTextLengthW(hEdit); CHARRANGE cr = {L, L}; SendMessageW(hEdit, EM_EXSETSEL, 0, (LPARAM)&cr);
        SendMessageW(hEdit, EM_REPLACESEL, TRUE, (LPARAM)L"\r// jaw."); acAfterChar(L'.');
        logLine(L"[selftest] ac items=%d first=%ls", nAcShown, nAcShown ? ACI[acShown[0]].label : L"-");
        _snwprintf(p, MAX_PATH + 8, L"%ls-ac.bmp", SELFOUT); p[MAX_PATH + 7] = 0; saveShot(p);
        acClose(); SendMessageW(hEdit, EM_UNDO, 0, 0); DOCS[CURD].dirty = 0;
        /* 설명서 */
        if (NDOC) { int k = openFile(DOCP[0], 1); logLine(L"[selftest] doc %ls lines=%d ro=%d", baseName(DOCP[0]), (int)SendMessageW(hEdit, EM_GETLINECOUNT, 0, 0), k >= 0 ? DOCS[k].ro : -1); }
        logLine(L"[selftest] tabs=%d", NDOCS);
        { DlgCtx c1 = {0, 0, setCmd}; shotDialog(buildSettings(&c1), L"set"); DlgCtx c2 = {0, 0, apkCmd}; shotDialog(buildApk(&c2), L"apk"); }
        { HMENU m = menuNew(); menuAdd(m, 1, L"x", L"x", NULL, 0); DestroyMenu(m); }
        logLine(L"[selftest] ui=%ls lang=%ls", CFG.mono ? L"mono" : L"color", LANGCODE[LANG]);
        _snwprintf(p, MAX_PATH + 8, L"%ls.bmp", SELFOUT); p[MAX_PATH + 7] = 0; saveShot(p);
        int Lg = GetWindowTextLengthW(hLog); wchar_t *w = malloc((Lg + 1) * sizeof(wchar_t)); GetWindowTextW(hLog, w, Lg + 1);
        DWORD m; char *b = wideToUtf8(w, &m); _snwprintf(p, MAX_PATH + 8, L"%ls.log", SELFOUT); p[MAX_PATH + 7] = 0; writeFileA(p, b, m); free(w); free(b);
        for (int i = 0; i < NDOCS; i++) DOCS[i].dirty = 0;
        DestroyWindow(hMain); return; }
    }
}
static int DRAG; static int DRAGY0, DRAGH0;
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_FINDMSG && WM_FINDMSG) { onFindMsg((FINDREPLACEW*)lp); return 0; }
    switch (msg) {
    case WM_SIZE: layout(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC wdc = BeginPaint(h, &ps); RECT r; GetClientRect(h, &r);
        HDC dc = CreateCompatibleDC(wdc); HBITMAP bm = CreateCompatibleBitmap(wdc, r.right, r.bottom); HGDIOBJ ob = SelectObject(dc, bm);
        fillRect(dc, 0, 0, r.right, r.bottom, TH.bg); paintMain(dc);
        BitBlt(wdc, 0, 0, r.right, r.bottom, dc, 0, 0, SRCCOPY); SelectObject(dc, ob); DeleteObject(bm); DeleteDC(dc);
        EndPaint(h, &ps); return 0;
    }
    case WM_CTLCOLORLISTBOX: { HDC dc = (HDC)wp; SetBkColor(dc, TH.panel2); SetTextColor(dc, TH.text); return (LRESULT)brPanel2; }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *m = (MEASUREITEMSTRUCT *)lp;
        if (m->CtlType == ODT_MENU) { menuMeasure(m); return TRUE; }
        if (m->CtlType == ODT_LISTBOX) { m->itemHeight = SC(26); return TRUE; }
        break;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
        if (d->CtlType == ODT_MENU) { menuDraw(d); return TRUE; }
        if (d->CtlType == ODT_BUTTON) { drawBtn(d); return TRUE; }
        if (d->CtlType == ODT_LISTBOX && d->itemID != (UINT)-1) {   /* 자동완성 줄: 이름 · 설명 (흐리게) */
            RECT rc = d->rcItem; int sel = (d->itemState & ODS_SELECTED) != 0;
            fillRect(d->hDC, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, sel ? mix(TH.panel2, TH.accent, 60) : TH.panel2);
            if (sel) fillRect(d->hDC, rc.left, rc.top, SC(3), rc.bottom - rc.top, TH.accent);
            wchar_t t[300]; SendMessageW(d->hwndItem, LB_GETTEXT, d->itemID, (LPARAM)t);
            wchar_t *tab = wcschr(t, L'\t'); if (tab) *tab++ = 0;
            textAt(d->hDC, hFontCode, TH.text, rc.left + SC(10), rc.top, rc.right - rc.left - SC(14), rc.bottom - rc.top, t, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            if (tab) { int lw = textW(d->hDC, hFontCode, t) + SC(18); textAt(d->hDC, hFontSmall, TH.dim, rc.left + SC(10) + lw, rc.top, rc.right - rc.left - lw - SC(16), rc.bottom - rc.top, tab, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS); }
            return TRUE;
        }
        break;
    }
    case WM_SETCURSOR: { POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt); RECT r; GetClientRect(h, &r); int py = r.bottom - logH(r.bottom) - PHEAD;
        if ((HWND)wp == h && pt.x >= EXW && pt.y >= py && pt.y < py + SC(10)) { SetCursor(LoadCursor(NULL, IDC_SIZENS)); return TRUE; } break; }
    case WM_LBUTTONDOWN: { RECT r; GetClientRect(h, &r); int y = GET_Y_LPARAM(lp), py = r.bottom - logH(r.bottom) - PHEAD;
        if (GET_X_LPARAM(lp) >= EXW && y >= py && y < py + SC(10)) { DRAG = 1; DRAGY0 = y; DRAGH0 = LOGH; SetCapture(h); } return 0; }
    case WM_MOUSEMOVE: if (DRAG) { LOGH = DRAGH0 + MulDiv(DRAGY0 - GET_Y_LPARAM(lp), 96, DPI); if (LOGH < 60) LOGH = 60; layout(); } return 0;
    case WM_LBUTTONUP: if (DRAG) { DRAG = 0; ReleaseCapture(); } return 0;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= ID_BTN && id < ID_BTN + B_N) {
            switch (id - ID_BTN) {
            case B_RUN: cmdRun(); break; case B_STOP: cmdStop(); break; case B_CHECK: runJdhCmd(L"check", L"jdh check"); break;
            case B_MENU: mainMenu(); break;
            case B_ADDFILE: MENUPATH[0] = 0; cmdNewFile(0); break; case B_ADDDIR: MENUPATH[0] = 0; cmdNewFile(1); break;
            case B_CLEAR: SetWindowTextW(hLog, L""); LOGBOL = 1; break;
            }
            SetFocus(hEdit); return 0;
        }
        switch (id) {
        case ID_C_NEWFILE: MENUPATH[0] = 0; cmdNewFile(0); return 0;
        case ID_C_NEWP: cmdNewProject(); return 0;
        case ID_C_OPEN: cmdOpen(); return 0;
        case ID_C_SAVE: saveAll(); tabsChanged(); return 0;
        case ID_C_CHECK: runJdhCmd(L"check", L"jdh check"); return 0;
        case ID_C_HTML: cmdHtml(); return 0;
        case ID_C_EXE: runJdhCmd(L"build exe", L"jdh build exe"); return 0;
        case ID_C_APK: cmdApk(); return 0;
        case ID_C_RUNASK: CFG.askRun = !CFG.askRun; cfgSave(); return 0;
        case ID_C_SETTINGS: cmdSettings(); return 0;
        case ID_C_EXPLORE: MENUPATH[0] = 0; /* 프로젝트 폴더 */
        case ID_M_EXPLORE: { wchar_t d[MAX_PATH]; menuDir(d); ShellExecuteW(hMain, L"explore", d, NULL, NULL, SW_SHOWNORMAL); return 0; }
        case ID_M_NEWFILE: cmdNewFile(0); return 0;
        case ID_M_NEWDIR: cmdNewFile(1); return 0;
        case ID_M_RENAME: cmdRename(); return 0;
        case ID_M_DELETE: cmdDelete(); return 0;
        case ID_EDIT:
            if (HIWORD(wp) == EN_CHANGE && !COLORING && CURD >= 0) { if (!DOCS[CURD].dirty) { DOCS[CURD].dirty = 1; tabLabel(CURD); } SetTimer(h, TIMER_COLOR, 250, NULL); InvalidateRect(hGut, NULL, FALSE); InvalidateRect(hTabs, NULL, FALSE); }
            if (HIWORD(wp) == EN_VSCROLL || HIWORD(wp) == EN_UPDATE) InvalidateRect(hGut, NULL, FALSE);
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
        else if (nh->idFrom == ID_TREE && nh->code == NM_CUSTOMDRAW) {   /* 파일 종류마다 색 (웹 판 .tr .ic) · 고른 줄은 강조색 바탕 */
            NMTVCUSTOMDRAW *cd = (NMTVCUSTOMDRAW *)lp;
            if (cd->nmcd.dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (cd->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
                int k = (int)cd->nmcd.lItemlParam; COLORREF c = TH.text;
                if (k >= 0 && k < NTP) { const wchar_t *x = wcsrchr(TPATHS[k], L'.'), *b = baseName(TPATHS[k]); if (dirExists(TPATHS[k])) c = TH.dim; else if (x && !_wcsicmp(x, L".jb")) c = CR_PINK; else if (x && !_wcsicmp(x, L".jbl")) c = CR_BLUE; else if (x && !_wcsicmp(x, L".jbs")) c = CR_YELLOW; else if (x && !_wcsicmp(x, L".jbx")) c = CR_VIOLET; else if (x && (!_wcsicmp(x, L".png") || !_wcsicmp(x, L".jpg") || !_wcsicmp(x, L".wav") || !_wcsicmp(x, L".ogg") || !_wcsicmp(x, L".mp3"))) c = CR_MINT; (void)b; }
                else if (k < 0) c = TH.dim;
                cd->clrText = c; cd->clrTextBk = (cd->nmcd.uItemState & CDIS_SELECTED) ? mix(TH.panel, TH.accent, 42) : TH.panel; cd->nmcd.uItemState &= ~(CDIS_SELECTED | CDIS_FOCUS);
                return CDRF_NEWFONT;
            }
            return CDRF_DODEFAULT;
        }
        return 0;
    }
    case WM_APP_OUT: {
        wchar_t *w = (wchar_t*)lp; logAppend(w);
        for (wchar_t *q = w; (q = wcsstr(q, L"✖")); q++) { wchar_t full[MAX_PATH]; int ln; if (parseErr(q, full, &ln) && !ERRLINE) { wcscpy(ERRPATH, full); ERRLINE = ln; InvalidateRect(hGut, NULL, FALSE); } }
        free(w); return 0;
    }
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
static HWND mkChild(const wchar_t *cls, WNDPROC proc, int id, HINSTANCE hi) {
    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = proc; wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW); wc.lpszClassName = cls; RegisterClassExW(&wc);
    return CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hMain, (HMENU)(INT_PTR)id, hi, NULL);
}
static HFONT mkFont(int px, int weight, const wchar_t *face) { return CreateFontW(-SC(px), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face); }
static void createUI(HINSTANCE hi) {
    LoadLibraryW(L"Msftedit.dll");
    { HDC dc = GetDC(NULL); DPI = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc); if (DPI < 96) DPI = 96; }
    INITCOMMONCONTROLSEX ic = {sizeof ic, ICC_TREEVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&ic);
    WM_FINDMSG = RegisterWindowMessageW(FINDMSGSTRINGW);
    themeSet(CFG.mono);
    WNDCLASSEXW wc; ZeroMemory(&wc, sizeof wc); wc.cbSize = sizeof wc; wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; wc.lpszClassName = L"JawBreakerDev"; wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    RegisterClassExW(&wc);
    hMain = CreateWindowExW(0, L"JawBreakerDev", L"JawBreaker Dev.", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, SC(1280), SC(820), NULL, NULL, hi, NULL);
    darkTitle(hMain);
    /* 글꼴: 화면 글자 = Segoe UI (한글은 맑은 고딕으로 이어짐) · 코드 = Consolas (웹 판 --fs 14px · 줄 22px) */
    hFontUI = mkFont(13, FW_NORMAL, L"Segoe UI"); hFontBold = mkFont(13, FW_SEMIBOLD, L"Segoe UI"); hFontBrand = mkFont(17, FW_BOLD, L"Segoe UI");
    hFontSmall = mkFont(11, FW_NORMAL, L"Segoe UI"); hFontEmoji = mkFont(15, FW_NORMAL, L"Segoe UI Emoji");
    hFontCode = mkFont(14, FW_NORMAL, L"Consolas"); hFontLog = mkFont(13, FW_NORMAL, L"Consolas"); hFontPath = mkFont(13, FW_NORMAL, L"Consolas");
    for (int i = 0; i < B_N; i++) hBtn[i] = mkBtn(hMain, ID_BTN + i, L"", i == B_RUN ? BS_J_PRIMARY : i == B_CLEAR ? BS_J_GHOST : BS_J_PLAIN);
    SendMessageW(hBtn[B_MENU], WM_SETFONT, (WPARAM)hFontBrand, TRUE);
    btnTexts();
    hTree = CreateWindowExW(0, WC_TREEVIEWW, L"", WS_CHILD | WS_VISIBLE | TVS_HASBUTTONS | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_FULLROWSELECT | TVS_TRACKSELECT, 0, 0, 10, 10, hMain, (HMENU)ID_TREE, hi, NULL);
    SendMessageW(hTree, WM_SETFONT, (WPARAM)hFontPath, TRUE); SendMessageW(hTree, TVM_SETITEMHEIGHT, SC(28), 0); SendMessageW(hTree, TVM_SETINDENT, SC(16), 0); darkScroll(hTree);
    hTabs = mkChild(L"JBDevTabs", TabsProc, ID_TAB, hi);
    hPath = mkChild(L"JBDevPath", PathProc, ID_PATH, hi);
    hGut = mkChild(L"JBDevGutter", GutProc, ID_GUT, hi);
    hEdit = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_NOHIDESEL | ES_WANTRETURN, 0, 0, 10, 10, hMain, (HMENU)ID_EDIT, hi, NULL);
    SendMessageW(hEdit, EM_SETTEXTMODE, TM_RICHTEXT | TM_MULTILEVELUNDO | TM_MULTICODEPAGE, 0);   /* 색칠하려면 서식 있는 글 (붙여 넣기는 글자만) */
    SendMessageW(hEdit, WM_SETFONT, (WPARAM)hFontCode, TRUE); SendMessageW(hEdit, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SCROLL | ENM_UPDATE); SendMessageW(hEdit, EM_EXLIMITTEXT, 0, 64 * 1024 * 1024);
    { PARAFORMAT2 pf; ZeroMemory(&pf, sizeof pf); pf.cbSize = sizeof pf; pf.dwMask = PFM_TABSTOPS | PFM_LINESPACING; pf.cTabCount = 32; for (int i = 0; i < 32; i++) pf.rgxTabs[i] = (i + 1) * 4 * 120; pf.bLineSpacingRule = 4; pf.dyLineSpacing = 330; SendMessageW(hEdit, EM_SETPARAFORMAT, 0, (LPARAM)&pf); }
    SendMessageW(hEdit, EM_SETREADONLY, TRUE, 0); darkScroll(hEdit);
    hLog = CreateWindowExW(0, MSFTEDIT_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY, 0, 0, 10, 10, hMain, (HMENU)ID_LOG, hi, NULL);
    SendMessageW(hLog, WM_SETFONT, (WPARAM)hFontLog, TRUE); SendMessageW(hLog, EM_EXLIMITTEXT, 0, 16 * 1024 * 1024); darkScroll(hLog);
    hAc = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, L"LISTBOX", L"", WS_POPUP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 10, 10, hMain, NULL, hi, NULL);
    SetWindowLongPtrW(hAc, GWLP_ID, ID_AC); SendMessageW(hAc, WM_SETFONT, (WPARAM)hFontCode, TRUE); darkScroll(hAc);
    editColors();
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
    cfgLoad(); pickLang();
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
            if (ctrl && m.wParam == 'S') { saveAll(); tabsChanged(); continue; }
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
        if (m.message == WM_KEYDOWN && m.hwnd == hEdit && ((GetKeyState(VK_CONTROL) < 0 && m.wParam == 'V') || (GetKeyState(VK_SHIFT) < 0 && m.wParam == VK_INSERT))) { SendMessageW(hEdit, EM_PASTESPECIAL, CF_UNICODETEXT, 0); continue; }   /* 붙여 넣기는 글자만 */
        if ((m.message == WM_LBUTTONDOWN || m.message == WM_MOUSEWHEEL) && m.hwnd != hAc) acClose();
        TranslateMessage(&m); DispatchMessageW(&m);
        if (m.message == WM_CHAR && m.hwnd == hEdit) acAfterChar((wchar_t)m.wParam);
        if (m.message == WM_KEYDOWN && m.hwnd == hEdit && (m.wParam == VK_BACK || m.wParam == VK_DELETE)) acRefilter();
        if (m.message == WM_LBUTTONDBLCLK && m.hwnd == hLog) { CHARRANGE cr; SendMessageW(hLog, EM_EXGETSEL, 0, (LPARAM)&cr); logJump((int)SendMessageW(hLog, EM_EXLINEFROMCHAR, 0, cr.cpMin)); }
    }
    LocalFree(argv);
    return 0;
}
