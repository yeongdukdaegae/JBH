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
- 네트워크: 웹에서는 쓸 수 없다 (라이브러리가 부를 수 있는 host 함수가 `log` · `error` 뿐이고 네트워크용은 만들지 않음). 따로 막는 장치를 두지는 않는다. EXE 에서는 C 코드가 격리되지 않으므로 소켓을 직접 쓸 수 있다. 네트워크 host 함수는 요청이 있을 때 추가한다.
- `jdh_log` 는 `print` 와 같은 디버그 출력이다. 에디터에서는 로그 패널에, EXE 에서는 표준 출력에 나온다 (창 프로그램이라 화면에는 안 보임).
- 엔진 버전은 1.3 (에디터의 `JB_VER` · `JBX_ENGINE` · `src/jdh.c` 의 `JDH_ENGINE_*` 를 같이 올린다). 라이브러리 선언의 `requires.engine` (">=1.2" 꼴) 을 `jdh lib check` 와 에디터가 검사한다.
- 신뢰 확인 (Dev.): `.jbx` 내용의 SHA-256 마다 한 번 묻고 `localStorage` 의 `jawbreaker.jbx.trust` 에 기억한다 (다시 구우면 다시 물음). 창에는 버전 · 명령어 · 값 개수 · 쓰는 권한 · "EXE 에서는 격리 없음" 을 보여 주고, files.read 가 있으면 허락 여부도 고른다. 묻는 때: 「라이브러리 불러오기 (.jbx)」 메뉴 · 파일로 넣을 때 (불러오기 · 끌어 놓기) · 프로젝트 zip 등으로 들어온 것은 처음 실행할 때. 신뢰하지 않은 라이브러리를 쓰는 프로그램은 실행하지 않는다. 코드는 에디터 쪽 DEV 구간 (`jbxTrustRun` · `jbxImport` · `jbxDocText` · `jbxAcMembers`) — 내보낸 게임에는 없다.
- `ext/*.jbx` 를 열면 설명서 (선언 · 권한 · `doc/` 파일) 를 읽기 전용으로 보여 준다. 자동완성은 `Use ` · `별칭.` · 문자열 안 `%별칭.값%` 에서 라이브러리의 명령어 · 값을 낸다.
- 엔진 기능 (host 함수) 은 `sdk/jdh.h` 에 선언하고 웹 (`JBX_BOOT` 의 `HOST` · `JBEXT`) 과 EXE (`runtime/src/gfx.c` · `render.c` · `run.c` 의 `jdh_*`) 에 같은 뜻으로 구현한다. 함수 → 권한 표는 `src/jdh.c` 의 `HOSTFN` 과 에디터의 `JBX_HOST` 를 같게 둔다.
  - `draw`: 라이브러리마다 그리기 층 하나 (사각형 · 타원 · 선 · 글자) · 지울 때까지 남음 · 오브젝트 위 · UI 아래 · 화면 단위 (카메라를 따름).
  - `objects.read`: 위치 · 크기 · 속도 · 색 · 글자. `objects.write`: 같은 것을 바꿈 (없는 오브젝트는 오류). 만들기 · 지우기는 없다.
  - 권한은 컴파일된 `lib.wasm` 이 가져다 쓰는 host 함수로 확인한다: `jdh lib check` · `jdh build exe` · 에디터 (불러올 때) 세 곳 모두 거부한다.
  - 웹에서는 엔진이 바뀐 오브젝트를 명령 묶음과 화면 프레임마다 워커로 보내고, 쓰기 · 그리기는 명령 묶음 · 프레임이 끝날 때 엔진으로 온다 (한 박자 늦을 수 있음 — 값과 같은 규칙). EXE 는 바로.
  - 시험: `tests/libs/Probe` (모든 기능) · `tests/libs/NoPerm` (권한 없이 씀 → 거부) · `tests/fixtures/probe-main.jb` 를 웹 (`tests/dev.mjs`) 과 EXE (CI) 가 같이 돌려 같은 줄이 나오는지 본다.
- `jdh build exe` 결과물: 디버그 정보를 뺀다 (`-s` · `.pdb` 도 안 생김). 아이콘과 고해상도 화면 설정 (`runtime/win/app.manifest` · PerMonitorV2) 을 리소스로 넣는다. 아이콘은 프로젝트 폴더의 `icon.ico` 가 있으면 그것, 없으면 `runtime/win/icon.ico`. 설치본에는 `sdk/runtime/win/` 으로 들어간다.
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
