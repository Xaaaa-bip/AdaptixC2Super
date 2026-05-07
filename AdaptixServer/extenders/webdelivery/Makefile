all: clean
	@ echo " * Building webdelivery plugin"
	@ mkdir -p dist
	@ cp config.yaml ./dist/
	@ CGO_ENABLED=1 GOEXPERIMENT=jsonv2,greenteagc go build -buildmode=plugin -ldflags="-s -w" -o ./dist/webdelivery.so pl_main.go
	@ echo " done..."

clean:
	@ rm -rf dist
