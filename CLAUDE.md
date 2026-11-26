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
- 웹에서 라이브러리는 에디터 · 저장 공간에 닿지 못한다. 프로젝트 파일은 선언에 `files.read` 권한이 있고 사용자가 불러올 때 허락한 경우에만, 그 프로젝트의 파일을 읽기 전용으로 연다 (쓰기는 없음 · 아직 구현 전 — 남은 일 1).
- 네트워크: 웹에서는 쓸 수 없다 (라이브러리가 부를 수 있는 host 함수가 `log` · `error` 뿐이고 네트워크용은 만들지 않음). 따로 막는 장치를 두지는 않는다. EXE 에서는 C 코드가 격리되지 않으므로 소켓을 직접 쓸 수 있다. 네트워크 host 함수는 요청이 있을 때 추가한다.
- `jdh_log` 는 `print` 와 같은 디버그 출력이다. 에디터에서는 로그 패널에, EXE 에서는 표준 출력에 나온다 (창 프로그램이라 화면에는 안 보임).
- 엔진 버전은 1.2 (에디터의 `JB_VER` · `JBX_ENGINE` · `src/jdh.c` 의 `JDH_ENGINE_*` 를 같이 올린다). 라이브러리 선언의 `requires.engine` (">=1.2" 꼴) 을 `jdh lib check` 와 에디터가 검사한다.
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

1. 라이브러리의 그리기와 오브젝트 접근 — host 함수를 웹과 EXE 양쪽에 같게 (선언의 `permissions`: draw · objects.read · objects.write · files.read)
2. Dev. 에서 불러올 때의 신뢰 확인 창, 자동완성, 설명서 열기, 메뉴의 「라이브러리 불러오기」
3. `jdh build exe` 결과물: 크기 줄이기 (`-s`), 아이콘 · 고해상도 화면 설정 (`runtime/win/app.rc`)
4. 창을 띄우는 게임 (DisPlayer) 을 `jdh build exe` 로 만든 EXE 의 실제 실행 확인
5. APK 에서의 라이브러리 동작 확인
6. `jbdev.exe` 설치 후 실제 PC 에서의 동작 확인 (설치 · PATH · 바로 가기 · 창)

## 아직 확인하지 못한 것

- 설치 파일을 실제 PC 에서 실행한 적이 없다.
- 격리 방식 (권한을 뺀 프레임 안의 워커) 은 헤드리스 Chrome 에서만 확인했다. Edge 앱 창 · Android WebView 는 미확인.
- 번역 (ja · zh-CN · zh-TW) 은 원어민 검수를 받지 않았다.
