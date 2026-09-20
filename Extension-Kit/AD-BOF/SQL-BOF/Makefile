CC64 = x86_64-w64-mingw32-gcc
STRIP64 = x86_64-w64-mingw32-strip --strip-unneeded
CFLAGS = -I ./src/common -w -Wno-incompatible-pointer-types -Os -s -DBOF -c

all: bof

bof:
	@(mkdir -p _bin/SQL 2>/dev/null) && echo 'creating _bin/SQL' || echo '_bin/SQL exists'
	@($(CC64) $(CFLAGS) src/SQL/1434udp/entry.c -o _bin/SQL/1434udp.x64.o       && $(STRIP64) _bin/SQL/1434udp.x64.o) && echo '[+] 1434udp' || echo '[!] 1434udp'
	@($(CC64) $(CFLAGS) src/SQL/adsi/entry.c -o _bin/SQL/adsi.x64.o             && $(STRIP64) _bin/SQL/adsi.x64.o) && echo '[+] adsi' || echo '[!] adsi'
	@($(CC64) $(CFLAGS) src/SQL/agentcmd/entry.c -o _bin/SQL/agentcmd.x64.o     && $(STRIP64) _bin/SQL/agentcmd.x64.o) && echo '[+] agentcmd' || echo '[!] agentcmd'
	@($(CC64) $(CFLAGS) src/SQL/agentstatus/entry.c -o _bin/SQL/agentstatus.x64.o && $(STRIP64) _bin/SQL/agentstatus.x64.o) && echo '[+] agentstatus' || echo '[!] agentstatus'
	@($(CC64) $(CFLAGS) src/SQL/checkrpc/entry.c -o _bin/SQL/checkrpc.x64.o     && $(STRIP64) _bin/SQL/checkrpc.x64.o) && echo '[+] checkrpc' || echo '[!] checkrpc'
	@($(CC64) $(CFLAGS) src/SQL/clr/entry.c -o _bin/SQL/clr.x64.o               && $(STRIP64) _bin/SQL/clr.x64.o) && echo '[+] clr' || echo '[!] clr'
	@($(CC64) $(CFLAGS) src/SQL/columns/entry.c -o _bin/SQL/columns.x64.o       && $(STRIP64) _bin/SQL/columns.x64.o) && echo '[+] columns' || echo '[!] columns'
	@($(CC64) $(CFLAGS) src/SQL/databases/entry.c -o _bin/SQL/databases.x64.o   && $(STRIP64) _bin/SQL/databases.x64.o) && echo '[+] databases' || echo '[!] databases'
	@($(CC64) $(CFLAGS) src/SQL/togglemodule/entry.c -o _bin/SQL/togglemodule.x64.o && $(STRIP64) _bin/SQL/togglemodule.x64.o) && echo '[+] togglemodule' || echo '[!] togglemodule'
	@($(CC64) $(CFLAGS) src/SQL/impersonate/entry.c -o _bin/SQL/impersonate.x64.o && $(STRIP64) _bin/SQL/impersonate.x64.o) && echo '[+] impersonate' || echo '[!] impersonate'
	@($(CC64) $(CFLAGS) src/SQL/info/entry.c -o _bin/SQL/info.x64.o             && $(STRIP64) _bin/SQL/info.x64.o) && echo '[+] info' || echo '[!] info'
	@($(CC64) $(CFLAGS) src/SQL/links/entry.c -o _bin/SQL/links.x64.o           && $(STRIP64) _bin/SQL/links.x64.o) && echo '[+] links' || echo '[!] links'
	@($(CC64) $(CFLAGS) src/SQL/olecmd/entry.c -o _bin/SQL/olecmd.x64.o         && $(STRIP64) _bin/SQL/olecmd.x64.o) && echo '[+] olecmd' || echo '[!] olecmd'
	@($(CC64) $(CFLAGS) src/SQL/query/entry.c -o _bin/SQL/query.x64.o           && $(STRIP64) _bin/SQL/query.x64.o) && echo '[+] query' || echo '[!] query'
	@($(CC64) $(CFLAGS) src/SQL/rows/entry.c -o _bin/SQL/rows.x64.o             && $(STRIP64) _bin/SQL/rows.x64.o) && echo '[+] rows' || echo '[!] rows'
	@($(CC64) $(CFLAGS) src/SQL/search/entry.c -o _bin/SQL/search.x64.o         && $(STRIP64) _bin/SQL/search.x64.o) && echo '[+] search' || echo '[!] search'
	@($(CC64) $(CFLAGS) src/SQL/smb/entry.c -o _bin/SQL/smb.x64.o               && $(STRIP64) _bin/SQL/smb.x64.o) && echo '[+] smb' || echo '[!] smb'
	@($(CC64) $(CFLAGS) src/SQL/tables/entry.c -o _bin/SQL/tables.x64.o         && $(STRIP64) _bin/SQL/tables.x64.o) && echo '[+] tables' || echo '[!] tables'
	@($(CC64) $(CFLAGS) src/SQL/users/entry.c -o _bin/SQL/users.x64.o           && $(STRIP64) _bin/SQL/users.x64.o) && echo '[+] users' || echo '[!] users'
	@($(CC64) $(CFLAGS) src/SQL/whoami/entry.c -o _bin/SQL/whoami.x64.o         && $(STRIP64) _bin/SQL/whoami.x64.o) && echo '[+] whoami' || echo '[!] whoami'
	@($(CC64) $(CFLAGS) src/SQL/xpcmd/entry.c -o _bin/SQL/xpcmd.x64.o           && $(STRIP64) _bin/SQL/xpcmd.x64.o) && echo '[+] xpcmd' || echo '[!] xpcmd'

clean:
	@(rm -rf _bin)