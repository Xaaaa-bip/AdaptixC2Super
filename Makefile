all: clean prepare server client extenders

DIST_DIR := dist

UNAME_S := $(shell uname -s)

ifeq ($(UNAME_S),Linux)
  NPROC := $(shell nproc)
endif

ifeq ($(UNAME_S),Darwin)
  NPROC := $(shell sysctl -n hw.ncpu)
endif



### CLEAN ###

prepare:
	@if [ ! -d "$(DIST_DIR)" ]; then \
		mkdir "$(DIST_DIR)"; \
		echo "[+] Created '$(DIST_DIR)' directory"; \
	fi

clean:
	@ rm -rf $(DIST_DIR)

clean-all: clean
	@ echo "[*] Cleaning all build artifacts..."
	@ find . -name "*.o" -delete
	@ find . -name "*.so" -delete
	@ find . -name "*.a" -delete
	@ find . -name "build_error.log" -delete
	@ find . -name "cmake_error.log" -delete
	@ echo "[+] All artifacts cleaned"

docker-clean:
	@ echo "[*] Cleaning Docker containers and images..."
	@ docker compose --profile build-server --profile build-extenders --profile build-server-ext down --rmi local 2>/dev/null || true
	@ echo "[+] Docker containers and images cleaned"

docker-clean-all: docker-clean
	@ echo "[*] Cleaning all Docker artifacts (containers, images, volumes, networks)..."
	@ docker compose --profile build-server --profile build-extenders --profile build-server-ext --profile runtime down --rmi all --volumes 2>/dev/null || true
	@ echo "[*] Cleaning build output directories..."
	@ rm -rf AdaptixServer/server-dist 2>/dev/null || true
	@ echo "[+] All Docker artifacts cleaned"



### EXTENDERS ###

EXTENDER_DIRS := $(shell find AdaptixServer/extenders -maxdepth 1 -type d -not -path "$(DIST_DIR)" -exec test -f {}/Makefile \; -print)

# 与 Kharon/setup_kharon.sh 的 copy_agent / copy_listener 一致：从并列 Kharon 仓库拷入 extenders（可选）
KHRON_ROOT ?= ../Kharon

kharon-sync:
	@if [ ! -d "$(KHRON_ROOT)/agent_kharon" ]; then \
		echo "[!] kharon-sync: 跳过（未找到 $(KHRON_ROOT)/agent_kharon）"; \
	elif [ ! -d "$(KHRON_ROOT)/listener_kharon_http" ]; then \
		echo "[!] kharon-sync: 跳过（未找到 $(KHRON_ROOT)/listener_kharon_http）"; \
	else \
		echo "[*] kharon-sync: 同步 $(KHRON_ROOT) -> AdaptixServer/extenders ..."; \
		rm -rf AdaptixServer/extenders/agent_kharon AdaptixServer/extenders/listener_kharon_http; \
		cp -a "$(KHRON_ROOT)/agent_kharon" AdaptixServer/extenders/; \
		cp -a "$(KHRON_ROOT)/listener_kharon_http" AdaptixServer/extenders/; \
		cd AdaptixServer && go work sync; \
		echo "[+] kharon-sync 完成"; \
	fi

extenders: prepare
	@ echo "[*] Building default extenders"
	@ mkdir -p $(DIST_DIR)/extenders
	@ for dir in $(EXTENDER_DIRS); do \
		echo "[*]  $$dir"; \
		(cd $$dir && $(MAKE) --no-print-directory) || exit 1; \
		plugin_name=$$(basename $$dir); \
		if [ ! -d "$$dir/dist" ]; then echo "[ERROR] $$dir/dist 不存在（Makefile 未生成 dist）"; exit 1; fi; \
		rm -rf $(DIST_DIR)/extenders/$$plugin_name; \
		mv $$dir/dist $(DIST_DIR)/extenders/$$plugin_name; \
	done
	@ echo "[+] done"

# 先同步并列 Kharon 源码再 server-ext（等价于先跑 setup_kharon 再编译）
server-ext-with-kharon: kharon-sync server-ext


### SERVER ###

server: prepare
	@ echo "[*] Building adaptixserver..."
	@ cd AdaptixServer && GOEXPERIMENT=jsonv2,greenteagc go build -buildvcs=false -ldflags="-s -w" -o adaptixserver 2>build_error.log || { echo "[ERROR] Failed to build AdaptixServer:"; cat build_error.log >&2; exit 1; }     # for static build use CGO_ENABLED=0
	@ mv AdaptixServer/adaptixserver ./$(DIST_DIR)/
	@ cp AdaptixServer/ssl_gen.sh AdaptixServer/profile.yaml AdaptixServer/404page.html ./$(DIST_DIR)/
	@ echo "[+] done"

### WEB ###

