all: clean
	@ echo "    * Building agent_linux plugin"
	@ mkdir dist
	@ cp config.yaml ax_config.axs ./dist/
	@ mkdir -p ./dist/templates
	@ cp templates/stager_linux.c ./dist/templates/
	@ GOEXPERIMENT=jsonv2,greenteagc go build -buildmode=plugin -ldflags="-s -w" -o ./dist/agent_linux.so pl_main.go pl_utils.go pl_hashes_linux.go pl_encoder_linux.go session_crypto.go pl_stager_linux.go
	@ echo "      done..."

	@ echo "    * Copying agent sources for per-payload compilation"
	@ mkdir -p ./dist/src_agent/agent ./dist/src_agent/files
	@ cp src_agent/agent/*.c src_agent/agent/*.h ./dist/src_agent/agent/
	@ echo "      ws_connector* copied"
	@ rm -f ./dist/src_agent/agent/config.h ./dist/src_agent/agent/ApiDefines.h ./dist/src_agent/agent/strings_obf.h
	@ cp src_agent/files/config.tpl ./dist/src_agent/files/ 2>/dev/null || true
	@ echo "      done..."

clean:
	@ rm -rf dist
