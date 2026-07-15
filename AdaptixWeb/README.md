# Adaptix Web（服务器部署）

路径：`/root/AdaptixC2/AdaptixWeb`

## 访问

```
http://110.41.87.17:5173
```

登录页 **Teamserver URL 留空**（走 Vite 代理 `/api` → `https://127.0.0.1:8080`）。

账号同 `dist/profile.yaml` operators（示例：admin）。

## 进程

```bash
# 状态
ss -lntp | grep 5173
ps aux | grep vite | grep -v grep
tail -f /tmp/adaptixweb.log

# 重启
fuser -k 5173/tcp 2>/dev/null
cd /root/AdaptixC2/AdaptixWeb
nohup npm run dev > /tmp/adaptixweb.log 2>&1 &
```

> 必须用 `npm run dev`（带 `/api` 代理）。`vite preview` 默认不代理 API。

## MVP 功能

- Sessions 列表 / 过滤 / Tag / Remove
- Console：`command/raw` + WS 输出
- Listeners：列表、创建 BeaconHTTP、Stop
- Generate：beacon Exe/Stager 等，浏览器下载
- Tasks / Downloads / Logs

## 协议

1. `POST /api/login` version=v1.2  
2. `POST /api/otp/generate` type=connect, client_type=2  
3. `WS /api/connect?otp=...`  
4. `POST /api/sync`  
5. REST 变更 + WS 推送  

## 未做（后续）

文件浏览器、交互 Terminal、隧道数据面、AxScript 动态 GenerateUI、多工程。
