all: clean
	@ echo " * Building vnc plugin"
	@ mkdir -p dist
	@ cp config.yaml ax_config.axs ./dist/
	@ cp -r web ./dist/
	@ CGO_ENABLED=1 GOEXPERIMENT=jsonv2,greenteagc go build -buildmode=plugin -ldflags="-s -w" -o ./dist/vnc.so .
	@ echo " done..."

clean:
	@ rm -rf dist
