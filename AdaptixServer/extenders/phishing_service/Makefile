all: clean
	@ echo "    * Building phishing service plugin"
	@ mkdir dist
	@ cp config.yaml ax_config.axs ./dist/
	@ cp -r templates landers ./dist/
	@ go build -buildmode=plugin -ldflags="-s -w" -o ./dist/service_phishing.so *.go
	@ echo "      done..."

clean:
	@ rm -rf dist
