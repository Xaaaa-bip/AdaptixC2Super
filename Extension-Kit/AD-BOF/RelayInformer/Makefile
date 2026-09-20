CC64 = x86_64-w64-mingw32-gcc
STRIP64 = x86_64-w64-mingw32-strip --strip-unneeded
CFLAGS = -I common -w -Wno-incompatible-pointer-types -Os -s -DBOF -c

all: bof

bof:
	@(mkdir -p _bin/RelayInformer 2>/dev/null) && echo 'creating _bin/RelayInformer' || echo '_bin/RelayInformer exists'
	@($(CC64) $(CFLAGS) -lws2_32 relay-informer-smb/entry.c -o _bin/RelayInformer/smb.x64.o   && $(STRIP64) _bin/RelayInformer/smb.x64.o) && echo '[+] relay-informer-smb' || echo '[!] relay-informer-smb'
	@($(CC64) $(CFLAGS) -lodbc32 -lws2_32 relay-informer-mssql/entry.c -o _bin/RelayInformer/mssql.x64.o   && $(STRIP64) _bin/RelayInformer/mssql.x64.o) && echo '[+] relay-informer-mssql' || echo '[!] relay-informer-mssql'
	@($(CC64) $(CFLAGS) -lsecur32 -lwinhttp -lcrypt32 -lbcrypt relay-informer-http/entry.c -o _bin/RelayInformer/http.x64.o   && $(STRIP64) _bin/RelayInformer/http.x64.o) && echo '[+] relay-informer-http' || echo '[!] relay-informer-http'
	@($(CC64) $(CFLAGS) -lwldap32 -ldnsapi relay-informer-ldap/entry.c -o _bin/RelayInformer/ldap.x64.o   && $(STRIP64) _bin/RelayInformer/ldap.x64.o) && echo '[+] relay-informer-ldap' || echo '[!] relay-informer-ldap'

clean:
	@(rm -rf _bin)