# 只能在 Linux 上跑：-buildmode=plugin 要求目标平台与宿主一致，
# 插件的 .so 也必须与 adaptixserver 是同一个 Go 版本编出来的。
#
# 部署：make && cp -r dist/* /root/cc/run/extenders/argfuscator/
#   —— vendor_ps/ 必须跟着 .so 一起过去：pl_main.go 是在
#      <extender 目录>/vendor_ps/ 下找包装脚本和 models/ 的。
all: clean
	@ echo " * Building argfuscator plugin"
	@ mkdir -p dist
	@ cp config.yaml ./dist/
	@ cp -r vendor_ps ./dist/
	@ CGO_ENABLED=1 go build -buildmode=plugin -ldflags="-s -w" -o ./dist/argfuscator.so pl_main.go
	@ echo " done..."

clean:
	@ rm -rf dist
