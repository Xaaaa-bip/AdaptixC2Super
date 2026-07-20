CC64 = x86_64-w64-mingw32-gcc
CC86 = i686-w64-mingw32-gcc
STRIP64 = x86_64-w64-mingw32-strip --strip-unneeded
STRIP86 = i686-w64-mingw32-strip --strip-unneeded
CFLAGS = -I ../_include -w -Wno-int-conversion -Wno-incompatible-pointer-types -Os -DBOF -c

all: bof

bof: clean
	@(mkdir _bin 2>/dev/null) && echo 'creating _bin directory' || echo '_bin directory exists'
	# 64-bit builds
	@($(CC64) $(CFLAGS) inject_sec/inject_sec.c -o _bin/inject_sec.x64.o && $(STRIP64) _bin/inject_sec.x64.o) && echo '[+] inject_sec' || echo '[!] inject_sec'
	@($(CC64) $(CFLAGS) inject_poolparty/inject_poolparty.c -o _bin/inject_poolparty.x64.o && $(STRIP64) _bin/inject_poolparty.x64.o) && echo '[+] inject_poolparty' || echo '[!] inject_poolparty'
	@($(CC64) -I ../_include -masm=intel -Wall -Os -c inject_cfg/inject_cfg.c -o _bin/inject_cfg.x64.o && $(STRIP64) _bin/inject_cfg.x64.o) && echo '[+] inject_cfg' || echo '[!] inject_cfg'

	# 32-bit builds
	@($(CC86) $(CFLAGS) inject_sec/inject_sec.c         -o _bin/inject_sec.x32.o        && $(STRIP86) _bin/inject_sec.x32.o)        && echo '[+] inject_sec (x32)'        || echo '[!] inject_sec (x32)'
	@#($(CC86) $(CFLAGS) inject_poolparty/inject_poolparty.c -o _bin/inject_poolparty.x32.o && $(STRIP86) _bin/inject_poolparty.x32.o) && echo '[+] inject_poolparty (x32)' || echo '[!] inject_poolparty (x32)'
	@($(CC86) -I ../_include -masm=intel -Wall -Os -c inject_cfg/inject_cfg.c -o _bin/inject_cfg.x32.o && $(STRIP86) _bin/inject_cfg.x32.o) && echo '[+] inject_cfg (x32)' || echo '[!] inject_cfg (x32)'

	@($(CC86) $(CFLAGS) inject_32to64/inject_32to64.c -o _bin/inject_32to64.x86.o && $(STRIP64) _bin/inject_32to64.x86.o) && echo '[+] inject_32to64' || echo '[!] inject_32to64'

clean:
	@(rm -rf _bin)
