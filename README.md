# AdaptixC2 v1.4

## 分支说明
| 分支 | 说明 |
|------|------|
| `main` | 最新代码（v1.4） |
| `1.3` | v1.3 原始备份 |

## v1.4 更新内容
- **WS Beacon**：修复 WinHTTP 剥离 `Sec-WebSocket-Protocol` header 问题，改用 `X-Beat` 自定义头，三层 fallback
- **默认签名**：Exe/Service Exe 格式默认开启 Authenticode 伪签名，消除 SmartScreen 弹窗
- **Linux WS Agent**：支持 Linux WebSocket beacon
- **构建修复**：修复 `Linker.ld` 缺失导致 `agent builder failed`
- **错误日志**：构建失败时显示具体错误原因
- **对抗模块**：补齐 AntiSandbox / EntropyEvasion / SandboxDetect / JccObfuscate / ApiSequenceObfuscate
- **pe_header_mod**：PE 头混淆脚本
- **调试清理**：移除冗余调试日志

### 近期产品化（Web / 构建）
- **集成 Web 控制台**：`./adaptixserver -profile profile.yaml -web`，静态页在 `dist/web`，入口路径见 profile `Web.path`（产品示例 `/cnmbzxc`）
- **`make server-ext` 产物**：服务端 + extenders + **Web 静态拷贝** + **Extension-Kit 拷贝**（不编 Qt 客户端）
- **Web**：不在 Makefile 里跑 `npm`；只把已有静态产物拷到 `dist/web`（见下方「构建说明」）
- **Extension-Kit**：仓库根目录 `Extension-Kit/`（含 `extension-kit.axs`），`make extension-kit` / `server-ext` 自动拷到 `dist/Extension-Kit/`
- **beacon_agent**：去掉强制 `cp artifacts`（目录常不存在会导致 make 失败）；Qt 载荷格式去掉 Artifact 三项，与 Web Generate 对齐
- **载荷 / 监听 Web UI**：平台卡片、Stager/Exe 等格式、KharonHTTP / BeaconDiscord 等表单产品化

## 构建说明

### Server（推荐 VPS）

```bash
git clone https://github.com/Xaaaa-bip/AdaptixC2
cd AdaptixC2
./pre_install_linux_all.sh server
make server-ext
```

`make server-ext` 会：

1. `clean` 后编译 `adaptixserver` → `dist/`
2. 编译全部默认 extenders → `dist/extenders/`
3. **拷贝 Web 静态** → `dist/web/`（不执行 npm）
4. **拷贝 Extension-Kit** → `dist/Extension-Kit/`

#### Web 静态从哪来（只 copy）

按优先级：

1. `make web WEB_SRC=/path/to/web`（目录内需有 `index.html`）
2. 否则 `AdaptixWeb/dist/`（需事先在本机/CI 用 Vite 编好）
3. 否则 `AdaptixWeb/` 本身已是带 `assets/` 的静态树

单独只更 Web：

```bash
make web
# 或
make web WEB_SRC=/root/some/existing/web
```

前端源码改完后，在有 Node 的环境先生成产物再 copy，例如：

```bash
cd AdaptixWeb
# base 必须与 profile Web.path 一致（注意尾斜杠）
npm run build -- --base=/cnmbzxc/
cd ..
make web
```

#### Extension-Kit

- 源码位置（查找顺序）：`EXTKIT_SRC` → **`./Extension-Kit`** → `AdaptixServer/Extension-Kit` → `client/Extension-Kit`
- 需含入口 `extension-kit.axs`
- profile 中默认：`axscripts: - "Extension-Kit/extension-kit.axs"`（相对 **dist 工作目录**）

```bash
make extension-kit
# 或
make extension-kit EXTKIT_SRC=/path/to/Extension-Kit
```

#### 启动（必须在 dist 工作目录）

```bash
cd dist
# 按需：./ssl_gen.sh 或配置 TLS；证书 server.rsa.crt/key 不会由 make 生成
# 纯 API：
./adaptixserver -profile profile.yaml
# 带集成 Web：
./adaptixserver -profile profile.yaml -web
```

说明：

- `make` **不会**生成 TLS 证书、**不会**创建 `data/` 数据库；首次运行或拷贝现网证书/数据
- `clean` 会删整个 `dist/`，之后需重新 `server-ext`，并自行恢复证书 / `data` / 现网改过的 `profile.yaml`

### Client

```bash
./pre_install_linux_all.sh client
# 见下文「本地编译方法」Qt 客户端
```

