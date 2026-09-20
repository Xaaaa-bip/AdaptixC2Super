all: clean
	@ echo "    * Building agent_linux plugin"
	@ mkdir dist
	@ cp config.yaml ax_config.axs ./dist/
	@ mkdir -p ./dist/templates
	@ cp templates/stager_linux.c ./dist/templates/
	@ go build -buildmode=plugin -ldflags="-s -w" -o ./dist/agent_linux.so pl_main.go pl_utils.go pl_hashes_linux.go pl_encoder_linux.go session_crypto.go pl_stager_linux.go matrix_resolver.go pl_bofs_api.go
	@ echo "      done..."

	@ echo "    * Copying agent sources for per-payload compilation"
	@ mkdir -p ./dist/src_agent/agent ./dist/src_agent/files
	@ cp src_agent/agent/*.c src_agent/agent/*.h ./dist/src_agent/agent/
	@ echo "      ws_connector* copied"
	@ rm -f ./dist/src_agent/agent/config.h ./dist/src_agent/agent/ApiDefines.h ./dist/src_agent/agent/strings_obf.h
	@ cp src_agent/files/config.tpl ./dist/src_agent/files/ 2>/dev/null || true
# ★ BOF 源码同样是「运行期编译」的输入：pl_bofs_api.go 的 compile 端点要读
#   src_agent/bofs/<name>.c，-I 用的则是上面那批 src_agent/agent/*.h。
#   与 src_agent/agent 同理，必须随 dist 一起交付。
#   漏了会怎样（批次20 生产实证）：run 树没有 src_agent/bofs/ ⇒ BOF 页 26 个模块
#   全部显示「无源码」、单编与全量重编一律失败；而上传路径会 MkdirAll 自愈，
#   于是「上传能用、编译不能用」，最难排查的那种半可用状态。
#   这里**故意不加** `2>/dev/null || true`：目录缺失就该让构建当场失败，
#   而不是静默交付一个编译功能不可用的插件（fail-closed）。
	@ echo "    * Copying BOF sources (runtime compile API input)"
	@ mkdir -p ./dist/src_agent/bofs
	@ cp src_agent/bofs/*.c ./dist/src_agent/bofs/
	@ echo "    * Copying embedded mbedtls (H1 TLS) sources"
	@ mkdir -p ./dist/src_agent/mbedtls
	@ cp -r src_agent/mbedtls ./dist/src_agent/
	@ echo "      done..."

clean:
	@ rm -rf dist
