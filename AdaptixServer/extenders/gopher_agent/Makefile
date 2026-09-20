all: clean
	@ echo "    * Building agent_gopher plugin"
	@ mkdir dist
	@ cp config.yaml ax_config.axs ./dist/
	@ go build -buildmode=plugin -ldflags="-s -w" -o ./dist/agent_gopher.so .
	@ echo "      done..."

	@ echo "    * Building agent"
	@ (cd src_gopher && $(MAKE) --no-print-directory)
	@ cp -r src_gopher ./dist/src_gopher
	@ echo "      done..."

clean:
	@ rm -rf dist
