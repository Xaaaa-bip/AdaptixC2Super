all: clean
	@ echo "    * Building web_delivery plugin"
	@ mkdir dist
	@ cp config.yaml ax_config.axs ./dist/
	@ GOEXPERIMENT=jsonv2,greenteagc go build -buildmode=plugin -ldflags="-s -w" -o ./dist/web_delivery.so pl_main.go pl_server.go pl_builder.go pl_ua.go pl_crypto.go
	@ echo "      done..."

clean:
	@ rm -rf dist
