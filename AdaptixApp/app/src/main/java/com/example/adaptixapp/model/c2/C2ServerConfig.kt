package com.example.adaptixapp.model.c2

data class C2ServerConfig(
    val serverUrl: String = "http://127.0.0.1:8080",
    val username: String = "admin",
    val password: String = "",
    val profileName: String = "默认团队端"
) {
    /**
     * 获取规范化的 HTTP REST API Base URL (带 /api 前缀)
     * 例: "https://110.41.87.17:8080" -> "https://110.41.87.17:8080/api"
     * 例: "https://110.41.87.17:8080/api" -> "https://110.41.87.17:8080/api"
     */
    val httpApiBaseUrl: String
        get() {
            var url = serverUrl.trim()
            if (!url.startsWith("http://", ignoreCase = true) && !url.startsWith("https://", ignoreCase = true)) {
                url = "https://$url"
            }
            url = url.trimEnd('/')
            return if (url.endsWith("/api", ignoreCase = true)) {
                url
            } else {
                "$url/api"
            }
        }

    /**
     * 获取规范化的 WebSocket 实时连接 Endpoint URL
     * 例: "https://110.41.87.17:8080/api" -> "wss://110.41.87.17:8080/api/connect"
     */
    val wsConnectUrl: String
        get() {
            val apiBase = httpApiBaseUrl
            val wsScheme = if (apiBase.startsWith("https://", ignoreCase = true)) {
                apiBase.replaceFirst("https://", "wss://", ignoreCase = true)
            } else {
                apiBase.replaceFirst("http://", "ws://", ignoreCase = true)
            }
            return "$wsScheme/connect"
        }
}
