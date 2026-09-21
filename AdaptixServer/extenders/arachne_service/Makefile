all: clean
	@ echo "    * Building arachne service plugin"
	@ mkdir dist
	@ cp config.yaml ax_config.axs ./dist/
	@ GOWORK=off go build -buildmode=plugin -ldflags="-s -w" -o ./dist/arachne_service.so pl_main.go
	@ echo "      done..."

clean:
	@ rm -rf dist
