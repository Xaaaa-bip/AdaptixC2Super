# pillager_bridge/src_bof — 需要 mingw-w64；产出 COFF 供 execute bof / 服务端 bofs 下发

CC_X64  ?= x86_64-w64-mingw32-gcc
CC_X86  ?= i686-w64-mingw32-gcc
CFLAGS  := -DBOF -c -Os -fno-asynchronous-unwind-tables -fno-stack-protector -mno-stack-arg-probe \
	-Wno-unused-parameter -Wdiscarded-qualifiers -Wincompatible-pointer-types

all: pillager_mini.x64.o pillager_mini.x86.o

pillager_mini.x64.o: pillager_mini.c beacon.h adaptix.h
	$(CC_X64) $(CFLAGS) pillager_mini.c -o $@
	x86_64-w64-mingw32-strip --strip-unneeded $@ 2>/dev/null || true

pillager_mini.x86.o: pillager_mini.c beacon.h adaptix.h
	-$(CC_X86) $(CFLAGS) pillager_mini.c -o $@
	-i686-w64-mingw32-strip --strip-unneeded $@ 2>/dev/null || true

clean:
	rm -f pillager_mini.x64.o pillager_mini.x86.o

.PHONY: all clean
