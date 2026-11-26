#!/bin/sh
# Windows (x64) 실행 파일 — llvm-mingw + SDL2 정적 링크 (DLL 없이 한 파일)
#   MINGW=llvm-mingw 폴더 · SDLW=SDL2-devel-mingw 의 x86_64-w64-mingw32 폴더
set -e
cd "$(dirname "$0")/.."
CC="$MINGW/bin/x86_64-w64-mingw32-clang"
"$MINGW/bin/x86_64-w64-mingw32-windres" win/app.rc -O coff -o win/app.res.o
$CC -O2 -static -mwindows -Ivendor -I"$SDLW/include/SDL2" -D_REENTRANT -DSDL_MAIN_HANDLED \
  -Wno-unused-function -Wno-format-truncation -Wno-misleading-indentation \
  -o jawbreaker-win.exe src/*.c win/app.res.o \
  "$SDLW/lib/libSDL2.a" -lm -lpthread -lcomdlg32 -lsetupapi -lcfgmgr32 -lwinmm -limm32 -lversion -lole32 -loleaut32 -lgdi32 -luuid -lshell32 -luser32 -ladvapi32 -lwinhttp
"$MINGW/bin/llvm-strip" jawbreaker-win.exe   # 편집기에 넣을 때 작게
ls -la jawbreaker-win.exe
# 편집기 (JawBreaker.html) 의 <script id="jbrt-win"> 에 넣는 값: gzip -9 -c jawbreaker-win.exe | base64 -w0
