@echo off
chcp 65001 >nul
rem JDH 빌드 (Windows) — llvm-mingw 가 필요합니다.
rem   MINGW = llvm-mingw 를 푼 폴더 (안에 bin\x86_64-w64-mingw32-clang.exe 가 있는 곳)
rem   예:  set MINGW=C:\llvm-mingw   그리고  build-win.bat
setlocal
cd /d "%~dp0"
if "%MINGW%"=="" if exist "%~dp0llvm-mingw\bin\x86_64-w64-mingw32-clang.exe" set MINGW=%~dp0llvm-mingw
if "%MINGW%"=="" ( echo MINGW 가 정해지지 않았습니다. set MINGW=llvm-mingw 폴더  를 먼저 실행하세요. & exit /b 1 )
set CC="%MINGW%\bin\x86_64-w64-mingw32-clang.exe"
set RC="%MINGW%\bin\x86_64-w64-mingw32-windres.exe"
if not exist %CC% ( echo 컴파일러가 없습니다: %CC% & exit /b 1 )
if not exist dist\bin mkdir dist\bin
if not exist dist\editor mkdir dist\editor
if not exist build mkdir build
%RC% win\app.rc -O coff -o build\app.res.o || exit /b 1
%CC% -O2 -static -Wall -o dist\bin\jdh.exe src\jdh.c build\app.res.o -lshell32 -lwinhttp || exit /b 1
%CC% -O2 -static -Wall -mwindows -o dist\bin\jbdev.exe src\jbdev.c build\app.res.o -lcomctl32 -lcomdlg32 -lshell32 -lole32 -lgdi32 -luser32 -ladvapi32 || exit /b 1
copy /y editor\jbdev.html dist\editor\jbdev.html >nul || exit /b 1
xcopy /e /i /q /y editor\native\docs dist\editor\native\docs >nul || exit /b 1
copy /y editor\native\ac-*.txt dist\editor\native\ >nul || exit /b 1
if not exist dist\sdk\html mkdir dist\sdk\html
copy /y editor\native\game-*.html dist\sdk\html\ >nul || exit /b 1
if not exist dist\sdk mkdir dist\sdk
copy /y sdk\jdh.h dist\sdk\jdh.h >nul || exit /b 1
xcopy /e /i /q /y runtime\src dist\sdk\runtime\src >nul || exit /b 1
xcopy /e /i /q /y runtime\vendor dist\sdk\runtime\vendor >nul || exit /b 1
if not exist dist\sdk\runtime\win mkdir dist\sdk\runtime\win
copy /y runtime\win\app.manifest dist\sdk\runtime\win\app.manifest >nul || exit /b 1
copy /y runtime\win\icon.ico dist\sdk\runtime\win\icon.ico >nul || exit /b 1
echo.
echo 빌드 끝: dist\bin\jdh.exe · dist\bin\jbdev.exe · dist\editor\jbdev.html
echo 설치 파일: Inno Setup 으로 installer\jdh.iss 를 컴파일하면 installer\out\JDH-Setup-0.8.0.exe
