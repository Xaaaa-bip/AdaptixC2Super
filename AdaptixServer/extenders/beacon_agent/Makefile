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
	@ mv $(ROOT)src_beacon/objects_http $(ROOT)dist/objects_http
	@ mv $(ROOT)src_beacon/objects_smb $(ROOT)dist/objects_smb
	@ mv $(ROOT)src_beacon/objects_tcp $(ROOT)dist/objects_tcp
	@ mv $(ROOT)src_beacon/objects_dns $(ROOT)dist/objects_dns
	@ echo "      done..."

clean:
	@ rm -rf $(ROOT)dist
