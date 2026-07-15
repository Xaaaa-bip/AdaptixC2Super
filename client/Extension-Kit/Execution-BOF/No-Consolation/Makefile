BOFNAME := NoConsolation
CC_x64 := x86_64-w64-mingw32-gcc
CC_x86 := i686-w64-mingw32-gcc
STRIP_x64 := x86_64-w64-mingw32-strip
STRIP_x86 := i686-w64-mingw32-strip
OPTIONS := -masm=intel -Wall -I include

release:
	@(mkdir _bin 2>/dev/null) && echo 'creating _bin' || echo '_bin exists'
	@($(CC_x64) -c source/entry.c -o _bin/$(BOFNAME).x64.o $(OPTIONS) && $(STRIP_x64) --strip-unneeded _bin/$(BOFNAME).x64.o) && echo '[+] NoConsolation x64' || echo '[!] NoConsolation x64'
	@($(CC_x86) -c source/entry.c -o _bin/$(BOFNAME).x86.o $(OPTIONS) && $(STRIP_x86) --strip-unneeded _bin/$(BOFNAME).x86.o) && echo '[+] NoConsolation x86' || echo '[!] NoConsolation x86'

clean:
	@(rm -rf _bin)