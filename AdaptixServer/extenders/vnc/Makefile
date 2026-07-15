all: clean
	@ echo " * Building vnc plugin"
	@ mkdir -p dist
	@ cp config.yaml ax_config.axs ./dist/
	@ cp -r web ./dist/
	@ # agent payloads required by beacon "vnc start" auto-upload (next to plugin dir)
	@ if [ -d agent ]; then cp -a agent ./dist/; fi
	@ CGO_ENABLED=1 GOEXPERIMENT=jsonv2,greenteagc go build -buildmode=plugin -ldflags="-s -w" -o ./dist/vnc.so .
	@ echo " done..."

clean:
	@ rm -rf dist
