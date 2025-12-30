CC := gcc
CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
LIBS := $(shell pkg-config --libs gtk+-3.0)

SRCS := src/state.c src/layers.c src/undo.c src/palette.c src/canvas.c \
        src/fileio.c src/commands.c src/render.c src/input.c src/main.c

all: build/vimpaint

build/vimpaint: $(SRCS) src/vimpaint.h
	mkdir -p build/
	${CC} ${CFLAGS} $(SRCS) -o $@ ${LIBS} -lm

clean:
	rm -rf build/

.PHONY: clean
