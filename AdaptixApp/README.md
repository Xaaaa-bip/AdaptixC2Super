# AdaptixApp — Android 控制端

[English](README_EN.md)

手机上的 AdaptixC2 团队端客户端。连接**与 Web 控制台完全同一套**服务端接口：同一地址、同一组操作员账号，网页控制台能连上的服务器，App 填同样信息就能连上。

## 连接方式（与 Web 控制台一致）

前提：一台已经跑起来的 AdaptixServer（即你现在用浏览器访问控制台的那台）。服务端部署见[仓库根 README](../README.md)。

| | Web 控制台（浏览器） | AdaptixApp |
|---|---|---|
| 地址 | `https://<ip>:<port>/<入口路径>/`（入口路径 = `profile.yaml` 的 `Web.path`） | **只填 `https://<ip>:<port>`，不要带入口路径** |
| 账号 | 操作员账号密码 | 同一组账号密码 |
| 证书 | 浏览器要点"继续访问"（自签） | 自签证书直接可用，无需处理 |

- `http://` / `https://` 均可；不带 scheme 时默认按 `https://` 处理。
- 连接流程（App 自动完成，仅供排查参考）：
  `POST /api/login` 拿 Bearer token → `POST /api/otp/generate` 生成一次性票据 → `WS wss://<ip>:<port>/api/connect?otp=…` 建立实时信道。

## 功能

- **连接配置管理**：多套服务器配置本地保存，点击历史配置一键回填。
- **仪表盘**：Agents / Listeners / Targets / 团队聊天 / 控制台 五个页签。
  - Agents：在线 agent 列表与状态。
  - Listeners：监听器列表。
  - Targets：目标列表。
  - 团队聊天：与 Web 控制台互通的团队消息。
  - 控制台：选中 Agent 直接下发命令，查看历史任务与回显。
- **实时推送**：WebSocket 订阅 targets / agents / tasks / chat 等事件，断线自动重连。
- **通知**：新消息/任务提醒（Android 13+ 首次使用需授予通知权限）。

## 获取与安装

CI（push 到 main / 打 tag）会自动编译并把 APK 传到 GitHub Release：

| 资产 | 说明 |
|---|---|
| `AdaptixApp-debug.apk` | 直接安装即可 |
| `AdaptixApp-release-unsigned.apk` | **未签名，装不了**，需先用自己的 key 签： |

```bash
# 没有 keystore 先生成一个
keytool -genkey -v -keystore myrelease.key -alias adaptix -keyalg RSA -keysize 2048 -validity 10000

# 对齐 + 签名（zipalign / apksigner 在 Android SDK build-tools 里）
zipalign -f -p 4 AdaptixApp-release-unsigned.apk aligned.apk
apksigner sign --ks myrelease.key --out AdaptixApp-release.apk aligned.apk
```

系统要求：Android 7.0+（minSdk 24）。

## 本地编译

- **Android Studio**：直接打开 `AdaptixApp/` 目录，选设备 Run。
- **命令行**：

```bash
cd AdaptixApp
./gradlew assembleDebug        # 产物: app/build/outputs/apk/debug/
./gradlew assembleRelease      # 产物: app/build/outputs/apk/release/（未签名）
```

环境要求：JDK（daemon 工具链锁 25，本机没有时 Gradle 会经 foojay 自动下载）、Android SDK（compileSdk 37，缺失组件 AGP 会自动补装）。Gradle 9.6 由 wrapper 自带，`local.properties` 由 IDE 自动生成、不入库。

## 安全须知

- **客户端信任所有 TLS 证书**（自签服务器友好）。这意味着在不可控网络里可能被中间人——只在可信网络使用，能走 `https://` 就不要走 `http://`。
- **连接配置（含密码）明文保存在本机**（SharedPreferences）。手机勿 root、勿外借；换机/退役时卸载应用清除。
- App 与服务器之间的所有流量（登录、命令下发、任务回显、聊天）都走上述同一条信道，服务端侧无需为 App 做任何额外配置或开洞。
