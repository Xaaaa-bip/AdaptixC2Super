# test_x86_peb.cpp 是 32 位 PEB 访问的**独立诊断程序**（自带 main 与用法注释，
# 按它自己的说明用 `clang++ -target i686-w64-mingw32 test_x86_peb.cpp -o test_x86.exe`
# 单独编）。它躺在 beacon/ 下会被这条 wildcard 收进**两种架构**的载荷编译，而里面的
# `movl %%fs:0x30` 在 x64 下是非法指令 ⇒ objects_http/test_x86_peb.x64.o 编译失败，
# 整个 extenders 构建挂掉（CI 实测 2026-09-21）。所以必须是 filter-out，
# 而不是给它加 `#ifdef` 守卫 —— 它本来就不该进载荷。
SOURCES := $(filter-out beacon/config.cpp beacon/test_x86_peb.cpp, $(wildcard beacon/*.cpp))

# Clang cross to MinGW (Kharon-style fingerprint); do not use -masm=intel globally — breaks Win headers + Clang.
CXX_X64 := clang++ -target x86_64-w64-mingw32
CXX_X86 := clang++ -target i686-w64-mingw32

# Do not wrap these in quotes — they become part of $@ and break NASM -o paths.
BEACON_DIR := beacon
FILES_DIR := files
HTTP_DIST_DIR := objects_http
SMB_DIST_DIR := objects_smb
TCP_DIST_DIR := objects_tcp
DNS_DIST_DIR := objects_dns
# WS flavor. The teamserver has always mapped a ws listener to objects_ws and linked
# ConnectorWS (pl_main.go ObjectDir_ws / ConnectorFile="ConnectorWS", plus a ws-only
# "Files += ObjectDir/CryptAES"), and beacon_agent/Makefile already copies objects_ws into
# dist/ -- but this Makefile never built it, so every beacon+ws build died with
# "cc1plus: fatal error: objects_ws/config.cpp: No such file or directory".
WS_DIST_DIR := objects_ws

# Each flavor compiles only its Connector*.cpp (see also rm -f cleanup in x64/x86 targets).
# ConnectorWS.cpp 无 #ifdef 守卫，必须从另外四个 flavor 里排除，否则同一次链接里
# 会出现两份 WS connector（HTTP flavor 曾经同时编 ConnectorHTTP + ConnectorWS）。
HTTP_SOURCES := $(filter-out beacon/ConnectorSMB.cpp beacon/ConnectorTCP.cpp beacon/ConnectorDNS.cpp beacon/ConnectorWS.cpp,$(SOURCES))
SMB_SOURCES := $(filter-out beacon/ConnectorHTTP.cpp beacon/ConnectorTCP.cpp beacon/ConnectorDNS.cpp beacon/ConnectorWS.cpp,$(SOURCES))
TCP_SOURCES := $(filter-out beacon/ConnectorHTTP.cpp beacon/ConnectorSMB.cpp beacon/ConnectorDNS.cpp beacon/ConnectorWS.cpp,$(SOURCES))
DNS_SOURCES := $(filter-out beacon/ConnectorHTTP.cpp beacon/ConnectorSMB.cpp beacon/ConnectorTCP.cpp beacon/ConnectorWS.cpp,$(SOURCES))
# WS keeps ConnectorWS.cpp and drops the other four (MainAgent.cpp: #elif defined(BEACON_WS) -> CreateConnectorWS()).
WS_SOURCES := $(filter-out beacon/ConnectorHTTP.cpp beacon/ConnectorSMB.cpp beacon/ConnectorTCP.cpp beacon/ConnectorDNS.cpp,$(SOURCES))

HTTP_OBJECTS_X64 := $(patsubst beacon/%.cpp, $(HTTP_DIST_DIR)/%.x64.o, $(HTTP_SOURCES))
HTTP_OBJECTS_X86 := $(patsubst beacon/%.cpp, $(HTTP_DIST_DIR)/%.x86.o, $(HTTP_SOURCES))

SMB_OBJECTS_X64 := $(patsubst beacon/%.cpp, $(SMB_DIST_DIR)/%.x64.o, $(SMB_SOURCES))
SMB_OBJECTS_X86 := $(patsubst beacon/%.cpp, $(SMB_DIST_DIR)/%.x86.o, $(SMB_SOURCES))

TCP_OBJECTS_X64 := $(patsubst beacon/%.cpp, $(TCP_DIST_DIR)/%.x64.o, $(TCP_SOURCES))
TCP_OBJECTS_X86 := $(patsubst beacon/%.cpp, $(TCP_DIST_DIR)/%.x86.o, $(TCP_SOURCES))

DNS_OBJECTS_X64 := $(patsubst beacon/%.cpp, $(DNS_DIST_DIR)/%.x64.o, $(DNS_SOURCES))
DNS_OBJECTS_X86 := $(patsubst beacon/%.cpp, $(DNS_DIST_DIR)/%.x86.o, $(DNS_SOURCES))

WS_OBJECTS_X64 := $(patsubst beacon/%.cpp, $(WS_DIST_DIR)/%.x64.o, $(WS_SOURCES))
WS_OBJECTS_X86 := $(patsubst beacon/%.cpp, $(WS_DIST_DIR)/%.x86.o, $(WS_SOURCES))

SECURITY_FLAGS := -fno-stack-protector \
                 -fno-strict-overflow \
                 -fno-delete-null-pointer-checks \
                 -fno-strict-aliasing \
                 -fno-builtin

OPTIMIZATION_FLAGS := -fno-exceptions \
                     -fno-rtti \
                     -fno-unroll-loops \
                     -fno-unwind-tables \
                     -fno-asynchronous-unwind-tables

# Kharon-aligned codegen (no -mno-sse: breaks Clang + mingw-w64 intrin headers).
# Support for Polymorphic Seed from command line
SEED ?= unknown
SEED_FLAG := -DAX_BUILD_SEED=$(SEED)

# Extra per-build flags injected by pl_main.go (e.g. random -falign-functions=N)
EXTRA_FLAGS ?=

COMMON_FLAGS := -I $(BEACON_DIR) \
                -include beacon/DynamicPolymorphism.h \
                -Os \
                -fno-lto \
                -falign-functions=1 \
                -ffunction-sections \
                -fdata-sections \
                -falign-jumps=1 \
                -falign-labels=1 \
                -fms-extensions \
                -Wno-error=gnu-anonymous-struct \
                -Wno-error=nested-anon-types \
                -Wno-error=anonymous-struct \
                -w \
                $(SECURITY_FLAGS) \
                $(OPTIMIZATION_FLAGS) \
                $(SEED_FLAG) \
                $(EXTRA_FLAGS)

NASM := nasm
BEACON_ASM := beacon/asm
DIST_DIRS := $(HTTP_DIST_DIR) $(SMB_DIST_DIR) $(TCP_DIST_DIR) $(DNS_DIST_DIR) $(WS_DIST_DIR)
ENTRY_EXE_X64 := $(patsubst %,%/entry_exe.x64.o,$(DIST_DIRS))
ENTRY_SVC_X64 := $(patsubst %,%/entry_svc.x64.o,$(DIST_DIRS))
ENTRY_EXE_X86 := $(patsubst %,%/entry_exe.x86.o,$(DIST_DIRS))
ENTRY_SVC_X86 := $(patsubst %,%/entry_svc.x86.o,$(DIST_DIRS))

define ENTRY_NASM_TEMPLATE
$(1)/entry_exe.x64.o: $(BEACON_ASM)/entry_exe_x64.asm
	@mkdir -p $$(dir $$@)
	@$(NASM) -f win64 $$< -o $$@
$(1)/entry_exe.x86.o: $(BEACON_ASM)/entry_exe_x86.asm
	@mkdir -p $$(dir $$@)
	@$(NASM) -f win32 $$< -o $$@
$(1)/entry_svc.x64.o: $(BEACON_ASM)/entry_svc_x64.asm
	@mkdir -p $$(dir $$@)
	@$(NASM) -f win64 $$< -o $$@
$(1)/entry_svc.x86.o: $(BEACON_ASM)/entry_svc_x86.asm
	@mkdir -p $$(dir $$@)
	@$(NASM) -f win32 $$< -o $$@
endef

$(eval $(call ENTRY_NASM_TEMPLATE,$(HTTP_DIST_DIR)))
$(eval $(call ENTRY_NASM_TEMPLATE,$(SMB_DIST_DIR)))
$(eval $(call ENTRY_NASM_TEMPLATE,$(TCP_DIST_DIR)))
$(eval $(call ENTRY_NASM_TEMPLATE,$(DNS_DIST_DIR)))
$(eval $(call ENTRY_NASM_TEMPLATE,$(WS_DIST_DIR)))

INDIRECT_SYSCALL_ASM := $(BEACON_ASM)/indirect_syscall_x64.asm
define INDIRECT_SYSCALL_TEMPLATE
$(1)/indirect_syscall.x64.o: $(INDIRECT_SYSCALL_ASM)
	@mkdir -p $$(dir $$@)
	@$(NASM) -f win64 $$< -o $$@
endef
$(eval $(call INDIRECT_SYSCALL_TEMPLATE,$(HTTP_DIST_DIR)))
$(eval $(call INDIRECT_SYSCALL_TEMPLATE,$(SMB_DIST_DIR)))
$(eval $(call INDIRECT_SYSCALL_TEMPLATE,$(TCP_DIST_DIR)))
$(eval $(call INDIRECT_SYSCALL_TEMPLATE,$(DNS_DIST_DIR)))
$(eval $(call INDIRECT_SYSCALL_TEMPLATE,$(WS_DIST_DIR)))

INDIRECT_SYSCALL_X64_OBJECTS := $(patsubst %,%/indirect_syscall.x64.o,$(DIST_DIRS))

# Miniz trim: disable stdio/archive/time/assert to eliminate CRT dependencies.
# Keeps zlib compress/uncompress for DnsCodec.
MINIZ_TRIM_FLAGS := -DMINIZ_NO_STDIO -DMINIZ_NO_ARCHIVE_APIS -DMINIZ_NO_ARCHIVE_WRITING_APIS -DMINIZ_NO_TIME -DMINIZ_NO_ASSERT

.PHONY: all clean pre x64 x86

# First explicit rule is NASM entry_*.o; without this, bare `make` / `make -C src_beacon` only builds that goal.
.DEFAULT_GOAL := all

NPROC := $(shell nproc)
ifeq ($(MAKELEVEL), 0)
    MAKEFLAGS += -j$(NPROC) --no-print-directory
endif

# clean/pre must finish before compile. With -j, "all: clean pre x64 x86"
# races and produces: can't create objects_http/*.o: No such file or directory
all:
	@$(MAKE) --no-print-directory clean
	@$(MAKE) --no-print-directory pre
	@$(MAKE) --no-print-directory x64 x86

pre:
	@mkdir -p $(HTTP_DIST_DIR) $(SMB_DIST_DIR) $(TCP_DIST_DIR) $(DNS_DIST_DIR) $(WS_DIST_DIR)
	@ # http
	@ cp $(FILES_DIR)/config.tpl $(HTTP_DIST_DIR)/config.cpp
	@ cp $(FILES_DIR)/stub.x64.bin $(HTTP_DIST_DIR)/stub.x64.bin
	@ cp $(FILES_DIR)/stub.x86.bin $(HTTP_DIST_DIR)/stub.x86.bin
	@ # smb
	@ cp $(FILES_DIR)/config.tpl $(SMB_DIST_DIR)/config.cpp
	@ cp $(FILES_DIR)/stub.x64.bin $(SMB_DIST_DIR)/stub.x64.bin
	@ cp $(FILES_DIR)/stub.x86.bin $(SMB_DIST_DIR)/stub.x86.bin
	@ # tcp
	@ cp $(FILES_DIR)/config.tpl $(TCP_DIST_DIR)/config.cpp
	@ cp $(FILES_DIR)/stub.x64.bin $(TCP_DIST_DIR)/stub.x64.bin
	@ cp $(FILES_DIR)/stub.x86.bin $(TCP_DIST_DIR)/stub.x86.bin
	@ # dns
	@ cp $(FILES_DIR)/config.tpl $(DNS_DIST_DIR)/config.cpp
	@ cp $(FILES_DIR)/stub.x64.bin $(DNS_DIST_DIR)/stub.x64.bin
	@ cp $(FILES_DIR)/stub.x86.bin $(DNS_DIST_DIR)/stub.x86.bin
	@ # ws
	@ cp $(FILES_DIR)/config.tpl $(WS_DIST_DIR)/config.cpp
	@ cp $(FILES_DIR)/stub.x64.bin $(WS_DIST_DIR)/stub.x64.bin
	@ cp $(FILES_DIR)/stub.x86.bin $(WS_DIST_DIR)/stub.x86.bin

clean:
	@rm -rf $(HTTP_DIST_DIR) $(SMB_DIST_DIR) $(TCP_DIST_DIR) $(DNS_DIST_DIR) $(WS_DIST_DIR)
	@mkdir -p $(HTTP_DIST_DIR) $(SMB_DIST_DIR) $(TCP_DIST_DIR) $(DNS_DIST_DIR) $(WS_DIST_DIR)

x64: $(HTTP_OBJECTS_X64) $(SMB_OBJECTS_X64) $(TCP_OBJECTS_X64) $(DNS_OBJECTS_X64) $(WS_OBJECTS_X64) $(INDIRECT_SYSCALL_X64_OBJECTS) $(ENTRY_EXE_X64) $(ENTRY_SVC_X64)
	@ # http
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_HTTP -D BUILD_SVC -o $(HTTP_DIST_DIR)/main_service.x64.o
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D_WIN32_WINNT=0x0600 -D BEACON_HTTP -D BUILD_DLL -o $(HTTP_DIST_DIR)/main_dll.x64.o
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_HTTP -D BUILD_SHELLCODE -o $(HTTP_DIST_DIR)/main_shellcode.x64.o
	@rm -f $(HTTP_DIST_DIR)/ConnectorSMB.x64.o $(HTTP_DIST_DIR)/ConnectorTCP.x64.o $(HTTP_DIST_DIR)/ConnectorDNS.x64.o
	@ # smb
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_SMB -D BUILD_SVC -o $(SMB_DIST_DIR)/main_service.x64.o
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D_WIN32_WINNT=0x0600 -D BEACON_SMB -D BUILD_DLL -o $(SMB_DIST_DIR)/main_dll.x64.o
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_SMB -D BUILD_SHELLCODE -o $(SMB_DIST_DIR)/main_shellcode.x64.o
	@rm -f $(SMB_DIST_DIR)/ConnectorHTTP.x64.o $(SMB_DIST_DIR)/ConnectorTCP.x64.o $(SMB_DIST_DIR)/ConnectorDNS.x64.o
	@ # tcp
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_TCP -D BUILD_SVC -o $(TCP_DIST_DIR)/main_service.x64.o
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D_WIN32_WINNT=0x0600 -D BEACON_TCP -D BUILD_DLL -o $(TCP_DIST_DIR)/main_dll.x64.o
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_TCP -D BUILD_SHELLCODE -o $(TCP_DIST_DIR)/main_shellcode.x64.o
	@rm -f $(TCP_DIST_DIR)/ConnectorHTTP.x64.o $(TCP_DIST_DIR)/ConnectorSMB.x64.o $(TCP_DIST_DIR)/ConnectorDNS.x64.o
	@ # dns
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_DNS -D BUILD_SVC -o $(DNS_DIST_DIR)/main_service.x64.o
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D_WIN32_WINNT=0x0600 -D BEACON_DNS -D BUILD_DLL -o $(DNS_DIST_DIR)/main_dll.x64.o
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_DNS -D BUILD_SHELLCODE -o $(DNS_DIST_DIR)/main_shellcode.x64.o
	@rm -f $(DNS_DIST_DIR)/ConnectorHTTP.x64.o $(DNS_DIST_DIR)/ConnectorSMB.x64.o $(DNS_DIST_DIR)/ConnectorTCP.x64.o
	@ # ws
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_WS -D BUILD_SVC -o $(WS_DIST_DIR)/main_service.x64.o
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D_WIN32_WINNT=0x0600 -D BEACON_WS -D BUILD_DLL -o $(WS_DIST_DIR)/main_dll.x64.o
	@$(CXX_X64) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_WS -D BUILD_SHELLCODE -o $(WS_DIST_DIR)/main_shellcode.x64.o
	@rm -f $(WS_DIST_DIR)/ConnectorHTTP.x64.o $(WS_DIST_DIR)/ConnectorSMB.x64.o $(WS_DIST_DIR)/ConnectorTCP.x64.o $(WS_DIST_DIR)/ConnectorDNS.x64.o

x86: $(HTTP_OBJECTS_X86) $(SMB_OBJECTS_X86) $(TCP_OBJECTS_X86) $(DNS_OBJECTS_X86) $(WS_OBJECTS_X86) $(ENTRY_EXE_X86) $(ENTRY_SVC_X86)
	@ # http
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_HTTP -D BUILD_SVC -o $(HTTP_DIST_DIR)/main_service.x86.o
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D_WIN32_WINNT=0x0600 -D BEACON_HTTP -D BUILD_DLL -o $(HTTP_DIST_DIR)/main_dll.x86.o
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_HTTP -D BUILD_SHELLCODE -o $(HTTP_DIST_DIR)/main_shellcode.x86.o
	@rm -f $(HTTP_DIST_DIR)/ConnectorSMB.x86.o $(HTTP_DIST_DIR)/ConnectorTCP.x86.o $(HTTP_DIST_DIR)/ConnectorDNS.x86.o
	@ # smb
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_SMB -D BUILD_SVC -o $(SMB_DIST_DIR)/main_service.x86.o
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D_WIN32_WINNT=0x0600 -D BEACON_SMB -D BUILD_DLL -o $(SMB_DIST_DIR)/main_dll.x86.o
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_SMB -D BUILD_SHELLCODE -o $(SMB_DIST_DIR)/main_shellcode.x86.o
	@rm -f $(SMB_DIST_DIR)/ConnectorHTTP.x86.o $(SMB_DIST_DIR)/ConnectorTCP.x86.o $(SMB_DIST_DIR)/ConnectorDNS.x86.o
	@ # tcp
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_TCP -D BUILD_SVC -o $(TCP_DIST_DIR)/main_service.x86.o
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D_WIN32_WINNT=0x0600 -D BEACON_TCP -D BUILD_DLL -o $(TCP_DIST_DIR)/main_dll.x86.o
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_TCP -D BUILD_SHELLCODE -o $(TCP_DIST_DIR)/main_shellcode.x86.o
	@rm -f $(TCP_DIST_DIR)/ConnectorHTTP.x86.o $(TCP_DIST_DIR)/ConnectorSMB.x86.o $(TCP_DIST_DIR)/ConnectorDNS.x86.o
	@ # dns
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_DNS -D BUILD_SVC -o $(DNS_DIST_DIR)/main_service.x86.o
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D_WIN32_WINNT=0x0600 -D BEACON_DNS -D BUILD_DLL -o $(DNS_DIST_DIR)/main_dll.x86.o
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_DNS -D BUILD_SHELLCODE -o $(DNS_DIST_DIR)/main_shellcode.x86.o
	@rm -f $(DNS_DIST_DIR)/ConnectorHTTP.x86.o $(DNS_DIST_DIR)/ConnectorSMB.x86.o $(DNS_DIST_DIR)/ConnectorTCP.x86.o
	@ # ws
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_WS -D BUILD_SVC -o $(WS_DIST_DIR)/main_service.x86.o
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D_WIN32_WINNT=0x0600 -D BEACON_WS -D BUILD_DLL -o $(WS_DIST_DIR)/main_dll.x86.o
	@$(CXX_X86) -c $(COMMON_FLAGS) $(BEACON_DIR)/main.cpp -D BEACON_WS -D BUILD_SHELLCODE -o $(WS_DIST_DIR)/main_shellcode.x86.o
	@rm -f $(WS_DIST_DIR)/ConnectorHTTP.x86.o $(WS_DIST_DIR)/ConnectorSMB.x86.o $(WS_DIST_DIR)/ConnectorTCP.x86.o $(WS_DIST_DIR)/ConnectorDNS.x86.o

bin_x64: x64
	@echo "    * Extracting x64 shellcode bins (HTTP/SMB/TCP/DNS)"
	@# HTTP
	@$(CXX_X64) $(LFlags) -Wl,-T,$(BEACON_DIR)/Shellcode.ld -Wl,-e,AxAsmEntry -o $(HTTP_DIST_DIR)/beacon_http.x64.elf \
		$(HTTP_DIST_DIR)/config.o $(HTTP_DIST_DIR)/ConnectorHTTP.x64.o $(HTTP_OBJECTS_X64) $(INDIRECT_SYSCALL_X64_OBJECTS) $(HTTP_DIST_DIR)/entry_exe.x64.o
	@llvm-objcopy -O binary --only-section=.text $(HTTP_DIST_DIR)/beacon_http.x64.elf $(HTTP_DIST_DIR)/beacon_http.x64.bin
	@# SMB
	@$(CXX_X64) $(LFlags) -Wl,-T,$(BEACON_DIR)/Shellcode.ld -Wl,-e,AxAsmEntry -o $(SMB_DIST_DIR)/beacon_smb.x64.elf \
		$(SMB_DIST_DIR)/config.o $(SMB_DIST_DIR)/ConnectorSMB.x64.o $(SMB_OBJECTS_X64) $(INDIRECT_SYSCALL_X64_OBJECTS) $(SMB_DIST_DIR)/entry_exe.x64.o
	@llvm-objcopy -O binary --only-section=.text $(SMB_DIST_DIR)/beacon_smb.x64.elf $(SMB_DIST_DIR)/beacon_smb.x64.bin
	@# TCP
	@$(CXX_X64) $(LFlags) -Wl,-T,$(BEACON_DIR)/Shellcode.ld -Wl,-e,AxAsmEntry -o $(TCP_DIST_DIR)/beacon_tcp.x64.elf \
		$(TCP_DIST_DIR)/config.o $(TCP_DIST_DIR)/ConnectorTCP.x64.o $(TCP_OBJECTS_X64) $(INDIRECT_SYSCALL_X64_OBJECTS) $(TCP_DIST_DIR)/entry_exe.x64.o
	@llvm-objcopy -O binary --only-section=.text $(TCP_DIST_DIR)/beacon_tcp.x64.elf $(TCP_DIST_DIR)/beacon_tcp.x64.bin
	@# DNS
	@$(CXX_X64) $(LFlags) -Wl,-T,$(BEACON_DIR)/Shellcode.ld -Wl,-e,AxAsmEntry -o $(DNS_DIST_DIR)/beacon_dns.x64.elf \
		$(DNS_DIST_DIR)/config.o $(DNS_DIST_DIR)/ConnectorDNS.x64.o $(DNS_OBJECTS_X64) $(INDIRECT_SYSCALL_X64_OBJECTS) $(DNS_DIST_DIR)/entry_exe.x64.o
	@llvm-objcopy -O binary --only-section=.text $(DNS_DIST_DIR)/beacon_dns.x64.elf $(DNS_DIST_DIR)/beacon_dns.x64.bin

bin_x86: x86
	@echo "    * Extracting x86 shellcode bins (HTTP/SMB/TCP/DNS)"
	@# HTTP
	@$(CXX_X86) $(LFlags) -Wl,-T,$(BEACON_DIR)/Shellcode.ld -Wl,-e,_AxAsmEntry -o $(HTTP_DIST_DIR)/beacon_http.x86.elf \
		$(HTTP_DIST_DIR)/config.o $(HTTP_DIST_DIR)/ConnectorHTTP.x86.o $(HTTP_OBJECTS_X86) $(HTTP_DIST_DIR)/entry_exe.x86.o
	@llvm-objcopy -O binary --only-section=.text $(HTTP_DIST_DIR)/beacon_http.x86.elf $(HTTP_DIST_DIR)/beacon_http.x86.bin
	@# SMB
	@$(CXX_X86) $(LFlags) -Wl,-T,$(BEACON_DIR)/Shellcode.ld -Wl,-e,_AxAsmEntry -o $(SMB_DIST_DIR)/beacon_smb.x86.elf \
		$(SMB_DIST_DIR)/config.o $(SMB_DIST_DIR)/ConnectorSMB.x86.o $(SMB_OBJECTS_X86) $(SMB_DIST_DIR)/entry_exe.x86.o
	@llvm-objcopy -O binary --only-section=.text $(SMB_DIST_DIR)/beacon_smb.x86.elf $(SMB_DIST_DIR)/beacon_smb.x86.bin
	@# TCP
	@$(CXX_X86) $(LFlags) -Wl,-T,$(BEACON_DIR)/Shellcode.ld -Wl,-e,_AxAsmEntry -o $(TCP_DIST_DIR)/beacon_tcp.x86.elf \
		$(TCP_DIST_DIR)/config.o $(TCP_DIST_DIR)/ConnectorTCP.x86.o $(TCP_OBJECTS_X86) $(TCP_DIST_DIR)/entry_exe.x86.o
	@llvm-objcopy -O binary --only-section=.text $(TCP_DIST_DIR)/beacon_tcp.x86.elf $(TCP_DIST_DIR)/beacon_tcp.x86.bin
	@# DNS
	@$(CXX_X86) $(LFlags) -Wl,-T,$(BEACON_DIR)/Shellcode.ld -Wl,-e,_AxAsmEntry -o $(DNS_DIST_DIR)/beacon_dns.x86.elf \
		$(DNS_DIST_DIR)/config.o $(DNS_DIST_DIR)/ConnectorDNS.x86.o $(DNS_OBJECTS_X86) $(DNS_DIST_DIR)/entry_exe.x86.o
	@llvm-objcopy -O binary --only-section=.text $(DNS_DIST_DIR)/beacon_dns.x86.elf $(DNS_DIST_DIR)/beacon_dns.x86.bin

# crt.cpp: Intel-syntax inline asm only — -masm=intel must be local (not in COMMON_FLAGS).
# -fno-ms-extensions: Clang + -fms-extensions declares MSVC _alloca(void*) which clashes with our naked void _alloca(void).
$(HTTP_DIST_DIR)/crt.x64.o: beacon/crt.cpp
	@$(CXX_X64) -c $(COMMON_FLAGS) -masm=intel -fno-ms-extensions -D BEACON_HTTP -c $< -o $@
$(HTTP_DIST_DIR)/crt.x86.o: beacon/crt.cpp
	@$(CXX_X86) -c $(COMMON_FLAGS) -masm=intel -fno-ms-extensions -D BEACON_HTTP -c $< -o $@
$(SMB_DIST_DIR)/crt.x64.o: beacon/crt.cpp
	@$(CXX_X64) -c $(COMMON_FLAGS) -masm=intel -fno-ms-extensions -D BEACON_SMB -c $< -o $@
$(SMB_DIST_DIR)/crt.x86.o: beacon/crt.cpp
	@$(CXX_X86) -c $(COMMON_FLAGS) -masm=intel -fno-ms-extensions -D BEACON_SMB -c $< -o $@
$(TCP_DIST_DIR)/crt.x64.o: beacon/crt.cpp
	@$(CXX_X64) -c $(COMMON_FLAGS) -masm=intel -fno-ms-extensions -D BEACON_TCP -c $< -o $@
$(TCP_DIST_DIR)/crt.x86.o: beacon/crt.cpp
	@$(CXX_X86) -c $(COMMON_FLAGS) -masm=intel -fno-ms-extensions -D BEACON_TCP -c $< -o $@
$(DNS_DIST_DIR)/crt.x64.o: beacon/crt.cpp
	@$(CXX_X64) -c $(COMMON_FLAGS) -masm=intel -fno-ms-extensions -D BEACON_DNS -c $< -o $@
$(DNS_DIST_DIR)/crt.x86.o: beacon/crt.cpp
	@$(CXX_X86) -c $(COMMON_FLAGS) -masm=intel -fno-ms-extensions -D BEACON_DNS -c $< -o $@
$(WS_DIST_DIR)/crt.x64.o: beacon/crt.cpp
	@$(CXX_X64) -c $(COMMON_FLAGS) -masm=intel -fno-ms-extensions -D BEACON_WS -c $< -o $@
$(WS_DIST_DIR)/crt.x86.o: beacon/crt.cpp
	@$(CXX_X86) -c $(COMMON_FLAGS) -masm=intel -fno-ms-extensions -D BEACON_WS -c $< -o $@

$(HTTP_DIST_DIR)/%.x64.o: beacon/%.cpp
	@mkdir -p $(HTTP_DIST_DIR)
	@$(CXX_X64) -c $(COMMON_FLAGS) -D BEACON_HTTP -c $< -o $@

$(HTTP_DIST_DIR)/%.x86.o: beacon/%.cpp
	@mkdir -p $(HTTP_DIST_DIR)
	@$(CXX_X86) -c $(COMMON_FLAGS) -D BEACON_HTTP -c $< -o $@



$(SMB_DIST_DIR)/%.x64.o: beacon/%.cpp
	@mkdir -p $(SMB_DIST_DIR)
	@$(CXX_X64) -c $(COMMON_FLAGS) -D BEACON_SMB -c $< -o $@

$(SMB_DIST_DIR)/%.x86.o: beacon/%.cpp
	@mkdir -p $(SMB_DIST_DIR)
	@$(CXX_X86) -c $(COMMON_FLAGS) -D BEACON_SMB -c $< -o $@



$(TCP_DIST_DIR)/%.x64.o: beacon/%.cpp
	@mkdir -p $(TCP_DIST_DIR)
	@$(CXX_X64) -c $(COMMON_FLAGS) -D BEACON_TCP -c $< -o $@

$(TCP_DIST_DIR)/%.x86.o: beacon/%.cpp
	@mkdir -p $(TCP_DIST_DIR)
	@$(CXX_X86) -c $(COMMON_FLAGS) -D BEACON_TCP -c $< -o $@



$(DNS_DIST_DIR)/%.x64.o: beacon/%.cpp
	@mkdir -p $(DNS_DIST_DIR)
	@$(CXX_X64) -c $(COMMON_FLAGS) -D BEACON_DNS -c $< -o $@

$(DNS_DIST_DIR)/%.x86.o: beacon/%.cpp
	@mkdir -p $(DNS_DIST_DIR)
	@$(CXX_X86) -c $(COMMON_FLAGS) -D BEACON_DNS -c $< -o $@


$(WS_DIST_DIR)/%.x64.o: beacon/%.cpp
	@mkdir -p $(WS_DIST_DIR)
	@$(CXX_X64) -c $(COMMON_FLAGS) -D BEACON_WS -c $< -o $@

$(WS_DIST_DIR)/%.x86.o: beacon/%.cpp
	@mkdir -p $(WS_DIST_DIR)
	@$(CXX_X86) -c $(COMMON_FLAGS) -D BEACON_WS -c $< -o $@

# Miniz: compile with trim macros for ALL beacon types to eliminate CRT deps
$(HTTP_DIST_DIR)/miniz.x64.o: beacon/miniz.cpp
	@$(CXX_X64) -c $(COMMON_FLAGS) -D BEACON_HTTP $(MINIZ_TRIM_FLAGS) -c $< -o $@

$(HTTP_DIST_DIR)/miniz.x86.o: beacon/miniz.cpp
	@$(CXX_X86) -c $(COMMON_FLAGS) -D BEACON_HTTP $(MINIZ_TRIM_FLAGS) -c $< -o $@

$(SMB_DIST_DIR)/miniz.x64.o: beacon/miniz.cpp
	@$(CXX_X64) -c $(COMMON_FLAGS) -D BEACON_SMB $(MINIZ_TRIM_FLAGS) -c $< -o $@

$(SMB_DIST_DIR)/miniz.x86.o: beacon/miniz.cpp
	@$(CXX_X86) -c $(COMMON_FLAGS) -D BEACON_SMB $(MINIZ_TRIM_FLAGS) -c $< -o $@

$(TCP_DIST_DIR)/miniz.x64.o: beacon/miniz.cpp
	@$(CXX_X64) -c $(COMMON_FLAGS) -D BEACON_TCP $(MINIZ_TRIM_FLAGS) -c $< -o $@

$(TCP_DIST_DIR)/miniz.x86.o: beacon/miniz.cpp
	@$(CXX_X86) -c $(COMMON_FLAGS) -D BEACON_TCP $(MINIZ_TRIM_FLAGS) -c $< -o $@

$(DNS_DIST_DIR)/miniz.x64.o: beacon/miniz.cpp
	@$(CXX_X64) -c $(COMMON_FLAGS) -D BEACON_DNS $(MINIZ_TRIM_FLAGS) -c $< -o $@

$(DNS_DIST_DIR)/miniz.x86.o: beacon/miniz.cpp
	@$(CXX_X86) -c $(COMMON_FLAGS) -D BEACON_DNS $(MINIZ_TRIM_FLAGS) -c $< -o $@

$(WS_DIST_DIR)/miniz.x64.o: beacon/miniz.cpp
	@$(CXX_X64) -c $(COMMON_FLAGS) -D BEACON_WS $(MINIZ_TRIM_FLAGS) -c $< -o $@

$(WS_DIST_DIR)/miniz.x86.o: beacon/miniz.cpp
	@$(CXX_X86) -c $(COMMON_FLAGS) -D BEACON_WS $(MINIZ_TRIM_FLAGS) -c $< -o $@
