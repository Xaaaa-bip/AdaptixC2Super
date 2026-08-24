# AdaptixC2 v1.5.5

## 分支说明
| 分支 | 说明 |
|------|------|
| `main` | 最新代码（**v1.5.5**） |
| `1.3` | v1.3 原始备份 |

## v1.5.5 更新内容

相对 v1.5，本版修复 **Web 缺资源白屏** 与 **GopherTCP callback 地址** 兼容问题（已在现网验证），并 **移除 Qt 桌面客户端**，仓库以服务端 + Web 控制台为主。

### 移除 Qt 客户端
- 删除仓库内 `AdaptixClient/` 源码与 `make client` / `make client-fast` 目标
- GitHub Actions 不再构建或上传 Qt 客户端产物
- 服务端 / extenders 构建成功后自动发布 zip 到 GitHub Releases（`v*` 正式版，`main` 更新 `latest` 预发布）
- `pre_install_linux_all.sh` 仅安装服务端依赖；操作员界面改用集成 Web 控制台（`./adaptixserver -profile profile.yaml -web`）

### Web UI 缺资源不再白屏（teamserver）
- `core/connector/webui.go`：启动时 `inspectWebRoot` 检查 `index.html` 与其引用的 `.js`/`.css`，以及 `assets/` 是否非空
- 静态不完整时：**不硬失败退出**，进入 **placeholder** 诊断页（说明原因与 `make web` 修复步骤），API 仍可用
- 缺失的 `.js`/`.css` 等静态资源 **直接 404**，禁止 SPA 回退到 `index.html`（避免 Vite 把 HTML 当 JS 导致白屏）
- 日志带 `mode=static|placeholder`

### GopherTCP `callback_addresses` 兼容
- `gopher_listener_tcp`：新增 `StringOrArray`，同时接受 JSON **string**（Qt 多行/profile）与 **[]string**（Web 表单）
- 归一化为逗号/换行列表字符串，供 Agent profile 使用
- 校验支持换行与逗号分隔
- Web `Listeners.tsx`：GopherTCP 提交时将 callback 列表 **join 为字符串**，与 Qt/agent 一致

### 涉及文件
- `AdaptixServer/core/connector/webui.go`
- `AdaptixServer/extenders/gopher_listener_tcp/pl_main.go`
- `AdaptixServer/extenders/gopher_listener_tcp/pl_transport.go`
- `AdaptixWeb/src/components/Listeners.tsx`

---

## v1.5 更新内容

相对 v1.4，本版侧重 **Web 产品化**、**一键 VPS 构建** 与 **Extension-Kit 布局收敛**。

### 集成 Web 控制台（teamserver）
- 启动开关：`./adaptixserver -profile profile.yaml **-web**`（不加 `-web` 则只有 API，无页面）
- 静态目录：`dist/web`（profile `Web.root`，默认 `web`）
- 浏览器入口：`https://host:{Web.port}{Web.path}`（产品示例 path=`/cnmbzxc`；根路径 `/` 可不暴露控制台）
- 同源 API：`Web.api_base` 可空，走 Teamserver endpoint；登录可读 `{path}/config.json`
- 相关实现：`main.go` CLI、`core/connector/webui.go`、profile `Web` 段

### Web 前端（AdaptixWeb）产品能力
- **载荷 Generate**：平台卡片（Windows / Linux / macOS / Gopher）；Windows 合并为单卡，格式区选 Stager / Exe / Service Exe / DLL / Shellcode / Hex C 等；含 Kharon
- **监听 Listeners**：BeaconHTTP/WS/TCP/SMB/DNS、**BeaconDiscord**、LinuxTCP、GopherTCP、**KharonHTTP** 等专用表单
- **投递 WebDelivery**、登录与 `vite` base（`VITE_BASE` / `Web.path`）对齐产品路径
- Qt `ax_config` 去掉 Artifact 三项，与 Web 载荷格式对齐

### 构建系统（Makefile）
- **`make server-ext`**（推荐 VPS）：`clean` → 编 server → 编 extenders → **拷贝 Web** → **拷贝 Extension-Kit**
- **`make web`**：只 **copy** 静态产物到 `dist/web`，**不**在 Makefile 里执行 `npm install` / `npm run build`
  - 源优先级：`WEB_SRC=...` → `AdaptixWeb/dist/` → 已是静态树的 `AdaptixWeb/`
- **`make extension-kit`**：拷贝到 `dist/Extension-Kit/`
  - 源优先级：`EXTKIT_SRC` → **`./Extension-Kit`** → `AdaptixServer/Extension-Kit`
- **beacon_agent Makefile**：删除强制 `cp artifacts`（无该目录时会导致 `server-ext` 失败）

### Extension-Kit 布局
- **唯一维护路径**：仓库根目录 **`Extension-Kit/`**（含 `extension-kit.axs`）
- **删除** 重复树 `client/Extension-Kit/`（避免双份不同步）
- profile 默认：`axscripts: - "Extension-Kit/extension-kit.axs"`（相对 **dist 工作目录**）
- 启动须 `cd dist`；kit 与 so 均在 dist 侧

### 运维注意（v1.5 构建产物）
- `make` **不生成** TLS 证书（`server.rsa.crt` / `key`）、**不创建** `data/` 库；需首次生成或从现网拷贝
- `clean` 会清空整个 `dist/`，证书 / 数据 / 现网改过的 `profile.yaml` 需自行备份恢复
- 必须在 **`dist` cwd** 启动 adaptixserver，否则路径与 getwd 易出错

