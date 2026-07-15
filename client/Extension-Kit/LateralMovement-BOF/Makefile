CC64 = x86_64-w64-mingw32-gcc
CC86 = i686-w64-mingw32-gcc
CPP64 = x86_64-w64-mingw32-g++
CPP86 = i686-w64-mingw32-g++
STRIP64 = x86_64-w64-mingw32-strip --strip-unneeded
STRIP86 = i686-w64-mingw32-strip --strip-unneeded
CFLAGS = -I ../_include -w -Wno-int-conversion -Wno-incompatible-pointer-types -Os -DBOF -s -c

all: bof

bof: clean
	@(mkdir _bin 2>/dev/null) && echo 'creating _bin directory' || echo '_bin directory exists'
	# 64-bit builds
	@($(CC64) $(CFLAGS) psexec/psexec.c -o _bin/psexec.x64.o && $(STRIP64) _bin/psexec.x64.o) && echo '[+] psexec' || echo '[!] psexec'
	@($(CC64) $(CFLAGS) scshell/scshell.c -o _bin/scshell.x64.o && $(STRIP64) _bin/scshell.x64.o) && echo '[+] scshell' || echo '[!] scshell'
	@($(CC64) $(CFLAGS) token_make/token_make.c -o _bin/token_make.x64.o && $(STRIP64) _bin/token_make.x64.o) && echo '[+] token_make' || echo '[!] token_make'
	@($(CC64) $(CFLAGS) token_steal/token_steal.c -o _bin/token_steal.x64.o && $(STRIP64) _bin/token_steal.x64.o) && echo '[+] token_steal' || echo '[!] token_steal'
	@($(CC64) $(CFLAGS) runas/runas.c -o _bin/runas.x64.o && $(STRIP64) _bin/runas.x64.o) && echo '[+] runas' || echo '[!] runas'
	@($(CC64) $(CFLAGS) runas_sess_IHxExec/ihxexec.c -o _bin/runas_sess_ihxexec.x64.o && $(STRIP64) _bin/runas_sess_ihxexec.x64.o) && echo '[+] runas_sess_ihxexec' || echo '[!] runas_sess_ihxexec'
	@($(CPP64) -I ../_include -w -Os -s -c -masm=intel -nostdlib -Wno-nonnull -fno-asynchronous-unwind-tables -fno-exceptions -fno-rtti winrm-client/winrm.cpp -o _bin/winrm.x64.o && $(STRIP64) _bin/winrm.x64.o) && echo '[+] winrm' || echo '[!] winrm'

	# 32-bit builds
	@($(CC86) $(CFLAGS) psexec/psexec.c            -o _bin/psexec.x32.o      && $(STRIP86) _bin/psexec.x32.o)      && echo '[+] psexec (x32)'      || echo '[!] psexec (x32)'
	@($(CC86) $(CFLAGS) scshell/scshell.c          -o _bin/scshell.x32.o     && $(STRIP86) _bin/scshell.x32.o)     && echo '[+] scshell (x32)'     || echo '[!] scshell (x32)'
	@($(CC86) $(CFLAGS) token_make/token_make.c    -o _bin/token_make.x32.o  && $(STRIP86) _bin/token_make.x32.o)  && echo '[+] token_make (x32)'  || echo '[!] token_make (x32)'
	@($(CC86) $(CFLAGS) token_steal/token_steal.c  -o _bin/token_steal.x32.o && $(STRIP86) _bin/token_steal.x32.o) && echo '[+] token_steal (x32)' || echo '[!] token_steal (x32)'
	@($(CC86) $(CFLAGS) runas/runas.c              -o _bin/runas.x32.o       && $(STRIP86) _bin/runas.x32.o)       && echo '[+] runas (x32)'       || echo '[!] runas (x32)'
	@#($(CC64) $(CFLAGS) runas_sess_IHxExec/ihxexec.c -o _bin/runas_sess_ihxexec.x64.o && $(STRIP64) _bin/runas_sess_ihxexec.x64.o) && echo '[+] runas_sess_ihxexec' || echo '[!] runas_sess_ihxexec'
	@#($(CPP86) -I ../_include -w -Os -s -c -masm=intel -nostdlib -Wno-nonnull -fno-asynchronous-unwind-tables -fno-exceptions -fno-rtti winrm-client/winrm.cpp -o _bin/winrm.x32.o && $(STRIP86) _bin/winrm.x32.o) && echo '[+] winrm (x32)' || echo '[!] winrm (x32)'

clean:
	@(rm -rf _bin)
