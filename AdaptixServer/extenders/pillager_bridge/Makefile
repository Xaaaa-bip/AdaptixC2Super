ROOT := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
SRC_BOF := $(ROOT)src_bof
MK := $(MAKE) -f $(abspath $(lastword $(MAKEFILE_LIST)))

# 注意：不要写 all: clean bundle-bofs（两个前置目标在 make -j 下会并行，bundle-bofs 可能先跑，看到旧 .o → Nothing to be done）
# 第一个目标若是 clean-src-bof，无参数 make 只会清理不建 dist；extenders 里正是裸调 make
.DEFAULT_GOAL := all
.PHONY: all clean clean-src-bof bundle-bofs

clean-src-bof:
	@ rm -f $(ROOT)bofs/*.o
	@ $(MAKE) -C $(SRC_BOF)/pillager_browser --no-print-directory clean
	@ $(MAKE) -C $(SRC_BOF)/pillager_mini --no-print-directory clean

clean: clean-src-bof
	@ rm -rf $(ROOT)dist

# 仅重编 BOF、拷到 bofs/（不删 dist）；依赖 clean-src-bof 保证子目录一定重新编译
bundle-bofs: clean-src-bof
	@ mkdir -p $(ROOT)bofs
	@ echo "    [*] Building src_bof/pillager_browser ..."
	@ $(MAKE) -C $(SRC_BOF)/pillager_browser --no-print-directory all
	@ cp -f $(SRC_BOF)/pillager_browser/pillager_browser.x64.o $(ROOT)bofs/
	@ echo "    [+] bofs/pillager_browser.x64.o"
	@ echo "    [*] Building src_bof/pillager_mini ..."
	@ $(MAKE) -C $(SRC_BOF)/pillager_mini --no-print-directory all
	@ cp -f $(SRC_BOF)/pillager_mini/pillager_mini.x64.o $(ROOT)bofs/
	-@ cp -f $(SRC_BOF)/pillager_mini/pillager_mini.x86.o $(ROOT)bofs/ 2>/dev/null || true
	@ echo "    [+] bofs/pillager_mini.x64.o + pillager_mini.x86.o"

# 串行：先 clean 再 bundle-bofs，再打包 dist + go build（不受 -j 影响）
all:
	@ $(MK) clean
	@ $(MK) bundle-bofs
	@ echo " * Building pillager_bridge service plugin"
	@ mkdir -p $(ROOT)dist/bofs
	@ cp $(ROOT)config.yaml $(ROOT)ax_config.axs $(ROOT)dist/
	@ cp -f $(ROOT)bofs/*.o $(ROOT)dist/bofs/ 2>/dev/null || true
	@ cp $(ROOT)bofs/README.txt $(ROOT)dist/bofs/ 2>/dev/null || true
	@ cd $(ROOT) && GOWORK=off GOEXPERIMENT=jsonv2,greenteagc go build -buildmode=plugin -ldflags="-s -w" -o ./dist/pillager_bridge.so pl_main.go pl_utils.go
	@ echo " done..."
