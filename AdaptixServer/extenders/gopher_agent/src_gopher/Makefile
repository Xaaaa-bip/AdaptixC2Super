all: agent

agent:
	@ GOWORK=off CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -modfile=go.mod -trimpath -ldflags="-s -w" -o agent && rm agent
	@ GOWORK=off CGO_ENABLED=0 GOOS=linux GOARCH=arm64 go build -modfile=go.mod -trimpath -ldflags="-s -w" -o agent && rm agent
	@ GOWORK=off CGO_ENABLED=0 GOOS=darwin GOARCH=amd64 go build -modfile=go.mod -trimpath -ldflags="-s -w" -o agent && rm agent
	@ GOWORK=off CGO_ENABLED=0 GOOS=darwin GOARCH=arm64 go build -modfile=go.mod -trimpath -ldflags="-s -w" -o agent && rm agent
	@ GOWORK=off CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -modfile=go.mod -trimpath -ldflags="-s -w" -o agent && rm agent
	@ GOWORK=off CGO_ENABLED=0 GOOS=windows GOARCH=arm64 go build -modfile=go.mod -trimpath -ldflags="-s -w" -o agent && rm agent
# ★ B-B1：带 `-tags libp2p` 的产物。默认的 agent 目标**不含** libp2p（那是 §4.2 的
#   决策，/agent/generate 时由 pl_main.go 按 profile 自动加 tag）；这个目标留给
#   "不经 builder 的独立构建/复现"。两个变体产物里的 transport 指纹互斥，构建后
#   由 pl_main.go 的 axVerifyTransportTag 按指纹断言。
libp2p:
	@ GOWORK=off CGO_ENABLED=0 GOOS=linux GOARCH=amd64 go build -tags libp2p -modfile=go.mod -trimpath -ldflags="-s -w" -o agent-libp2p && rm agent-libp2p
	@ GOWORK=off CGO_ENABLED=0 GOOS=windows GOARCH=amd64 go build -tags libp2p -modfile=go.mod -trimpath -ldflags="-s -w" -o agent-libp2p.exe && rm agent-libp2p.exe

.PHONY: agent libp2p
