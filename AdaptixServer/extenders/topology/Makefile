# ★ 这一行不能少。缺了它，若第一个目标不是 all，`make` 会报
# "Nothing to be done for 'all'" 然后**静默退出 0** —— 构建脚本看到
# 退出码 0 以为成功了，实际 dist/ 里什么都没有。
# （本项目已踩过：kharon-sync 会把这一行悄悄抹掉。）
.DEFAULT_GOAL := all

all: clean
	@ echo " * Building topology plugin"
	@ mkdir -p dist
	@ cp config.yaml ./dist/
	@ CGO_ENABLED=1 go build -buildmode=plugin -ldflags="-s -w" -o ./dist/topology.so pl_main.go
	@ echo " done..."

clean:
	@ rm -rf dist
