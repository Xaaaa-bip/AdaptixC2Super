all: clean
	@ echo "    * Building listener_beacon_https_emu plugin"
	@ mkdir dist
	@ cp config.yaml ax_config.axs ./dist/
	@ go build -buildmode=plugin -ldflags="-s -w" -o ./dist/listener_beacon_https_emu.so pl_main.go pl_transport.go
	@ echo "      done..."

clean:
	@ rm -rf dist