## 相关插件
- https://github.com/danieloz147/SpyIt vnc
- https://github.com/vikialora/PersisTask-BOF-AdaptixC2 计划任务
- https://github.com/DarksBlackSk/Keylogger-BOF 键盘监听


## 项目说明

Adaptix 是一个用于授权安全测试与红队演练的 C2 框架。

- 服务端：Golang
- 客户端：C++ / Qt
- Web 控制台：`AdaptixWeb`（可选，`-web` 挂载）
- 运行平台：Linux / Windows / macOS

官方文档入口：

- 安装与使用文档：https://adaptix-framework.gitbook.io/adaptix-framework
- v1.2 更新说明：https://adaptix-framework.gitbook.io/adaptix-framework/changelog-and-updates/v1.1-greater-than-v1.2

## 法律声明

本项目仅限在取得明确授权的安全测试、攻防演练和研究环境中使用。
任何未授权使用都可能违反当地法律法规，使用者需自行承担风险。

## 目录结构

- `AdaptixServer/`：服务端源码与默认配置
- `AdaptixClient/`：Qt 客户端源码
- `AdaptixWeb/`：Web 控制台源码（构建产物可放 `AdaptixWeb/dist` 供 `make web` 拷贝）
- `Extension-Kit/`：BOF / axscript 扩展（`make server-ext` 拷到 `dist/Extension-Kit`）
- `.github/workflows/build.yml`：GitHub Actions 三平台构建流程
- `scripts/`：辅助脚本
- `Makefile`：`server` / `extenders` / `web` / `extension-kit` / `server-ext` / `client`

## GitHub Actions 一键构建

仓库已经提供了三平台整包构建流程，工作流文件为：

- `.github/workflows/build.yml`

触发方式：

1. 推送到 `main` 或 `master`
2. 提交 Pull Request 到 `main` 或 `master`
3. 在 GitHub Actions 页面手动点击 `workflow_dispatch`

一次工作流会生成以下三类整包产物：

- `Adaptix-linux-amd64`
- `Adaptix-windows-amd64`
- `Adaptix-macos-arm64`

每个平台整包默认包含：

- 服务端可执行文件
- 客户端可执行文件
- README
- 服务端运行所需的默认配置文件

额外说明：

- Linux 和 macOS 整包会附带默认启用的原生 extenders `dist/` 目录
- Windows 整包中的 `profile.yaml` 会自动禁用 extenders，避免因 Go plugin 平台差异导致启动时报错

## 默认启用的 Extenders

- Beacon HTTP / SMB / TCP / DNS / WS Listener
- Beacon Agent
- Gopher TCP Listener / Gopher Agent
- Linux TCP Listener / Linux Agent
- macOS Agent
- Discord Listener
- Kharon HTTP Listener（默认未启用）
- VNC 远程桌面

说明：

- `agent_kharon` 默认未启用
- `listener_kharon_http` 默认未启用
- 原因是这两个模块在默认仓库布局下更容易出现配置文件或额外依赖问题，不适合作为“开箱即用”的默认项

## 本地编译环境要求

### 服务端

- Go `1.25.4`
- 需要启用 `CGO`
- Linux 需要 `build-essential pkg-config libsqlite3-dev`
- macOS 需要 `pkg-config sqlite`
- Windows 建议使用 GitHub Actions 或 `MSYS2 + MinGW`

### 客户端

- CMake `>= 3.28`
- Ninja
- OpenSSL 3
- Qt `>= 6.8.0`

客户端 Qt 组件至少需要：

- `qtbase`
- `qtdeclarative`
- `qtsvg`
- `qtwebsockets`

## 本地编译方法

### 1. 编译服务端

Linux / macOS：

```bash
cd AdaptixServer
CGO_ENABLED=1 go build -ldflags="-s -w" -o AdaptixServer .
```

Windows（建议在 GitHub Actions 中构建；如果本地构建，需先准备 MSYS2 / MinGW 环境）：

```powershell
cd AdaptixServer
$env:Path = "C:\msys64\mingw64\bin;C:\msys64\usr\bin;" + $env:Path
$env:CGO_ENABLED = "1"
go build -ldflags="-s -w" -o AdaptixServer.exe .
```

### 2. 编译客户端

#### Linux

```bash
sudo apt-get update
sudo apt-get install -y \
  cmake ninja-build build-essential pkg-config libssl-dev \
  libxkbcommon-dev libgl1-mesa-dev \
  libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 \
  libxcb-render-util0 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 \
  libxkbcommon-x11-0
```

安装 Qt 之后执行：

```bash
cmake -S AdaptixClient -B build-client -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build-client --parallel
```

#### macOS

推荐依赖安装方式：

