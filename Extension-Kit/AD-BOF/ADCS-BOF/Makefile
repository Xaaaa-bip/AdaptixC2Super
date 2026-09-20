CC64 = x86_64-w64-mingw32-gcc
STRIP64 = x86_64-w64-mingw32-strip --strip-unneeded
CFLAGS = -I ../../_include -w -Wno-incompatible-pointer-types -Os -s -DBOF -c

all: bof

bof:
	@(mkdir -p _bin/ADCS 2>/dev/null) && echo 'creating _bin/ADCS' || echo '_bin/ADCS exists'
	@($(CC64) $(CFLAGS) src/enum/entry.c -o _bin/ADCS/certi_enum.x64.o   && $(STRIP64) _bin/ADCS/certi_enum.x64.o) && echo '[+] enum' || echo '[!] enum'
	@($(CC64) $(CFLAGS) src/request/entry.c -o _bin/ADCS/certi_req.x64.o && $(STRIP64) _bin/ADCS/certi_req.x64.o)  && echo '[+] certi_req' || echo '[!] certi_req'
	@($(CC64) $(CFLAGS) src/request_on_behalf/entry.c -o _bin/ADCS/certi_req_onbehalf.x64.o && $(STRIP64) _bin/ADCS/certi_req_onbehalf.x64.o) && echo '[+] certi_req_onbehalf' || echo '[!] certi_req_onbehalf'
	@($(CC64) $(CFLAGS) src/auth/certi_auth.c -o _bin/ADCS/certi_auth.x64.o && $(STRIP64) _bin/ADCS/certi_auth.x64.o) && echo '[+] certi_auth' || echo '[!] certi_auth'
	@($(CC64) $(CFLAGS) src/shadow/certi_shadow.c -o _bin/ADCS/certi_shadow.x64.o && $(STRIP64) _bin/ADCS/certi_shadow.x64.o) && echo '[+] certi_shadow' || echo '[!] certi_shadow'

clean:
	@(rm -rf _bin)