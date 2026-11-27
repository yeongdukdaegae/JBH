# JDH (JawBreaker Development Helper)

Jawbreaker 는 이 저장소 주인이 만드는 프로그래밍 언어와 게임 엔진이고, JDH 는 그 Windows 개발 환경이다.
답은 한국어 존댓말로, 결론부터 짧게 한다.

## 구성

| 경로 | 내용 |
|---|---|
| `editor/src/jb.html` | 에디터 소스 (Lite 와 Dev. 공통 · 엔진 · 내장 실행기 포함, 약 3MB) |
| `editor/build.py` | 소스 → `editor/out/JawBreaker-Lite.html` · `editor/jbdev.html` (Dev.) |
| `editor/jbdev.html` | Dev. 에디터 빌드 결과 (설치 파일에 들어감 · 직접 고치지 말 것) |
| `src/jdh.c` | `jdh` 명령 (new lib · lib check · lib build · lib add · build exe) |
| `src/jbdev.c` | JawBreaker Dev. 를 Edge 앱 창으로 여는 프로그램 |
| `sdk/jdh.h` | 사용자 라이브러리가 포함하는 헤더 |
| `runtime/` | C 실행기 소스 (SDL2) — EXE 내보내기에 쓰임 |
| `installer/jdh.iss` | 설치 프로그램 (Inno Setup) |
| `tests/` | 에디터 시험 (Playwright) · 시험용 라이브러리 `tests/libs/Counter` |
| `.github/workflows/build.yml` | Windows 빌드와 시험 |

## 핵심 규칙

- 에디터는 두 판이다. **Lite** 는 기존 에디터로 계속 지원하며 사용자 라이브러리가 봉인돼 있다 (코드가 아예 없음). **Dev.** 만 사용자 라이브러리(`.jbx`)를 불러온다.
- 소스 안의 `/*DEV-START*/ … /*DEV-END*/` 는 Dev. 에만, `/*LITE-START*/ … /*LITE-END*/` 는 Lite 에만 들어간다. 사용자 라이브러리 관련 코드는 반드시 DEV 구간 안에 둔다 (`build.py` 가 Lite 에 남았는지 검사한다).
- 사용자 라이브러리는 **C 로 한 번만** 쓴다. 웹 (Dev. 실행 · HTML · APK) 에서는 `jdh lib build` 가 WebAssembly 로 컴파일한 것을 격리된 워커에서 돌리고, EXE 에서는 `jdh build exe` 가 같은 C 소스를 실행기와 함께 컴파일한다.
- 컴파일러는 zig (`zig cc`). 웹은 `wasm32-wasi`, EXE 는 `x86_64-windows-gnu`.
- 웹에서 라이브러리는 에디터 · 저장 공간에 닿지 못한다. 프로젝트 파일은 선언에 `files.read` 권한이 있고 사용자가 허락한 경우에만, 그 프로젝트의 파일을 읽기 전용으로 연다 (쓰기는 없음 · `ext/` 는 못 읽음). 허락은 신뢰 확인 창에서 고른다. 내보낸 HTML · APK 는 에디터에서 허락한 것을 그대로 가진다. EXE 는 격리가 없으므로 선언만 있으면 읽는다.
- 네트워크 (`network` 권한 · 엔진 1.5 부터): HTTP 요청 (`jdh_http_*`) 과 WebSocket (`jdh_ws_*`). 모두 기다리지 않는다 — 걸어 두고 다음 명령 · `JDH_FRAME` 에서 상태를 물어본다.
  - 웹에서는 선언에 `network` 가 있고 사용자가 신뢰 확인 창에서 허락한 경우에만 접속한다 (허락하지 않으면 시작하지 못함 = 0). 허락하면 어느 서버든 접속할 수 있다 — 창에 "읽은 것을 밖으로 보낼 수 있음" 을 알린다. 허락은 `jawbreaker.jbx.trust` 의 `net` 에 기억하고, 내보낸 HTML · APK 는 에디터에서 허락한 것을 그대로 가진다 (`PROJECT.extNet` · APK 는 INTERNET 권한을 넣음). EXE 는 격리가 없으므로 선언만 있으면 된다.
  - 웹은 워커 안에서 `fetch` · `WebSocket` 을 쓴다 (`JBX_BOOT` 의 `HOST`). 그래서 브라우저 규칙을 따른다: HTTP 는 서버가 CORS 를 허용해야 응답을 읽고, https 페이지에서는 http · ws 에 접속하지 못한다. EXE (`runtime/src/net.c` · WinHTTP · 스레드) 에는 이 제한이 없다 — 차이로 남아 있음.
  - 한도: 동시에 64개 · 응답 본문과 WebSocket 에 쌓인 메시지는 16MB.
  - 시험: `tests/libs/Net` · `tests/fixtures/net-main.jb` · `tests/net_server.py` (HTTP + WebSocket) 를 웹 (`tests/dev.mjs`) 과 EXE (CI) 가 같이 돌린다. Windows 가 아닌 빌드의 `net.c` 는 소켓으로 http · ws 만 한다 (시험용).
