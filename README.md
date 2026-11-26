# JDH (JawBreaker Development Helper) 0.4.0 — 소스

| 경로 | 내용 |
|---|---|
| `src/jdh.c` | `jdh` 명령 (라이브러리 틀 · 검사 · 굽기 · 적용) |
| `sdk/jdh.h` | 라이브러리가 포함하는 헤더 (C 로 한 번만 작성) |
| `src/jbdev.c` | JawBreaker Dev. 를 전용 창으로 여는 프로그램 |
| `editor/src/jb.html` · `editor/build.py` | 에디터 소스 (Lite · Dev. 공통) 와 두 벌을 만드는 스크립트 |
| `editor/jbdev.html` | JawBreaker Dev. 에디터 (빌드 결과 · 창 안에 뜨는 내용) |
| `runtime/` | C 실행기 소스 (`jdh build exe` 가 라이브러리와 함께 컴파일) |
| `tests/` | 에디터 시험과 시험용 라이브러리 |
| `build-win.bat` | Windows 빌드 (llvm-mingw 필요) |
| `installer/jdh.iss` | 설치 프로그램 (Inno Setup 6) |
| `.github/workflows/build.yml` | GitHub 에서 자동 빌드 |

## 빌드 방법 1 — GitHub 에 맡기기 (PC 없이)

1. GitHub 에 새 저장소를 만들고 이 폴더의 내용을 그대로 올립니다.
2. 저장소의 **Actions** 탭 → **JDH build** → **Run workflow**.
3. 끝나면 그 실행 화면 아래 **Artifacts** 에서 `JDH-Setup` 을 받습니다. 안에 `JDH-Setup-0.4.0.exe` 가 있습니다.

## 빌드 방법 2 — Windows PC 에서 직접

1. llvm-mingw 를 받습니다: https://github.com/mstorsjo/llvm-mingw/releases 에서 이름이 `ucrt-x86_64.zip` 으로 끝나는 파일. 풀어서 이 폴더 안에 `llvm-mingw` 라는 이름으로 둡니다.
2. `build-win.bat` 를 실행합니다. `dist\` 에 `jdh.exe` · `jbdev.exe` · `jbdev.html` 이 나옵니다.
3. Inno Setup 6 (https://jrsoftware.org/isdl.php) 을 설치하고 `installer\jdh.iss` 를 열어 Compile 을 누릅니다. `installer\out\JDH-Setup-0.4.0.exe` 가 나옵니다.

## 설치하면

- 시작 메뉴 · 바탕 화면의 **JawBreaker Dev.** 로 에디터가 전용 창으로 열립니다.
- 명령 창에서 `jdh` 를 쓸 수 있습니다 (새 명령 창부터).

```
jdh new lib Hello          라이브러리 틀 만들기
cd Hello
jdh lib check              검사
jdh lib build              Hello-0.1.0.jbx 로 굽기
jdh lib add Hello-0.1.0.jbx <프로젝트 폴더>
```

구운 `.jbx` 는 JawBreaker Dev. 에서 프로젝트를 연 뒤 불러오기로 넣어도 `ext/` 에 들어갑니다.

## 라이브러리는 C 로 한 번만

`native/Hello.c` 하나가 웹(Dev. 실행 · HTML · APK)에서는 WebAssembly 로, EXE 에서는 실행기와 함께 컴파일되어 쓰입니다.
`jdh lib build` 가 웹용으로 컴파일하고, 선언(`jdl.json`)과 명령어 · 값 · 인자 형식이 맞는지 확인한 뒤 `.jbx` 에 담습니다.
컴파일러(zig)는 설치 파일에 들어 있습니다. 직접 빌드할 때는 `dist\toolchain\zig` 에 zig 를 넣거나 환경 변수 `JDH_ZIG` 로 알려 줍니다.
