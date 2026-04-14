.PHONY: all plugin agent core beacon bundle clean help

GO := go
GOFLAGS := -buildmode=plugin -ldflags="-s -w"
GOEXPERIMENT := jsonv2,greenteagc
DIST_DIR := dist
SRC_SERVER := src_server
SRC_BEACON := src_beacon
SRC_CORE := src_core
SRC_MODULES := src_modules

PLUGIN_FILES := \
	$(SRC_SERVER)/pl_lokycrypt.go \
	$(SRC_SERVER)/pl_malleable.go \
	$(SRC_SERVER)/pl_main.go \
	$(SRC_SERVER)/pl_agent.go \
	$(SRC_SERVER)/pl_packer.go \
	$(SRC_SERVER)/pl_utils.go

PLUGIN_OUT := $(DIST_DIR)/agent_kharon.so

plugin: $(PLUGIN_OUT)

$(PLUGIN_OUT): $(PLUGIN_FILES) config.yaml ax_config.axs
	@echo "[*] Building agent_kharon plugin..."
	@mkdir -p $(DIST_DIR)
	@cp -f config.yaml ax_config.axs $(DIST_DIR)/
	@GOEXPERIMENT=$(GOEXPERIMENT) $(GO) build $(GOFLAGS) \
		-o $@ \
		$(PLUGIN_FILES)
	@echo "[✓] Plugin built: $(PLUGIN_OUT)"

agent: plugin
	@echo "[*] Building agent_kharon src code (beacon prebuild)..."
	@$(MAKE) -C $(SRC_BEACON) prebuild-x64 --no-print-directory
	@echo "[✓] Agent beacon prebuild (x64) done"

core: plugin
	@echo "[*] Building agent_kharon src_core (.o modules)..."
	@$(MAKE) -C $(SRC_CORE) all --no-print-directory
	@echo "[✓] src_core built"

# Final beacon shellcode (x64); matches Kharon/setup_kharon.sh build_agent_beacon
beacon: agent core
	@echo "[*] Linking beacon x64..."
	@$(MAKE) -C $(SRC_BEACON) x64 --no-print-directory
	@echo "[✓] Beacon x64 (.bin) built"

# 将 setup_kharon.sh 中 copy_agent_dist 的内容打进 dist/，供仓库根 make extenders 的 mv 使用
bundle: plugin agent core beacon
	@echo "[*] Bundling beacon bins + sources into $(DIST_DIR)..."
	@mkdir -p $(DIST_DIR)/beacon_bin
	@if ls $(SRC_BEACON)/Bin/*.bin >/dev/null 2>&1; then \
		cp -f $(SRC_BEACON)/Bin/*.bin $(DIST_DIR)/beacon_bin/; \
	fi
	@rm -rf $(DIST_DIR)/src_beacon $(DIST_DIR)/src_loader $(DIST_DIR)/src_core $(DIST_DIR)/src_modules
	@cp -a $(SRC_BEACON) $(DIST_DIR)/src_beacon
	@cp -a src_loader $(DIST_DIR)/src_loader
	@cp -a $(SRC_CORE) $(DIST_DIR)/src_core
	@if [ -d "$(SRC_MODULES)" ]; then cp -a $(SRC_MODULES) $(DIST_DIR)/src_modules; fi
	@echo "[✓] Bundle complete"

all: clean bundle

clean:
	@echo "[*] Cleaning build artifacts..."
	@rm -rf "$(DIST_DIR)"
	@$(MAKE) -C $(SRC_BEACON) clean --no-print-directory || true
	@$(MAKE) -C $(SRC_CORE) clean --no-print-directory || true
	@echo "[✓] Clean complete"

.SILENT: help