# 直接拷贝已构建的静态控制台到 dist/web（不跑 npm）
# 优先：AdaptixWeb/dist（本仓库前端产物）
# 其次：WEB_SRC 环境变量 / 参数指定的现成 web 目录（如线上 dist/web）
WEB_DIR := AdaptixWeb
WEB_SRC ?=

web: prepare
	@ echo "[*] Copying Web console -> $(DIST_DIR)/web (no npm build) ..."
	@ src=""; \
	if [ -n "$(WEB_SRC)" ] && [ -d "$(WEB_SRC)" ] && [ -f "$(WEB_SRC)/index.html" ]; then \
		src="$(WEB_SRC)"; \
	elif [ -d "$(WEB_DIR)/dist" ] && [ -f "$(WEB_DIR)/dist/index.html" ]; then \
		src="$(WEB_DIR)/dist"; \
	elif [ -f "$(WEB_DIR)/index.html" ] && [ -d "$(WEB_DIR)/assets" ]; then \
		src="$(WEB_DIR)"; \
	else \
		echo "[ERROR] 找不到 Web 静态产物。任选其一："; \
		echo "  - 放好 $(WEB_DIR)/dist/（含 index.html）"; \
		echo "  - 或 make web WEB_SRC=/path/to/existing/web"; \
		exit 1; \
	fi; \
	echo "[*] source: $$src"; \
	rm -rf $(DIST_DIR)/web; \
	mkdir -p $(DIST_DIR)/web; \
	cp -a "$$src"/. $(DIST_DIR)/web/; \
	if [ ! -f $(DIST_DIR)/web/qqwry.ipdb ]; then \
		if [ -f "$(WEB_DIR)/public/qqwry.ipdb" ]; then \
			cp -f "$(WEB_DIR)/public/qqwry.ipdb" $(DIST_DIR)/web/qqwry.ipdb; \
			echo "[+] qqwry.ipdb from $(WEB_DIR)/public"; \
		elif [ -f "client/qqwry.ipdb" ]; then \
			cp -f "client/qqwry.ipdb" $(DIST_DIR)/web/qqwry.ipdb; \
			echo "[+] qqwry.ipdb from client/"; \
		else \
			echo "[!] qqwry.ipdb not found (optional)"; \
		fi; \
	fi; \
	echo "[+] web done ($(DIST_DIR)/web)"

### EXTENSION-KIT ###

# profile axscripts 期望 dist 下: Extension-Kit/extension-kit.axs
# 源码查找顺序: EXTKIT_SRC > ./Extension-Kit > AdaptixServer/Extension-Kit
EXTKIT_DST := $(DIST_DIR)/Extension-Kit
EXTKIT_SRC ?=

extension-kit: prepare
	@ echo "[*] Copying Extension-Kit -> $(EXTKIT_DST) ..."
	@ src=""; \
	if [ -n "$(EXTKIT_SRC)" ] && [ -d "$(EXTKIT_SRC)" ]; then \
		src="$(EXTKIT_SRC)"; \
	elif [ -f "Extension-Kit/extension-kit.axs" ]; then \
		src="Extension-Kit"; \
	elif [ -f "AdaptixServer/Extension-Kit/extension-kit.axs" ]; then \
		src="AdaptixServer/Extension-Kit"; \
	else \
		echo "[ERROR] 找不到 Extension-Kit（需含 extension-kit.axs）。请放到仓库根目录 Extension-Kit/"; \
		echo "  或 make extension-kit EXTKIT_SRC=/path/to/Extension-Kit"; \
		exit 1; \
	fi; \
	if [ ! -f "$$src/extension-kit.axs" ]; then \
		echo "[ERROR] $$src/extension-kit.axs 不存在"; \
		exit 1; \
	fi; \
	echo "[*] source: $$src"; \
	rm -rf $(EXTKIT_DST); \
	mkdir -p $(DIST_DIR); \
	if command -v rsync >/dev/null 2>&1; then \
		rsync -a --exclude='.git' --exclude='.git/**' "$$src"/ $(EXTKIT_DST)/; \
	else \
		cp -a "$$src" $(EXTKIT_DST); \
		rm -rf $(EXTKIT_DST)/.git; \
	fi; \
	echo "[+] extension-kit done ($(EXTKIT_DST))"

server-ext: clean server extenders web extension-kit
	@ cp AdaptixServer/extenders/beacon_agent/dist/agent_beacon.so $(DIST_DIR)/extenders/beacon_agent/agent_beacon.so 2>/dev/null || true



### CLIENT ###

client: prepare
	@ echo "[*] Building AdaptixClient..."
	@ cd AdaptixClient && cmake . > /dev/null 2>cmake_error.log || { echo "[ERROR] CMake failed:"; cat cmake_error.log >&2; exit 1; }
	@ cd AdaptixClient && make --no-print-directory
	@ mv ./AdaptixClient/AdaptixClient ./$(DIST_DIR)/
	@ echo "[+] done"

