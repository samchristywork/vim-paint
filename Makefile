CC := gcc

CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
LIBS := $(shell pkg-config --libs gtk+-3.0)

all: build/vimpaint

build/vimpaint: src/vimpaint.c
	mkdir -p build/
	${CC} ${CFLAGS} src/vimpaint.c -o $@ ${LIBS}

clean:
	rm -rf build/
