# JDH (JawBreaker Development Helper)

Jawbreaker 는 이 저장소 주인이 만드는 프로그래밍 언어와 게임 엔진이고, JDH 는 그 Windows 개발 환경이다.
답은 한국어 존댓말로, 결론부터 짧게 한다.

## 구성

| 경로 | 내용 |
|---|---|
| `editor/src/jb.html` | 에디터 소스 (Lite 와 Dev. 공통 · 엔진 · 내장 실행기 포함, 약 3MB) |
| `editor/build.py` | 소스 → `editor/out/JawBreaker-Lite.html` · `editor/jbdev.html` (Dev.) |
| `editor/jbdev.html` | 웹 판 빌드 결과 (내보낸 HTML 의 틀 · 직접 고치지 말 것) |
| `src/jdh.c` | `jdh` 명령 (new lib · lib check · lib build · lib add · build exe) |
| `src/jbdev.c` | JawBreaker Dev. — Windows 네이티브 에디터 (C · Win32 · HTML 을 쓰지 않음) |
| `sdk/jdh.h` | 사용자 라이브러리가 포함하는 헤더 |
| `runtime/` | C 실행기 소스 (SDL2) — EXE 내보내기에 쓰임 |
| `installer/jdh.iss` | 설치 프로그램 (Inno Setup) |
| `tests/` | 에디터 시험 (Playwright) · 시험용 라이브러리 `tests/libs/Counter` |
| `.github/workflows/build.yml` | Windows 빌드와 시험 |

## 핵심 규칙

