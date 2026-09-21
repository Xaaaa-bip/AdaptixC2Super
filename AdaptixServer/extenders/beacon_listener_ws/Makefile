all: clean
	@ echo "    * Building listener_beacon_ws plugin"
	@ mkdir dist
	@ cp config.yaml ax_config.axs ./dist/
	@ go build -buildmode=plugin -ldflags="-s -w" -o ./dist/listener_beacon_ws.so pl_main.go pl_transport.go
	@ echo "      done..."

clean:
	@ rm -rf dist
