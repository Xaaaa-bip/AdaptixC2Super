# Paths relative to this file so `make -f …/beacon_agent/Makefile` works from any cwd.
ROOT := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

all: clean
	@ echo "    * Building agent_beacon plugin"
	@ mkdir -p $(ROOT)dist
	@ cp $(ROOT)config.yaml $(ROOT)ax_config.axs $(ROOT)dist/
	@ cd $(ROOT) && GOEXPERIMENT=jsonv2,greenteagc go build -buildmode=plugin -ldflags="-s -w" -o ./dist/agent_beacon.so .
	@ echo "      done..."

	@ echo "    * Building agent"
	@ $(MAKE) -C $(ROOT)src_beacon --no-print-directory
	@ mkdir -p $(ROOT)dist/beacon
	@ cp $(ROOT)src_beacon/beacon/Linker.ld $(ROOT)dist/beacon/Linker.ld
	@ cp $(ROOT)src_beacon/beacon/Shellcode.ld $(ROOT)dist/beacon/Shellcode.ld
	@ cp -r $(ROOT)artifacts $(ROOT)dist/
	@ # stager / PE helpers required at generate time next to agent_beacon.so
	@ if [ -d "$(ROOT)templates" ]; then cp -a "$(ROOT)templates" "$(ROOT)dist/"; fi
	@ if [ -f "$(ROOT)pe_header_mod.py" ]; then cp -f "$(ROOT)pe_header_mod.py" "$(ROOT)dist/"; fi
	@ # object dirs may not all exist (optional flavors); never hard-fail on missing
	@ for d in objects_http objects_smb objects_tcp objects_dns objects_ws; do \
		if [ -d "$(ROOT)src_beacon/$$d" ]; then \
			rm -rf "$(ROOT)dist/$$d"; \
			mv "$(ROOT)src_beacon/$$d" "$(ROOT)dist/$$d"; \
		fi; \
	done
	@ echo "      done..."

clean:
	@ rm -rf $(ROOT)dist