```bash
brew install cmake ninja openssl@3 qtbase qtdeclarative qtsvg qtwebsockets
```

构建命令：

```bash
cmake -S AdaptixClient -B build-client -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/opt/homebrew \
  -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
cmake --build build-client --parallel
```

#### Windows

推荐使用：

- Qt 6.8.x MSVC 2022
- OpenSSL 3
- Ninja
- Visual Studio 2022 Build Tools

构建命令：

```powershell
cmake -S AdaptixClient -B build -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DADAPTIX_USE_SYSTEM_QT6=ON `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.3\msvc2022_64" `
  -DOPENSSL_ROOT_DIR="C:\Program Files\OpenSSL-Win64"
cmake --build build --parallel
```

## 服务端运行方法

在服务端目录下执行：

```bash
cd AdaptixServer
./AdaptixServer
```

首次启动时如果证书不存在，会自动生成：

- `server.rsa.crt`
- `server.rsa.key`

默认配置文件：

- `AdaptixServer/profile.yaml`

当前示例配置中的关键项：

- 监听地址：`0.0.0.0`
- 监听端口：`8843`
- 登录 endpoint：`/api/login`

因此默认登录地址为：

```text
https://127.0.0.1:8843/api/login
```

## 客户端登录方法

客户端编译完成后，启动可执行文件：

- Linux / macOS：`AdaptixClient`
- Windows：`AdaptixClient.exe`

默认示例账号来自 `AdaptixServer/profile.yaml`：

- 用户名：`admin`
- 密码：`cnmd@qax666`

补充说明：

- 当前配置中 `only_password: false`
- 这意味着不能随便填用户名
- 必须使用 `operators` 中存在的用户，例如 `admin`

首次使用后请立刻修改默认密码。

## 编译产物位置

### 推荐：`make server-ext`（仓库根目录）

| 路径 | 说明 |
|------|------|
| `dist/adaptixserver` | 服务端二进制（支持 `-web`） |
| `dist/extenders/` | 插件 so / objects / 配置 |
| `dist/web/` | Web 静态（若源存在） |
| `dist/Extension-Kit/` | BOF 扩展（若源存在） |
| `dist/profile.yaml` | 默认配置模板（启动前可按环境改 `Web.path` 等） |

### 本地手工编译

- 服务端：`AdaptixServer/AdaptixServer` 或根目录 `make server` → `dist/adaptixserver`
- 客户端：`build-client/AdaptixClient` 或 `AdaptixClient/build/AdaptixClient`

### GitHub Actions 产物

下载对应 artifact 后，目录中会包含：

- `AdaptixServer/`
- `AdaptixClient/`
- `README.md`

其中：

- Linux / macOS 包可直接配合默认 `profile.yaml` 运行
- Windows 包默认已禁用 extenders，适合只验证服务端基础启动与客户端连接

## 常见问题

### 1. `Invalid profile`

通常是以下原因：

- `profile.yaml` 中引用的 extender 配置文件不存在
- `404page.html` 路径错误
- TLS 证书路径错误

本仓库默认 profile 已改为使用：

- `extenders/*/dist/config.yaml`

因此运行前应确保这些 `dist/` 目录已经存在。

### 2. `failed to open plugin ... different version of package internal/goarch`

这表示服务端和 Go plugin 不是用完全一致的编译条件生成的。

处理原则：

- 服务端和 extenders 必须使用同一个 Go 版本
- 服务端和 extenders 必须使用一致的 `GOOS / GOARCH / CGO_ENABLED`
- 如果服务端要加载本地编译出的 extenders，不要让服务端和插件使用不同的构建参数
- 在本项目中，服务端若要与本地原生 extenders 配套，建议不要对服务端单独加 `-trimpath`

最稳妥的方式：

- 直接使用 GitHub Actions 打好的整包产物

### 3. `Qt6Config.cmake` 找不到

说明 Qt 开发环境没有正确安装，或 `CMAKE_PREFIX_PATH` 没有指向 Qt 根目录。

### 4. `login failure`

优先检查：

- 服务端是否真的在运行
- 客户端填写的地址是否是 `https://127.0.0.1:8843`
- 用户名是否为 `admin`
- 密码是否与 `profile.yaml` 中 `operators.admin` 一致
- `only_password` 是否为 `false`

### 5. `bind: address already in use`

说明端口被占用。

处理方法：

- 修改 `AdaptixServer/profile.yaml` 中的 `Teamserver.port`
- 或先停止旧的 `AdaptixServer` 进程

## 提交说明

如需向上游贡献修改，请优先提交到：

- `dev`

否则上游可能不会直接接收你的变更。
