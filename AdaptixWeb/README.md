# AdaptixWeb（Qt 桌面风格 Web 控制台）

基于 React 18 + Vite 5 + Zustand 构建，按 `AdaptixClient` 桌面端（Qlementine + KDDockWidgets）风格 1:1 重写，与 `AdaptixServer` 单端口同源托管。

## 本地开发与构建

```bash
cd AdaptixWeb
npm ci

# 1. 连接真实 teamserver 开发（/api 与 WebSocket 代理到 VITE_API_TARGET）
VITE_API_TARGET=https://<teamserver>:8080 npm run dev

# 2. 生产构建（VITE_BASE 需与 profile.yaml 中 Web.path 一致）
VITE_BASE=/bf21a14d8b0eb60f8b95ae0df61162b8/ npm run build   # → dist/

# 3. 离线本地预览（自带 6 台模拟主机与 WS 文件/进程实时推包，任意账密登录）
npm run build && node scripts/preview_server.mjs 5199      # → http://127.0.0.1:5199/
```

## 主要特性

- **10 套 Qt 官方主题 + 10 套 Console 主题**：默认 `Adaptix Dark`，支持 `Adaptix Dracula`、`Adaptix Light`、`Dark Classic`、`Dark Ice`、`Dracula`、`Fallout Pip-Boy`、`Glass Morphism`、`Hacker Tech`、`Light Arc` 即时切换，静态资源自动适配 `VITE_BASE`。
- **KDDockWidgets 双区布局**：顶部 `QMenuBar` + 分组图文工具栏 + `Extensions Docks` 弹出面板 + 上下可拖拽双区 Dock + 底部项目状态栏 + 4 栏 `Adaptix Settings` 设置弹窗。
- **完整桌面级核心视图**：
  - `DialogConnect` 双栏连接弹窗（历史连接档案管理 + JSON 导入导出）
  - `Sessions table`（17 列可配、排序、真实 OS 图标、`Ctrl+F` 折叠搜索）与 `Sessions graph` 拓扑图
  - `Console` & `Logs`（官方背景图与语法高亮、命令补全、历史、`Ctrl+F` 查找）
  - `Tasks` 左表右 `Task Output` 分屏视图
  - **文件管理**：左侧目录树 + 右侧文件表（Windows/Linux 原生列、拖拽上传、多编码文本预览、切页保活）
  - **进程管理**：左侧 3 列 `Process Tree` 与右侧进程表双向联动、自身会话 PID 红字防误杀、`SYSTEM/root` 高亮、切页保活
  - Listeners / Downloads / Tunnels / Credentials / Targets / Screenshots / Chat / Generate / WebDelivery / Relay / AI Collab / BOFs / ArgFuscator / Extension-Kit / PTY / VNC

## 协议流程

1. `POST /api/login` → 获取 Bearer `access_token` / `refresh_token`
2. `POST /api/otp/generate`（`type=connect`, `client_type=2`）
3. `WS /api/connect?otp=...`
4. `POST /api/sync` → 精简实时订阅，毫秒级进入实时心跳与事件流
5. REST 变更 + WebSocket 实时推包（含 `BROWSER_DISKS / BROWSER_FILES / BROWSER_PROCESS`）
