CC := gcc
CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
LIBS := $(shell pkg-config --libs gtk+-3.0)

SRCS := src/state.c src/layers.c src/undo.c src/palette.c src/canvas.c \
        src/fileio.c src/commands.c src/render.c src/input.c src/main.c
OBJS := $(SRCS:src/%.c=build/%.o)

all: build/vimpaint

build/vimpaint: $(OBJS)
	${CC} $(OBJS) -o $@ ${LIBS} -lm

build/%.o: src/%.c src/vimpaint.h
	mkdir -p build/
	${CC} ${CFLAGS} -c $< -o $@

clean:
	rm -rf build/

.PHONY: clean
