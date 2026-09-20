# =============================================================
# AdaptixC2 1.5.5（二开版）初始运行配置
# 启动：./adaptixserver -profile profile.initial.yaml [-debug] [-web]
# 字段定义严格对应 AdaptixServer/core/profile/utils.go 结构体
# =============================================================

Teamserver:
  interface: "0.0.0.0"            # 0.0.0.0=直接对外；建议 127.0.0.1 + 反向代理
  port: 
  endpoint: "/api"                # 登录 URL = https://host:port/api/login
  password: ""
  only_password: true            # false = 走 operators 逐人账号
  #operators:
    #admin: "REPLACE_ADMIN_PASS_2026!"
    #operator1: "REPLACE_OP1_PASS_2026!"
  # manage_password: "REPLACE_MANAGE_PASS!"   # 可选：保护 AX 脚本安装/卸载接口
  cert: "server.rsa.crt"
  key: "server.rsa.key"
  tls_autogen: true               # 证书缺失时自动生成自签名 PEM（在校验之前执行）
  # tls_autogen_common_name: "cdn-gateway.your-domain.net"
  # tls_autogen_organization: "Your Company Ltd"
  # tls_autogen_dns_names: ["localhost"]
  # tls_autogen_valid_days: 397
  # tls_autogen_force: false
  extenders:
    - "extenders/beacon_listener_http/config.yaml"
    - "extenders/beacon_listener_smb/config.yaml"
    - "extenders/beacon_listener_tcp/config.yaml"
    - "extenders/beacon_listener_ws/config.yaml"
    - "extenders/beacon_listener_https_emu/config.yaml"
    - "extenders/beacon_listener_dns/config.yaml"
    - "extenders/beacon_listener_discord/config.yaml"
    - "extenders/beacon_agent/config.yaml"
    - "extenders/listener_kharon_http/config.yaml"
    - "extenders/agent_kharon/config.yaml"
    - "extenders/gopher_listener_tcp/config.yaml"
    - "extenders/gopher_agent/config.yaml"
    - "extenders/linux_listener_tcp/config.yaml"
    - "extenders/linux_agent/config.yaml"
    - "extenders/macos_agent/config.yaml"
    - "extenders/hosting_service/config.yaml"
    - "extenders/phishing_service/config.yaml"
    - "extenders/webdelivery/config.yaml"
    - "extenders/notifier/config.yaml"
    - "extenders/vnc/config.yaml"
    - "extenders/argfuscator/config.yaml"
  # 如需 Extension-Kit：路径按 **进程 cwd** 解析（filepath.Abs），
  # 故用绝对路径最稳；此处 dist/ 为运行目录，Extension-Kit/ 与二进制同级。
    # 二开新增：拓扑图服务端持久化（ExtenderData KV + WS 广播，零核心 Go 改动）。
    - "extenders/topology/config.yaml"
    # 二开新增：libp2p 中转控制面板（systemd unit 的遥控器，插件自己不拉进程）。
    # ★ 同 topology：必须**两份 profile 都加**，只加一份对在跑的实例完全无效。
    - "extenders/relay/config.yaml"
  axscripts:
    - "/home/ad/dist/Extension-Kit/extension-kit.axs"
  access_token_live_hours: 12
  refresh_token_live_hours: 168

HttpServer:
  # default_response_headers:
  #   Server: "nginx"
  rate_limit:
    # 仅 POST /login、/refresh，按源 IP 滚动 1 分钟。二开：默认 0（关闭）。
    login_requests_per_minute: 0
  error:
    status: 404
    headers:
      Content-Type: "text/html; charset=UTF-8"
    page: "404page.html"
  http:
    max_header_bytes: 8192
    read_header_timeout_sec: 30
    read_timeout_sec: 0
    write_timeout_sec: 0
    idle_timeout_sec: 120
    request_timeout_sec: 300
    request_timeout_message: "504 Gateway Timeout"
    disable_keep_alives: false
    enable_http2: true
    # trusted_proxies:
    #   - "127.0.0.1"
  tls:
    min_version: "TLS1.2"
    max_version: "TLS1.3"
    prefer_server_cipher_suites: false
    cipher_suites:
      - "TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256"
      - "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384"
      - "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256"
      - "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384"
    client_auth: "none"           # none | request | require
    # client_ca_file: "client-ca-chain.crt"

Web:                              # 仅在 ./adaptixserver -web 时生效
  interface: "0.0.0.0"
  port: 38212                      # 与 Teamserver.port 相同 = 挂同一 TLS 监听
  path: "/"
  root: "web"
  spa: true
  api_base: ""