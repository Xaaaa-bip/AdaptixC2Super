CC = x86_64-w64-mingw32-gcc
LD = x86_64-w64-mingw32-ld
STRIP = x86_64-w64-mingw32-strip

CFLAGS_BASE = -I. -Os -masm=intel -fno-stack-protector -mno-stack-arg-probe -DBOF -Wall -Wno-format -D_M_AMD64 -Wno-unused-variable -Wno-unknown-pragmas -Wno-missing-braces -Wno-builtin-macro-redefined
CFLAGS_RELEASE = $(CFLAGS_BASE) -DNDEBUG
LDFLAGS = -r

CFLAGS = $(CFLAGS_RELEASE)
RPC_STUB = drsuapi/ms-drsr-custom.c

all: both

both: clean single-user all-users

single-user: CFLAGS = $(CFLAGS_RELEASE)
single-user: RPC_STUB = drsuapi/ms-drsr-custom.c
single-user: SRC_FILE = src/dcsync-single.c
single-user: bof-single
	@echo '[+] dcsync single'

all-users: CFLAGS = $(CFLAGS_RELEASE)
all-users: RPC_STUB = drsuapi/ms-drsr-custom.c
all-users: SRC_FILE = src/dcsync-all.c
all-users: bof-all
	@echo '[+] dcsync all'

bof-single:
	@[ -d "_bin" ] || { mkdir -p "_bin"; }
	@$(CC) $(CFLAGS) -c util/rpc-adapter.c -o _bin/rpc-adapter-single.o
	@$(CC) $(CFLAGS) -c $(RPC_STUB) -o _bin/ms-drsr_c-single.o
	@$(CC) $(CFLAGS) -c $(SRC_FILE) -o _bin/dcsync-single-temp.o
	@$(LD) $(LDFLAGS) _bin/rpc-adapter-single.o _bin/ms-drsr_c-single.o _bin/dcsync-single-temp.o -o _bin/dcsync-single.x64.o
	@$(STRIP) --strip-unneeded _bin/dcsync-single.x64.o
	@rm -f _bin/rpc-adapter-single.o _bin/ms-drsr_c-single.o _bin/dcsync-single-temp.o

bof-all:
	@[ -d "_bin" ] || { mkdir -p "_bin"; }
	@$(CC) $(CFLAGS) -c util/rpc-adapter.c -o _bin/rpc-adapter-all.o
	@$(CC) $(CFLAGS) -c $(RPC_STUB) -o _bin/ms-drsr_c-all.o
	@$(CC) $(CFLAGS) -c $(SRC_FILE) -o _bin/dcsync-all-temp.o
	@$(LD) $(LDFLAGS) _bin/rpc-adapter-all.o _bin/ms-drsr_c-all.o _bin/dcsync-all-temp.o -o _bin/dcsync-all.x64.o
	@$(STRIP) --strip-unneeded _bin/dcsync-all.x64.o
	@rm -f _bin/rpc-adapter-all.o _bin/ms-drsr_c-all.o _bin/dcsync-all-temp.o

clean:
	@rm -rf _bin

