CC64 = x86_64-w64-mingw32-gcc
CC86 = i686-w64-mingw32-gcc
CPP64 = x86_64-w64-mingw32-g++
CPP86 = i686-w64-mingw32-g++
STRIP64 = x86_64-w64-mingw32-strip --strip-unneeded
STRIP86 = i686-w64-mingw32-strip --strip-unneeded
CFLAGS = -I ../_include -w -Wno-int-conversion -Wno-incompatible-pointer-types -Os -s -DBOF -c

all: bof

bof: clean
	@(mkdir _bin 2>/dev/null) && echo 'creating _bin' || echo '_bin exists'
	# 64-bit builds
	@($(CC64) $(CFLAGS) getsystem_token/getsystem_token.c -o _bin/getsystem_token.x64.o && $(STRIP64) _bin/getsystem_token.x64.o) && echo '[+] getsystem_token' || echo '[!] getsystem_token'
	@($(CC64) $(CFLAGS) uac_regshellcmd/RegistryShellCommandBOF.c -o _bin/uac_regshellcmd.x64.o && $(STRIP64) _bin/uac_regshellcmd.x64.o) && echo '[+] uac_regshellcmd' || echo '[!] uac_regshellcmd'
	@($(CC64) $(CFLAGS) printspoofer/printspoofer.c -o _bin/printspoofer.x64.o && $(STRIP64) _bin/printspoofer.x64.o) && echo '[+] printspoofer' || echo '[!] printspoofer'
	@($(CPP64) -I ../_include -w -Os -s -c -o _bin/uac_sspi.x64.o uac_sspi/SspiUacBypassBOF.cpp && $(STRIP64) _bin/uac_sspi.x64.o) && echo '[+] uac_sspi' || echo '[!] uac_sspi'
	@($(CPP64) -I ../_include -w -Os -s -c -o _bin/DCOMPotato.x64.o potato-dcom/DCOMPotato.cpp -masm=intel -nostdlib -Wno-nonnull -fno-asynchronous-unwind-tables -fno-exceptions -fno-rtti && $(STRIP64) _bin/DCOMPotato.x64.o) && echo '[+] DCOMPotato' || echo '[!] DCOMPotato'

	# 32-bit builds
	@($(CC86) $(CFLAGS) getsystem_token/getsystem_token.c -o _bin/getsystem_token.x32.o && $(STRIP86) _bin/getsystem_token.x32.o) && echo '[+] getsystem_token (x32)' || echo '[!] getsystem_token (x32)'
	@($(CC86) $(CFLAGS) uac_regshellcmd/RegistryShellCommandBOF.c -o _bin/uac_regshellcmd.x32.o && $(STRIP86) _bin/uac_regshellcmd.x32.o) && echo '[+] uac_regshellcmd (x32)' || echo '[!] uac_regshellcmd (x32)'
	@($(CC86) $(CFLAGS) printspoofer/printspoofer.c -o _bin/printspoofer.x32.o && $(STRIP86) _bin/printspoofer.x32.o) && echo '[+] printspoofer (x32)' || echo '[!] printspoofer (x32)'
	@($(CPP86) -I ../_include -w -Os -s -c -o _bin/uac_sspi.x32.o uac_sspi/SspiUacBypassBOF.cpp && $(STRIP86) _bin/uac_sspi.x32.o) && echo '[+] uac_sspi (x32)' || echo '[!] uac_sspi (x32)'
	@($(CPP86) -I ../_include -w -Os -s -c -o _bin/DCOMPotato.x32.o potato-dcom/DCOMPotato.cpp -masm=intel -nostdlib -Wno-nonnull -fno-asynchronous-unwind-tables -fno-exceptions -fno-rtti && $(STRIP86) _bin/DCOMPotato.x32.o) && echo '[+] DCOMPotato (x32)' || echo '[!] DCOMPotato (x32)'



clean:
	@(rm -rf _bin)