### 认证与协作（延续并文档化）
- 支持 profile `only_password` 等协作登录方式（以当前 `profile.yaml` 为准）
- 多端 WebSocket / 聊天等以服务端实现为准

---

## v1.4 更新内容（历史）
- **WS Beacon**：修复 WinHTTP 剥离 `Sec-WebSocket-Protocol` header 问题，改用 `X-Beat` 自定义头，三层 fallback
- **默认签名**：Exe/Service Exe 格式默认开启 Authenticode 伪签名，消除 SmartScreen 弹窗
- **Linux WS Agent**：支持 Linux WebSocket beacon
- **构建修复**：修复 `Linker.ld` 缺失导致 `agent builder failed`
- **错误日志**：构建失败时显示具体错误原因
- **对抗模块**：补齐 AntiSandbox / EntropyEvasion / SandboxDetect / JccObfuscate / ApiSequenceObfuscate
- **pe_header_mod**：PE 头混淆脚本
- **调试清理**：移除冗余调试日志

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

- 源码位置（查找顺序）：`EXTKIT_SRC` → **`./Extension-Kit`** → `AdaptixServer/Extension-Kit`（仓库仅维护根目录 `Extension-Kit/`）
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

## 相关插件
- https://github.com/danieloz147/SpyIt vnc
- https://github.com/vikialora/PersisTask-BOF-AdaptixC2 计划任务
- https://github.com/DarksBlackSk/Keylogger-BOF 键盘监听


## 项目说明

Adaptix 是一个用于授权安全测试与红队演练的 C2 框架。

- 服务端：Golang
- 操作员界面：`AdaptixWeb`（`-web` 挂载到 teamserver）
- 运行平台：Linux / Windows / macOS

官方文档入口：

- 安装与使用文档：https://adaptix-framework.gitbook.io/adaptix-framework
- v1.2 更新说明：https://adaptix-framework.gitbook.io/adaptix-framework/changelog-and-updates/v1.1-greater-than-v1.2

## 法律声明

本项目仅限在取得明确授权的安全测试、攻防演练和研究环境中使用。
任何未授权使用都可能违反当地法律法规，使用者需自行承担风险。

## 目录结构

- `AdaptixServer/`：服务端源码与默认配置
- `AdaptixWeb/`：Web 控制台源码（构建产物可放 `AdaptixWeb/dist` 供 `make web` 拷贝）
- `Extension-Kit/`：BOF / axscript 扩展（`make server-ext` 拷到 `dist/Extension-Kit`）
- `.github/workflows/build.yml`：GitHub Actions 服务端与 extenders 构建，成功后自动发布到 Releases
- `scripts/`：辅助脚本
- `Makefile`：`server` / `extenders` / `web` / `extension-kit` / `server-ext`

## GitHub Actions 一键构建

仓库已经提供了服务端与 extenders 构建流程，工作流文件为：

- `.github/workflows/build.yml`

触发方式：

1. 推送到 `main` 或 `master`
2. 推送 `v*` 标签（例如 `v1.5.6`）
3. 提交 Pull Request 到 `main` 或 `master`（只构建，不发 Release）
4. 在 GitHub Actions 页面手动点击 `workflow_dispatch`

一次工作流会生成：

- `AdaptixServer-linux-amd64`
- `AdaptixServer-windows-amd64`
- `AdaptixServer-macos`
- `extenders-dist`（Linux 原生插件与 beacon / gopher 产物）

构建成功后会自动把上述产物打成 zip 发到 [Releases](https://github.com/Xaaaa-bip/AdaptixC2/releases)：

- 推送 `v*` 标签 → 创建对应版本的正式 Release
- 推送到 `main` / `master` → 更新名为 `latest` 的预发布（滚动覆盖）

操作员界面使用集成 Web 控制台，不再构建 Qt 桌面客户端。

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

带 Web 控制台时，用 `./adaptixserver -profile profile.yaml -web` 启动，浏览器打开 `https://host:{Web.port}{Web.path}`。

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

### GitHub Actions 产物

下载对应 artifact 后，目录中会包含：

- `AdaptixServer` / `AdaptixServer.exe`
- `extenders-dist`（Linux 构建）

Linux / macOS 产物可配合默认 `profile.yaml` 运行。Windows 包适合验证服务端基础启动；Go plugin 在 Windows 上不可用，需在 Linux 上加载 extenders。

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

- 直接使用 GitHub Actions 打好的服务端与 extenders 产物

### 3. `login failure`

优先检查：

- 服务端是否真的在运行
- 浏览器或 API 地址是否是 `https://127.0.0.1:8843`（Web 入口另加 `Web.path`）
- 用户名是否为 `admin`
- 密码是否与 `profile.yaml` 中 `operators.admin` 一致
- `only_password` 是否为 `false`

### 4. `bind: address already in use`

说明端口被占用。

处理方法：

- 修改 `AdaptixServer/profile.yaml` 中的 `Teamserver.port`
- 或先停止旧的 `AdaptixServer` 进程

## 提交说明

如需向上游贡献修改，请优先提交到：

- `dev`

否则上游可能不会直接接收你的变更。