client-fast: prepare
	@ echo "[*] Building AdaptixClient in $(NPROC) threads..."
	@ cd AdaptixClient && cmake . > /dev/null 2>cmake_error.log || { echo "[ERROR] CMake failed:"; cat cmake_error.log >&2; exit 1; }
	@ cd AdaptixClient && make --no-print-directory -j$(NPROC)
	@ mv ./AdaptixClient/AdaptixClient ./$(DIST_DIR)/
	@ echo "[+] done"



### DOCKER BUILD ###

docker-build-server:
	@ echo "[*] Building server via Docker..."
	@ docker compose --profile build-server build
	@ docker compose --profile build-server up --abort-on-container-exit
	@ docker compose --profile build-server down
	@ echo "[+] Server built via Docker"

docker-build-client:
	@ echo "[*] Building client via Docker..."
	@ docker compose --profile build-client build
	@ docker compose --profile build-client up --abort-on-container-exit
	@ docker compose --profile build-client down
	@ echo "[+] Client AppImage built via Docker"

docker-build-extenders:
	@ echo "[*] Building extenders via Docker..."
	@ docker compose --profile build-extenders build
	@ docker compose --profile build-extenders up --abort-on-container-exit
	@ docker compose --profile build-extenders down
	@ echo "[+] Extenders built via Docker"

docker-build-server-ext:
	@ echo "[*] Building server and extenders via Docker..."
	@ docker compose --profile build-server-ext build
	@ docker compose --profile build-server-ext up --abort-on-container-exit
	@ docker compose --profile build-server-ext down
	@ echo "[+] Server and extenders built via Docker"

docker-build-all: docker-build-server-ext
	@ echo "[+] All Docker builds completed"



### DOCKER RUNTIME ###

docker-up:
	@ echo "[*] Starting Adaptix server runtime container..."
	@ docker compose --profile runtime up -d
	@ echo "[+] Runtime container started"
	@ echo "[*] Use 'make docker-logs' to view logs"
	@ echo "[*] Use 'make docker-down' to stop container"

docker-down:
	@ echo "[*] Stopping Adaptix server runtime container..."
	@ docker compose --profile runtime down
	@ echo "[+] Runtime container stopped"

docker-logs:
	@ docker compose --profile runtime logs -f

docker-restart: docker-down docker-up



### HELP ###

help:
	@ echo "AdaptixC2 Build System"
	@ echo ""
	@ echo "Available targets:"
	@ echo "  all              - Build everything (server, client, extenders)"
	@ echo "  extenders        - Build only the extenders"
	@ echo "  kharon-sync      - Copy agent_kharon + listener_kharon_http from KHRON_ROOT (default ../Kharon) into extenders + go work sync"
	@ echo "  server-ext       - Build server + extenders + web + Extension-Kit (no Qt client)"
	@ echo "  server-ext-with-kharon - kharon-sync then server-ext (对齐 setup_kharon.sh 的拷贝步骤)"
	@ echo "  server           - Build only the server"
	@ echo "  web              - Copy Web static assets to dist/web (no npm; WEB_SRC=... optional)"
	@ echo "  extension-kit    - Copy Extension-Kit to dist/ (default ./Extension-Kit)"
	@ echo "  client-fast      - Build only the client in multithread mode (fast build)"
	@ echo "  client           - Build only the client"
	@ echo "  clean            - Remove dist directory"
	@ echo "  clean-all        - Remove all build artifacts"
	@ echo ""
	@ echo "Docker commands:"
	@ echo "  docker-build-server     - Build server via Docker Compose"
	@ echo "  docker-build-client     - Build client AppImage via Docker Compose"
	@ echo "  docker-build-extenders  - Build extenders via Docker Compose"
	@ echo "  docker-build-server-ext - Build server and extenders via Docker Compose"
	@ echo "  docker-build-all        - Build server and extenders via Docker Compose (alias)"
	@ echo "  docker-up               - Start runtime container (detached)"
	@ echo "  docker-down             - Stop runtime container"
	@ echo "  docker-logs             - View runtime container logs (follow mode)"
	@ echo "  docker-restart          - Restart runtime container"
	@ echo "  docker-clean            - Remove Docker containers and images (builders only)"
	@ echo "  docker-clean-all        - Remove all Docker artifacts (containers, images, volumes, networks)"
	@ echo ""
	@ echo "  help             - Show this help message"
	@ echo ""
	@ echo "Platform: $(UNAME_S) [$(NPROC) proc]"

.PHONY: all extenders kharon-sync server-ext server-ext-with-kharon server web extension-kit client clean clean-all docker-build-server docker-build-extenders docker-build-server-ext docker-build-all docker-up docker-down docker-logs docker-restart docker-clean docker-clean-all help prepare