- 에디터는 두 판이다. **Lite** 는 기존 에디터 (HTML · 브라우저 안 실행) 로 계속 지원하며 사용자 라이브러리가 봉인돼 있다 (코드가 아예 없음). **Dev.** 는 Windows 네이티브 프로그램 (`src/jbdev.c`) 으로 바꾸는 중이다 (아래).
- JawBreaker Dev. (네이티브 · 설치 프로그램에 들어감): 디스크의 프로젝트 폴더를 직접 열고 저장한다 (브라우저 저장 공간 없음). 모양은 웹 판 에디터와 거의 같게 둔다 — 색은 **흑백 판만** (웹 판 `[data-ui=mono]` 의 `--bg` · `--panel` · `--line` …) · 그라데이션 없음 (▶ 실행도 평평하게) · 구문 색 · 로그 색은 웹 판 `#hl` 그대로 (웹 판 흑백처럼).
  - 화면 = 머리 줄 (로고 · JawBreaker Dev. · ▶ 실행 · ■ · 검사 · ☰) · [탐색기 (📁 · 경로 줄을 누르면 보이기/숨기기 · ＋ 파일 · ＋ 폴더 · TreeView · 오른쪽 단추 = 새 파일 · 새 폴더 · 이름 바꾸기 · 지우기 (휴지통) · 탐색기에서 열기 · 맨 아래 「내장 설명서」 = 읽기 전용)] · 탭 줄 (직접 그림 · ✕ / 가운데 단추 / Ctrl+W 닫기 · Ctrl+Tab · 오른쪽 ↶ ↷ 💾 📁) · 경로 줄 (오른쪽 = 알림) · 줄 번호 (오류 줄 빨강) + 코드 칸 (RichEdit 서식 있는 글 · 붙여 넣기는 글자만) · 로그 머리 (위쪽을 끌면 높이) · 로그 칸 (줄마다 시각 · ✖ 빨강 · 알림 민트).
  - ☰ 메뉴 (웹 판 프로젝트 메뉴 차례): 새 파일 · 새 프로젝트 · 폴더 열기 · 저장 · 검사 · HTML 로 내보내기 · EXE 로 변환 · APK 로 변환 · 실행 전 저장 묻기 · 설정 · 탐색기에서 열기. 메뉴 · 작은 창 · 단추는 직접 그림 (어두운 모양).
  - 설정 창 (웹 판 openSettings 와 같은 항목): 언어 (5개) · 램 한도 (자동 · 256 · 512 · 1G · 2G · 무제한 → `jdh build html|apk --ram`) · 버전 (UI 고르기는 없음 — 흑백만). `%APPDATA%\JDH\dev-settings.txt` (lang · ram · runask). 시험용 `JDH_LANG` 이 언제나 이김. jbdev 의 `JDH_VER_W` 를 `JDH_VERSION` 과 같이 올린다.
  - APK 창 (웹 판 exportApk 와 같은 칸): 앱 이름 · 패키지 · 버전 번호 (지난번 + 1) · 버전 이름 · 화면 방향 · 아이콘 (프로젝트의 PNG) · 서명 키 (있는지 · 만든 날 · 내보내기 · 가져오기 = `jdh apk-key import`) → `jdh build apk`. 칸 값은 `%APPDATA%\JDH\apk-<프로젝트 폴더>.cfg`.
  - ▶ 실행 (F5) = 모두 저장 후 프로젝트의 `run.bat` (없으면 `jdh run`) 을 창 없이 띄우고 출력을 로그 칸으로 · ■ 정지 = 작업 개체 (Job) 로 자식까지 끔 · 검사 (F7) = `jdh check` · EXE = `jdh build exe` · HTML = `jdh build html --lang <화면 언어>` · 새 프로젝트 = `jdh new project`.
  - 로그 칸의 "✖ 파일:줄" 을 두 번 누르면 그 파일 · 그 줄로. 찾기 Ctrl+F · 바꾸기 Ctrl+H · 다음 F3 (Windows 찾기 창). 자동완성 = 별칭 뒤 `.` 를 치면 그 라이브러리 목록 (`Use 라이브러리 call 별칭;` 을 읽음) · Ctrl+Space = 지금 단어 (jawbreaker · 문장 틀 · `Use ` 뒤는 라이브러리 이름) · ↑↓ · Enter/Tab · Esc · `$0` 자리에 커서.
  - 설명서 · 자동완성 목록 · HTML 틀은 웹 판에서 뽑는다: `python3 editor/build.py` 다음 `node editor/gen_native.mjs` → `editor/native/` (docs/<언어>/*.jbl · ac-<언어>.txt · game-<언어>.html · 저장소에 넣음). 설치본: `editor\native\` (jbdev 가 bin\..\editor\native 에서 찾음) · `sdk\html\game-*.html` (jdh build html 의 틀 · 시험용 `JDH_HTML_TEMPLATE`).
  - 화면 글자는 `TR` 표 (5개 언어 · Windows 표시 언어 · 시험용 `JDH_LANG`). 지난 프로젝트는 `%APPDATA%\JDH\dev-last.txt`. `--selftest <프로젝트> <결과>` = 열기 → 검사 → 실행 → HTML → APK → 자동완성 → 설명서 → 설정 · APK 창 그림을 차례로 하고 로그 · 창 그림을 남김 (CI · 창 그림은 CI 로그에 PNG base64 로도 찍음 — `SHOTL <이름> …` 줄을 이어 붙여 풂).
- `jdh build apk [프로젝트] [-o] [--label] [--pkg] [--version-code] [--version-name] [--orientation auto|landscape|portrait] [--icon 그림.png] [--lang] [--ram]`: 웹 판 `JBX.apk` 를 C 로 옮긴 것 — manifest (바이너리 XML · 웹 판과 바이트까지 같음) · zip (압축 없이 · 4 바이트 정렬) · APK 서명 v2 (RSA 2048 · SHA-256). 게임은 `jdh build html` 과 같은 HTML (`assets/index.html`) · 네트워크 라이브러리가 있으면 INTERNET 권한. 실행기 틀 = `editor/native/apk-tpl.json` (gen_native 가 웹 판 `#jbapk-tpl` 에서 뽑음 → 설치본 `sdk\apk\`). 아이콘은 PNG 를 그대로 넣음 (웹 판은 192 로 줄임 — 차이).
  - 서명 키: `%APPDATA%\JDH\apk-key.json` (`JDH_APK_KEY` 로 바꿈) = 웹 판 「키 내보내기」 파일과 같은 꼴 (`pkcs8` · `cert` · `made`) — 서로 가져올 수 있다 (로컬에서 양쪽으로 확인). 없으면 새로 만든다. RSA 는 Windows CNG (NCrypt · `-lncrypt`) · 그 밖 (시험용) 은 `openssl` 명령. `jdh apk-key [path | export <파일> | import <파일>]`.
  - 시험: CI 의 APK 단계 (Android SDK 의 `apksigner verify` · `aapt dump badging` · 키가 PKCS#8 인지) · jbdev selftest 도 APK 를 만듦.
- `jdh check [프로젝트]`: `jdh run` 과 같은 실행기로 `--check` (실행하지 않고 검사 · 오류는 "✖ 파일:줄  메시지"). 실행기의 `--no-box` = 오류를 메시지 상자 없이 콘솔에만 (jdh run · check 가 씀).
- `jdh build html [프로젝트] [-o 파일] [--lang 언어]`: 틀의 `/*__JB_PROJECT__*/` 자리에 프로젝트 JSON (`name` · `main` · `files` = 글자 파일은 글자 · 그림 · 소리 · 글꼴 · .jbx 는 data: 주소 · run.bat 은 뺌) · `__JB_TITLE__` 에 이름. 사용자 라이브러리의 files.read · network 는 선언한 대로 허락해 넣는다 (`extGrant` · `extNet`).
- 사용자 라이브러리는 **C 로 한 번만** 쓴다. 웹 (Dev. 실행 · HTML · APK) 에서는 `jdh lib build` 가 WebAssembly 로 컴파일한 것을 격리된 워커에서 돌리고, EXE 에서는 `jdh build exe` 가 같은 C 소스를 실행기와 함께 컴파일한다.
- 컴파일러는 zig (`zig cc`). 웹은 `wasm32-wasi`, EXE 는 `x86_64-windows-gnu`.
- 웹에서 라이브러리는 에디터 · 저장 공간에 닿지 못한다. 프로젝트 파일은 선언에 `files.read` 권한이 있고 사용자가 허락한 경우에만, 그 프로젝트의 파일을 읽기 전용으로 연다 (쓰기는 없음 · `ext/` 는 못 읽음). 허락은 신뢰 확인 창에서 고른다. 내보낸 HTML · APK 는 에디터에서 허락한 것을 그대로 가진다. EXE 는 격리가 없으므로 선언만 있으면 읽는다.
- 네트워크 (`network` 권한 · 엔진 1.5 부터): HTTP 요청 (`jdh_http_*`) 과 WebSocket (`jdh_ws_*`). 모두 기다리지 않는다 — 걸어 두고 다음 명령 · `JDH_FRAME` 에서 상태를 물어본다.
  - 웹에서는 선언에 `network` 가 있고 사용자가 신뢰 확인 창에서 허락한 경우에만 접속한다 (허락하지 않으면 시작하지 못함 = 0). 허락하면 어느 서버든 접속할 수 있다 — 창에 "읽은 것을 밖으로 보낼 수 있음" 을 알린다. 허락은 `jawbreaker.jbx.trust` 의 `net` 에 기억하고, 내보낸 HTML · APK 는 에디터에서 허락한 것을 그대로 가진다 (`PROJECT.extNet` · APK 는 INTERNET 권한을 넣음). EXE 는 격리가 없으므로 선언만 있으면 된다.
  - 웹은 워커 안에서 `fetch` · `WebSocket` 을 쓴다 (`JBX_BOOT` 의 `HOST`). 그래서 브라우저 규칙을 따른다: HTTP 는 서버가 CORS 를 허용해야 응답을 읽고, https 페이지에서는 http · ws 에 접속하지 못한다. EXE (`runtime/src/net.c` · WinHTTP · 스레드) 에는 이 제한이 없다 — 차이로 남아 있음.
  - 한도: 동시에 64개 · 응답 본문과 WebSocket 에 쌓인 메시지는 16MB.
  - 시험: `tests/libs/Net` · `tests/fixtures/net-main.jb` · `tests/net_server.py` (HTTP + WebSocket) 를 웹 (`tests/dev.mjs`) 과 EXE (CI) 가 같이 돌린다. Windows 가 아닌 빌드의 `net.c` 는 소켓으로 http · ws 만 한다 (시험용).
- 서버 (`server` 권한 · 엔진 1.6 · EXE 에서만): 라이브러리 C 코드에서 `jdh_srv_listen(포트, JDH_SRV_LOCAL 또는 0)` 으로 포트를 열고, 한 포트에서 HTTP 와 WebSocket 을 같이 받는다. `JDH_FRAME` (또는 명령) 에서 `jdh_srv_next` (HTTP 요청) · `jdh_srv_ws` (새 WebSocket) 로 꺼낸다 — 기다리지 않음.
  - HTTP: `jdh_req_info` (방법 · 경로 · 머리 · 보낸 곳) · `jdh_req_size` · `jdh_req_read` → `jdh_req_respond(번호, 상태, 머리, 본문, n)`. 요청마다 연결을 닫는다 (Connection: close) · 30초 안에 답하지 않으면 엔진이 503 · Content-Length 없는 chunked 본문은 411.
  - WebSocket: 받은 번호는 클라이언트와 같은 `jdh_ws_state · send · peek · recv · close` 로 쓴다 (이 다섯은 `network` 나 `server` 어느 쪽 권한으로도 씀 — 표에 `"network|server"`).
  - https · wss: `jdh_srv_listen_tls(포트, flags, "인증서.pfx", "비밀번호")` — Windows 의 SChannel (`-lsecur32 -lcrypt32 -lncrypt`). .pfx 는 프로젝트 파일 (EXE 에 들어감) 에서 먼저 찾고, 없으면 디스크 (절대 경로 · 지금 폴더 · 실행 파일 폴더). 개인 키는 여는 동안 사용자 키 저장소에 두고 서버를 닫을 때 지운다 (강제 종료면 남을 수 있음). 그 밖 (요청 · WebSocket) 은 평문 서버와 같은 함수 · 같은 코드 (`Io` = 소켓 또는 TLS). Windows 가 아닌 빌드는 늘 0.
  - 구현: `runtime/src/net.c` 의 "서버" 부분 (소켓 · Windows 는 Winsock `-lws2_32` · 접속마다 스레드 · SHA-1 은 직접). 한도: 서버 8개 · 받은 접속 192개 (클라이언트 64개와 따로) · 머리 64KB · 본문 16MB.
  - 웹은 브라우저 안이라 포트를 열 수 없다: `jdh_srv_listen` 이 늘 0 이고 로그에 "EXE 에서만" 을 알린다.
  - 시험: `tests/libs/Server` · `tests/fixtures/server-main.jb` (Net 라이브러리로 자기 자신에 접속 · 웹은 "server off" 와 compound 줄만) — EXE 는 CI 의 서버 시험 단계. TLS 는 CI 의 https · wss 단계 (`tests/fixtures/server-tls-main.jb` · 스스로 서명한 인증서를 그 PC 의 루트에 넣고 엔진의 WinHTTP 클라이언트 · `tests/tls_client.py` · curl 로 접속).
- 라이브러리 값 형식 `compound` (엔진 1.6): C 는 `JDH_VALUE(const char *, 이름)` 으로 JSON 글자를 돌려주고 엔진이 compound 로 바꾼다. 규칙은 `jaw.loadJson` 과 같다 (`[ ]` · 숫자 · 문자열 · true/false → 1/0 · null → 0 · 맨 바깥이 배열이 아니면 `[값]` · 빈 글자 = `[]` · `{ }` 나 틀린 JSON 은 그 값을 읽는 줄에서 오류). 웹 (`JBEXT.val`) 과 EXE (`run.c` 의 `ext_value`) 가 같은 문구.
- `data` 형식 (변수 형식 · Lite 와 Dev. 모두): 무엇이든 넣고 무엇에든 넣을 수 있는 바이트 묶음. 규칙은 에디터 (`toData` · `fromData` · `dco`) 와 C (`value.c` 의 `to_data` · `from_data` · `run.c` 의 `dco`) 를 같게 둔다.
  - 바이트는 복사하지 않고 나눠 쓴다: EXE 는 `Data.rc` (참조 수 · `v_copy` 는 rc 만 올림), 웹은 나눠 준 `Uint8Array` 에 `__sh` 표시. setPart 는 혼자 쓰는 바이트면 그 자리에서 (웹은 같은 길이일 때), 나눠 쓰는 중이면 새로 만든다. 프로젝트 파일 ↔ 바이트 · UTF-8 판정 · 쓰기 금지 경로는 core 의 `fileBytes` · `bytesFile` · `utf8Of` · `noWritePath` (EXE 는 `run.c` 의 `noWritePath`) 한 곳에만 둔다.
  - 넣을 때: 숫자 → 8바이트 실수 (big-endian) · 문자열 → UTF-8 · 행렬 · 리스트 · compound → JSON 글자 · data → 나눠 씀 (복사 없음).
  - 꺼낼 때: 받는 형식으로 억지로 읽고 안 맞으면 건너뜀 — 숫자는 8바이트일 때만 · 문자열은 올바른 UTF-8 (0 바이트 없음) · 리스트 · 행렬 · compound 는 JSON `[ ]` (행렬은 크기까지). 건너뛰면 대입은 원래 값 그대로 · 선언 · 매개변수 · 돌려줄 값은 기본값.
  - print · 문자열과 `+` · `%이름%`: 16진수 (64바이트가 넘으면 앞 32바이트 + `… (N B)`) · JSON 으로는 16진수 전체. 계산 (`+ - == …`) 은 검사 오류 · 리스트 · compound 에 못 넣음.
  - 일부분: `data.이름.getPart(시작, 끝)` (식 · data) · `data.이름.setPart(시작, 끝, 값);` (그 구간을 값의 바이트로 · 길이가 달라도 됨). 번호는 0 부터 · 끝은 빠짐 · 범위 밖은 잘라 냄.
  - 파일: `jaw.copyFileData("경로") as ("data 변수");` (프로젝트 파일의 바이트를 그 변수에 · 이번 실행에서 쓴 파일도) · `jaw.overWriteFile(값, "경로");` (쓰기는 genJson 과 같은 곳 · 코드 .jb · .jbl · project.jbs · .jbx · ext/ 는 거부). 시험: `tests/fixtures/datafile-main.jb` (웹 · EXE).
  - 시험: `tests/fixtures/data-main.jb` 를 웹 (`tests/dev.mjs`) 과 EXE (CI) 가 같이 돌려 같은 줄이 나오는지 본다.
- 프로젝트 폴더의 `run.bat` (`jdh new project` 가 만듦): 두 번 누르면 `jdh run .` 으로 main 을 실행한다 — 서버처럼 계속 도는 프로그램을 띄우기 쉽게. `jdh run` 은 콘솔 판 실행기 (`-mconsole` · print 가 그 창에 보임) 를 `build/jdh/run-base.exe` 에 두고 (JDH 버전 · SDK · `ext/*.jbx` 의 이름 · 크기 · 고친 때가 같으면 다시 컴파일하지 않음 · `run-base.exe.key`) 프로젝트 파일만 붙여 `build/jdh/run/<이름>.exe` 로 실행한다. 종료 코드를 그대로 돌려준다 (0 이 아니면 run.bat 이 창을 멈춰 둠). 시험: CI 의 run.bat 단계.
- `jdh_log` 는 `print` 와 같은 디버그 출력이다. 에디터에서는 로그 패널에, EXE 에서는 표준 출력에 나온다 (창 프로그램이라 화면에는 안 보임).
- 엔진 버전은 1.6 (에디터의 `JB_VER` · `JBX_ENGINE` · `src/jdh.c` 의 `JDH_ENGINE_*` 를 같이 올린다). 라이브러리 선언의 `requires.engine` (">=1.2" 꼴) 을 `jdh lib check` 와 에디터가 검사한다.
- 신뢰 확인 (Dev.): `.jbx` 내용의 SHA-256 마다 한 번 묻고 `localStorage` 의 `jawbreaker.jbx.trust` 에 기억한다 (다시 구우면 다시 물음). 창에는 버전 · 명령어 · 값 개수 · 쓰는 권한 · "EXE 에서는 격리 없음" 을 보여 주고, files.read 가 있으면 허락 여부도 고른다. 묻는 때: 「라이브러리 불러오기 (.jbx)」 메뉴 · 파일로 넣을 때 (불러오기 · 끌어 놓기) · 프로젝트 zip 등으로 들어온 것은 처음 실행할 때. 신뢰하지 않은 라이브러리를 쓰는 프로그램은 실행하지 않는다. 코드는 에디터 쪽 DEV 구간 (`jbxTrustRun` · `jbxImport` · `jbxDocText` · `jbxAcMembers`) — 내보낸 게임에는 없다.
- `ext/*.jbx` 를 열면 설명서 (선언 · 권한 · `doc/` 파일) 를 읽기 전용으로 보여 준다. 자동완성은 `Use ` · `별칭.` · 문자열 안 `%별칭.값%` 에서 라이브러리의 명령어 · 값을 낸다.
- 엔진 기능 (host 함수) 은 `sdk/jdh.h` 에 선언하고 웹 (`JBX_BOOT` 의 `HOST` · `JBEXT`) 과 EXE (`runtime/src/gfx.c` · `render.c` · `run.c` 의 `jdh_*`) 에 같은 뜻으로 구현한다. 함수 → 권한 표는 `src/jdh.c` 의 `HOSTFN` 과 에디터의 `JBX_HOST` 를 같게 둔다.
  - `draw`: 라이브러리마다 그리기 층 하나 (사각형 · 타원 · 선 · 글자) · 지울 때까지 남음 · 화면 단위. 놓이는 곳은 `jdh_draw_layer(층, n)` 으로 고른다 (그 뒤에 그리는 것부터 · 엔진 1.4 부터): `JDH_LAYER_OWN` = 오브젝트 위 · UI 아래 (기본) · `JDH_LAYER_OBJECT` = 오브젝트 사이 (n = `dis.setObjLayer` 와 같은 번호 — 번호가 n 이하인 오브젝트 위 · n 보다 큰 오브젝트 아래) · `JDH_LAYER_UI` = 화면에 고정 (카메라를 따르지 않음 · 배율 1) · UI 오브젝트 위. `jdh_draw_clear` 는 놓인 곳과 상관없이 모두 지운다. 시험: `tests/libs/Layers` · `tests/fixtures/layers-main.jb`.
  - `objects.read`: 위치 · 크기 · 속도 · 색 · 글자. `objects.write`: 같은 것을 바꿈 (없는 오브젝트는 오류). 만들기 · 지우기는 없다.
  - 권한은 컴파일된 `lib.wasm` 이 가져다 쓰는 host 함수로 확인한다: `jdh lib check` · `jdh build exe` · 에디터 (불러올 때) 세 곳 모두 거부한다.
  - 웹에서는 엔진이 바뀐 오브젝트를 명령 묶음과 화면 프레임마다 워커로 보내고, 쓰기 · 그리기는 명령 묶음 · 프레임이 끝날 때 엔진으로 온다 (한 박자 늦을 수 있음 — 값과 같은 규칙). EXE 는 바로.
  - 시험: `tests/libs/Probe` (모든 기능) · `tests/libs/NoPerm` (권한 없이 씀 → 거부) · `tests/fixtures/probe-main.jb` 를 웹 (`tests/dev.mjs`) 과 EXE (CI) 가 같이 돌려 같은 줄이 나오는지 본다.
- `jdh build exe` 결과물: 디버그 정보를 뺀다 (`-s` · `.pdb` 도 안 생김). 아이콘과 고해상도 화면 설정 (`runtime/win/app.manifest` · PerMonitorV2) 을 리소스로 넣는다. 아이콘은 프로젝트 폴더의 `icon.ico` 가 있으면 그것, 없으면 `runtime/win/icon.ico`. 설치본에는 `sdk/runtime/win/` 으로 들어간다.
- 업데이트 (`jdh update` · `src/jdh.c` 의 "업데이트" 부분): 새 버전은 GitHub 릴리스에서 받는다. 방식은 무결성 검사 — 파일 목록 (파일마다 SHA-256) 과 설치된 파일을 대조해 다른 파일만 받고, 받은 것을 다시 검사한 뒤 한꺼번에 바꾼다.
  - 릴리스의 파일: `jdh-latest.txt` (버전 · 목록의 SHA-256) · `jdh-manifest.txt` (파일마다 묶음 · 시작 · 크기 · SHA-256 · 경로) · `jdh-core.pack` · `jdh-zig.pack` (파일을 이어 붙인 것 — 필요한 자리만 HTTP Range 로 받음). `jdh pack-update <dist> <out> <버전>` 이 만든다 (CI).
  - 하나라도 SHA-256 이 틀리면 아무것도 바꾸지 않는다. 바꾸다 실패하면 되돌린다. 같은 버전이어도 파일이 깨졌으면 고친다. 지난 목록 (`{설치 폴더}/jdh-manifest.txt`) 에 있고 새 목록에 없는 파일은 지운다. 목록의 경로는 설치 폴더 안의 상대 경로만 받는다.
  - `jbdev.exe` 는 열 때 `jdh update --check` (5초 한도 · 실패하면 조용히 넘어감) 로 확인하고, 새 버전이면 "업데이트하시겠습니까?" 를 묻는다 (예 · 아니요 = 그 버전 건너뛰기 · 취소 = 다음에). `JDH_NO_UPDATE=1` 이면 확인하지 않는다. `JDH_UPDATE_URL` 로 받는 곳을 바꿀 수 있다 (시험용 · 폴더도 됨).
  - 게시: 버전 태그 (`v0.6.0` 꼴 · `JDH_VERSION` 과 같아야 함) 를 올리면 CI 가 릴리스로 게시한다. main 에 올리는 것만으로는 게시되지 않는다. **태그는 요청받았을 때만 만든다.**
  - 내려받은 것이 맞는지는 HTTPS 와 SHA-256 으로 본다 (서명은 없음 — GitHub 계정이 믿음의 뿌리).
- 실행이 끝난 뒤: 웹은 라이브러리를 끄고 그리기 층은 다음 실행까지 화면에 남긴다. EXE 는 창이 열려 있는 동안 `JDH_FRAME` 이 계속 불린다 (웹은 멈춤 — 차이로 남아 있음).
- 명령어 인자 형식 (`jdl.json` 의 `params`): `int` · `float` · `string` · `bit` · `color` · `mat12` · 리스트 · 행렬 (리스트 · 행렬은 엔진 1.3 부터). 규칙은 `src/jdh.c` 의 `ptParse` 와 에디터의 `jbxPT` 를 같게 둔다.
  - 리스트 `"[int, string]"`: 항목 수가 정해짐 (8개까지 · 항목은 int · float · string · bit) → C 에서는 항목마다 인자 하나 (`int a, const char *b`).
  - 리스트 `"[int, ...]"`: 몇 개든 (0개도 · `...` 는 이 꼴로만) → 배열과 개수 (`const int *items, int n` · float 은 `const double *` · string 은 `const char *const *`).
  - 행렬 `"matrix[2, 3]"` → `const double *m` (행 순서). 크기 자리의 `...` 는 아무 크기이고, 그 크기가 뒤에 붙는다 (`matrix[..., 3]` → `const double *m, int rows`).
  - 리스트 인자는 list · compound 변수와 `[ ]` 를 받고, 행렬 인자는 행렬만 받는다. 검사 때 아는 것 (형식 · `[ ]` 의 항목 수 · 행렬 크기) 은 검사기가, 나머지 (항목 수 · 항목 형식) 는 실행할 때 그 줄에서 오류를 낸다 — 웹 (`jbxArg`) 과 EXE (`run.c` 의 `ext_call`) 가 같은 문구. int 항목은 소수점 아래를 버린다.
  - C 에서 인자는 모두 합쳐 64칸까지 (리스트 항목 · 개수 · 크기 포함). 웹에서는 인자를 `jdh_scratch` (64KB 를 넘으면 malloc) 에 넣는다.
  - 시험: `tests/libs/Lists` · `tests/fixtures/lists-main.jb` 를 웹 (`tests/dev.mjs`) 과 EXE (CI) 가 같이 돌린다.
- `.jbx` 에는 `lib.wasm` 이 반드시 있어야 한다 (`jdh build exe` 가 init · frame · stop 이 있는지를 거기서 읽음 — 없으면 오류).
- 명령어는 값을 돌려주지 않는다 (엔진의 식 계산이 기다릴 수 없음). 돌려줄 것은 "값" 으로 선언하고 코드에서 `별칭.값이름` 으로 읽는다. 웹에서는 명령 직후 읽으면 한 박자 늦은 값일 수 있고, EXE 에서는 바로 새 값이다.
- `.jbx` 형식: `"JBLX"` · u32 개수 · (u32 경로 길이 · 경로 · u32 크기 · 내용)… — 안에 `jdl.json` · `lib.wasm` · `native/*.c` · `doc/*`. (`"JBX1"` 은 `jaw.exportFile` 의 난독화 파일이 쓰는 표시라 쓰지 않는다.)
- 결정 기준은 실행 성능이다. 단 격리는 안전을 우선한다.
- 에디터의 화면 글자는 5개 언어 (ko · en · ja · zh-CN · zh-TW) 다. 새 문구는 `I18N_EN` 과 `I18N_X` 의 같은 자리에 넣는다.

## 작업 방식

- 에디터를 고치면: `python3 editor/build.py` → `node tests/dev.mjs` · `node tests/lite.mjs` (Playwright 필요 · `PW` 와 `CHROME` 환경 변수로 위치 지정 가능).
- 라이브러리 굽기 시험: `JDH_ZIG="python3 -m ziglang" JDH_SDK=sdk ./jdh lib build tests/libs/Counter` (`pip install ziglang`).
- Windows 에서만 확인되는 것은 GitHub Actions 로 확인한다. main 에 올리면 빌드가 돌고, 라이브러리를 쓰는 프로젝트를 EXE 로 만들어 실제 실행까지 한다.
- 확인하지 못한 것은 확인하지 못했다고 말한다.
- 설계에서 벗어나는 결정은 먼저 묻는다.
- 릴리스 게시처럼 밖으로 공개되는 일은 요청받았을 때만 한다.

## 남은 일 (순서대로)

1. 창을 띄우는 게임 (DisPlayer) 을 `jdh build exe` 로 만든 EXE 의 실제 화면 확인 (CI 는 dummy 화면에서 실행 · 화면 저장까지만)
2. APK 에서의 라이브러리 동작 확인
3. `jbdev.exe` (네이티브 에디터) 설치 후 실제 PC 에서의 동작 확인 (설치 · PATH · 바로 가기 · 창 · 한글 입력 · 화면 배율)

## 아직 확인하지 못한 것

- 설치 파일을 실제 PC 에서 실행한 적이 없다.
- 격리 방식 (권한을 뺀 프레임 안의 워커) 은 헤드리스 Chrome 에서만 확인했다. Edge 앱 창 · Android WebView 는 미확인.
- 번역 (ja · zh-CN · zh-TW) 은 원어민 검수를 받지 않았다.
- data 형식: 에디터의 "EXE 로 내보내기" 에 든 미리 만든 실행기 (`jbrt-win`) 는 다시 만들지 않아 이 기능이 없다 (`jdh build exe` 는 됨).
- `run.bat`: 실제 PC 에서 두 번 눌러 본 적은 없다 (CI 에서 cmd 로 실행 · 콘솔 프로그램만).
- APK (`jdh build apk`): 실제 Android 기기에 설치해 본 적이 없다 (CI 는 apksigner · aapt 로 검사만). 웹 판 키로 만든 APK 가 웹 판 APK 위에 업데이트로 설치되는지도 기기에서 확인하지 못했다.
- 네이티브 에디터의 새 모양: CI 창 그림으로만 봤다 (실제 PC 의 화면 배율 · 한글 입력 · 메뉴 · 콤보 상자 어두운 모양은 미확인).
- 서버 (`server`): 실제 PC 의 방화벽 창 (모든 주소로 열 때) · 다른 PC 에서의 접속 · 공인 인증서 (Let's Encrypt 등) 로 연 https · 브라우저에서의 wss 접속은 확인하지 못했다 (CI 는 같은 PC 의 127.0.0.1 · 스스로 서명한 인증서).
- 네트워크: https · wss (실제 인터넷의 서버) 는 확인하지 못했다 (시험은 로컬의 http · ws 서버). APK 에서의 네트워크 (INTERNET 권한) 도 미확인.
- 업데이트: 실제 GitHub 릴리스에서 받는 것 · `jbdev.exe` 의 "업데이트하시겠습니까?" 창은 확인하지 못했다 (CI 는 로컬 HTTP 서버로 `jdh update` 만 시험 · 릴리스를 게시한 적이 없음).
