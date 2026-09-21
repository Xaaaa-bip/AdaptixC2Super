all: clean
	@ echo " * Building notifier plugin"
	@ mkdir -p dist
	@ cp config.yaml ax_config.axs ./dist/
	@ go build -buildmode=plugin -ldflags="-s -w" -o ./dist/notifier.so pl_main.go pl_utils.go
	@ echo " done..."

clean:
	@ rm -rf dist