- 권한 (언어의 내장 기능 · Lite 와 Dev. 모두): `jaw.getPermit("cam" | "mic" | "file");` 은 허락을 받을 때까지 기다리고, 결과는 `jaw.hasPermit("…")` (bit) 로 읽는다. 쓰는 기능: `mag.wearCam("id");` (카메라 영상을 오브젝트의 텍스쳐로 · 벗기기는 `mag.wearTexture("id", "")`) · `jaw.micLevel()` (소리 크기 RMS 0 ~ 1) · `jaw.permitText()` (고른 파일의 글자) · `jaw.writePermit("글자");` (고른 파일을 덮어씀).
  - 웹: `getUserMedia` (카메라 · 마이크) · `showOpenFilePicker` (파일 · Edge · Chrome 만 · 사용자가 막 누른 직후에만 열리므로 실패하면 화면에 "파일 고르기" 단추를 띄움). 코드는 `execProgram` 안 `PERMIT`. 실행이 끝나면 카메라 · 마이크를 끈다 (`PERMIT_LIVE`).
  - EXE: `runtime/src/permit.c` — 카메라는 Windows Media Foundation (첫 카메라 · 그 밖의 OS 는 0) · 마이크는 SDL2 녹음 장치 · 파일은 열기 창 (환경 변수 `JB_PERMIT_FILE` 이 있으면 그 파일 · 시험용). Windows 개인 정보 설정에서 막혀 있으면 0.
  - APK: 아직 안 됨 (안드로이드 쪽 앱 틀에 권한 요청이 없음 → 늘 0). C 변환은 이 기능들을 거부한다.
  - 시험: `tests/fixtures/permit-main.jb` 를 웹 (`tests/dev.mjs` · 가짜 카메라 · 마이크 · 파일 고르기 흉내) 과 EXE (CI · 파일만 실제로 확인) 가 같이 돌린다.
- `jdh_log` 는 `print` 와 같은 디버그 출력이다. 에디터에서는 로그 패널에, EXE 에서는 표준 출력에 나온다 (창 프로그램이라 화면에는 안 보임).
- 엔진 버전은 1.5 (에디터의 `JB_VER` · `JBX_ENGINE` · `src/jdh.c` 의 `JDH_ENGINE_*` 를 같이 올린다). 라이브러리 선언의 `requires.engine` (">=1.2" 꼴) 을 `jdh lib check` 와 에디터가 검사한다.
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
3. `jbdev.exe` 설치 후 실제 PC 에서의 동작 확인 (설치 · PATH · 바로 가기 · 창)

## 아직 확인하지 못한 것

- 설치 파일을 실제 PC 에서 실행한 적이 없다.
- 격리 방식 (권한을 뺀 프레임 안의 워커) 은 헤드리스 Chrome 에서만 확인했다. Edge 앱 창 · Android WebView 는 미확인.
- 번역 (ja · zh-CN · zh-TW) 은 원어민 검수를 받지 않았다.
- 권한: 실제 카메라 · 마이크 (웹은 가짜 장치, CI 에는 장치가 없음) · EXE 의 카메라 영상 (Media Foundation 코드는 컴파일까지만) · 실제 파일 고르기 창은 확인하지 못했다. 에디터의 "EXE 로 내보내기" 에 든 미리 만든 실행기 (`jbrt-win`) 는 다시 만들지 않아 이 기능이 없다 (`jdh build exe` 는 됨).
- 네트워크: https · wss (실제 인터넷의 서버) 는 확인하지 못했다 (시험은 로컬의 http · ws 서버). APK 에서의 네트워크 (INTERNET 권한) 도 미확인.
- 업데이트: 실제 GitHub 릴리스에서 받는 것 · `jbdev.exe` 의 "업데이트하시겠습니까?" 창은 확인하지 못했다 (CI 는 로컬 HTTP 서버로 `jdh update` 만 시험 · 릴리스를 게시한 적이 없음).
