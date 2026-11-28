# Jawbreaker C 판 — SDL2 만 있으면 됨 (이미지 · 글꼴 · 소리 디코더는 vendor/ 의 한 파일 라이브러리)
CC ?= gcc
CFLAGS ?= -O2 -g
WARN = -Wall -Wno-unused-function -Wno-format-truncation -Wno-misleading-indentation -Wno-unused-but-set-variable
SRC = $(wildcard src/*.c)

ifeq ($(OS),Windows_NT)
  SDL_CFLAGS ?= $(shell sdl2-config --cflags 2>/dev/null)
  SDL_LIBS ?= -lmingw32 -lSDL2 -lcomdlg32 -lwinhttp -lws2_32 -lsecur32 -lcrypt32 -lncrypt -mconsole
  EXE = jawbreaker.exe
else
  SDL_CFLAGS ?= $(shell sdl2-config --cflags 2>/dev/null || pkg-config --cflags sdl2)
  SDL_LIBS ?= $(shell sdl2-config --libs 2>/dev/null || pkg-config --libs sdl2 2>/dev/null || echo -lSDL2)
  EXE = jawbreaker
endif

$(EXE): $(SRC) src/jb.h src/rt.h
	$(CC) $(CFLAGS) $(WARN) -Ivendor $(SDL_CFLAGS) -pthread -o $@ $(SRC) $(SDL_LIBS) -lm

check: $(EXE)
	./$(EXE) --check examples/hello
	./$(EXE) --check examples/display

test: $(EXE)
	./$(EXE) examples/hello
	./$(EXE) examples/tasks

clean:
	rm -f jawbreaker jawbreaker.exe
	rm -rf examples/hello/out
