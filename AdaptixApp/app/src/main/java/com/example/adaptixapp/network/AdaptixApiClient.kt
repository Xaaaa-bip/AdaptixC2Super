package com.example.adaptixapp.network

import com.example.adaptixapp.model.c2.C2Agent
import com.example.adaptixapp.model.c2.C2ChatMessage
import com.example.adaptixapp.model.c2.C2Credential
import com.example.adaptixapp.model.c2.C2Listener
import com.example.adaptixapp.model.c2.C2ServerConfig
import com.example.adaptixapp.model.c2.C2Target
import com.example.adaptixapp.model.c2.C2Task
import com.google.gson.Gson
import com.google.gson.JsonObject
import com.google.gson.reflect.TypeToken
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.net.URLEncoder
import java.security.SecureRandom
import java.security.cert.X509Certificate
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLContext
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager

class AdaptixApiClient {

    private val gson = Gson()
    private val jsonMediaType = "application/json; charset=utf-8".toMediaType()

    var config: C2ServerConfig = C2ServerConfig()
        private set

    var accessToken: String = ""
        private set

    var refreshToken: String = ""
        private set

    // 构建忽略自签名 SSL 校验的 OkHttpClient (团队端测试环境友好)
    private val httpClient: OkHttpClient = createUnsafeOkHttpClient()

    fun updateConfig(newConfig: C2ServerConfig) {
        this.config = newConfig
    }

    /**
     * 登录团队端: POST /api/login
     */
    suspend fun login(): Boolean = withContext(Dispatchers.IO) {
        val url = "${config.httpApiBaseUrl}/login"
        val jsonBody = JsonObject().apply {
            addProperty("username", config.username)
            addProperty("password", config.password)
            addProperty("version", "v1.2")
        }.toString()

        val request = Request.Builder()
            .url(url)
            .post(jsonBody.toRequestBody(jsonMediaType))
            .build()

        val response = httpClient.newCall(request).execute()
        val responseBody = response.body?.string() ?: ""

        if (!response.isSuccessful) {
            throw Exception("登录失败 [HTTP ${response.code}]: $responseBody")
        }

        val jsonResult = gson.fromJson(responseBody, JsonObject::class.java)
        val access = jsonResult.get("access_token")?.asString
        val refresh = jsonResult.get("refresh_token")?.asString ?: ""

        if (access.isNullOrEmpty()) {
            throw Exception("登录响应缺少 access_token")
        }

        this@AdaptixApiClient.accessToken = access
        this@AdaptixApiClient.refreshToken = refresh
        true
    }

    /**
     * 生成 WebSocket 鉴权 OTP: POST /api/otp/generate
     * 对齐 AdaptixServer 规范:
     * { "type": "connect", "data": { "client_type": 2, "console_team_mode": true, "subscriptions": [...] } }
     */
    suspend fun generateWsOtp(): String = withContext(Dispatchers.IO) {
        val url = "${config.httpApiBaseUrl}/otp/generate"

        val defaultSubscriptions = listOf(
            "chat_history", "chat_realtime",
            "downloads_history", "downloads_realtime",
            "screenshot_history", "screenshot_realtime",
            "credentials_history", "credentials_realtime",
            "targets_history", "targets_realtime",
            "console_history", "tasks_history", "tasks_manager",
            "notifications", "tunnels", "agents", "pivots"
        )

        val connectDataObj = JsonObject().apply {
            addProperty("client_type", 2)
            addProperty("console_team_mode", true)
            add("subscriptions", gson.toJsonTree(defaultSubscriptions))
        }

        val jsonBody = JsonObject().apply {
            addProperty("type", "connect")
            add("data", connectDataObj)
        }.toString()

        val request = Request.Builder()
            .url(url)
            .addHeader("Authorization", "Bearer $accessToken")
            .post(jsonBody.toRequestBody(jsonMediaType))
            .build()

        val response = httpClient.newCall(request).execute()
        val responseBody = response.body?.string() ?: ""

        if (!response.isSuccessful) {
            throw Exception("获取 OTP 失败 [HTTP ${response.code}]: $responseBody")
        }

        val jsonResult = gson.fromJson(responseBody, JsonObject::class.java)
        jsonResult.get("message")?.asString ?: throw Exception("OTP 响应格式异常")
    }

    /**
     * 触发实时同步: POST /api/sync
     */
    suspend fun triggerSync(): Boolean = withContext(Dispatchers.IO) {
        val url = "${config.httpApiBaseUrl}/sync"
        val request = Request.Builder()
            .url(url)
            .addHeader("Authorization", "Bearer $accessToken")
            .post("{}".toRequestBody(jsonMediaType))
            .build()

        val response = httpClient.newCall(request).execute()
        response.isSuccessful
    }

    /**
     * 获取 Agent 列表: GET /api/agent/list
     */
    suspend fun getAgents(): List<C2Agent> = withContext(Dispatchers.IO) {
        val url = "${config.httpApiBaseUrl}/agent/list"
        val request = Request.Builder()
            .url(url)
            .addHeader("Authorization", "Bearer $accessToken")
            .get()
            .build()

        val response = httpClient.newCall(request).execute()
        val responseBody = response.body?.string() ?: ""

        if (!response.isSuccessful) return@withContext emptyList()

        val type = object : TypeToken<List<C2Agent>>() {}.type
        gson.fromJson<List<C2Agent>>(responseBody, type) ?: emptyList()
    }

    /**
     * 获取 Listener 列表: GET /api/listener/list
     */
    suspend fun getListeners(): List<C2Listener> = withContext(Dispatchers.IO) {
        val url = "${config.httpApiBaseUrl}/listener/list"
        val request = Request.Builder()
            .url(url)
            .addHeader("Authorization", "Bearer $accessToken")
            .get()
            .build()

        val response = httpClient.newCall(request).execute()
        val responseBody = response.body?.string() ?: ""

        if (!response.isSuccessful) return@withContext emptyList()

        val type = object : TypeToken<List<C2Listener>>() {}.type
        gson.fromJson<List<C2Listener>>(responseBody, type) ?: emptyList()
    }

    /**
     * 下发指令到 Agent: POST /api/agent/command/raw
     */
    suspend fun sendAgentCommand(agentId: String, command: String): Boolean = withContext(Dispatchers.IO) {
        val url = "${config.httpApiBaseUrl}/agent/command/raw"
        val jsonBody = JsonObject().apply {
            addProperty("id", agentId)
            addProperty("cmdline", command)
        }.toString()

        val request = Request.Builder()
            .url(url)
            .addHeader("Authorization", "Bearer $accessToken")
            .post(jsonBody.toRequestBody(jsonMediaType))
            .build()

        val response = httpClient.newCall(request).execute()
        response.isSuccessful
    }

    /**
     * 获取 Agent 历史任务与回显: GET /api/agent/task/list?agent_id=...
     */
    suspend fun getAgentTasks(agentId: String): List<C2Task> = withContext(Dispatchers.IO) {
        if (agentId.isBlank()) return@withContext emptyList()
        val encodedId = URLEncoder.encode(agentId, "UTF-8")
        val url = "${config.httpApiBaseUrl}/agent/task/list?agent_id=$encodedId&limit=200&offset=0"
        val request = Request.Builder()
            .url(url)
            .addHeader("Authorization", "Bearer $accessToken")
            .get()
            .build()

        val response = httpClient.newCall(request).execute()
        val responseBody = response.body?.string() ?: ""

        if (!response.isSuccessful) return@withContext emptyList()

        val type = object : TypeToken<List<C2Task>>() {}.type
        gson.fromJson<List<C2Task>>(responseBody, type) ?: emptyList()
    }

    /**
     * 获取 Chat 消息列表: GET /api/chat/list
     */
    suspend fun getChatMessages(): List<C2ChatMessage> = withContext(Dispatchers.IO) {
        val url = "${config.httpApiBaseUrl}/chat/list"
        val request = Request.Builder()
            .url(url)
            .addHeader("Authorization", "Bearer $accessToken")
            .get()
            .build()

        val response = httpClient.newCall(request).execute()
        val responseBody = response.body?.string() ?: ""

        if (!response.isSuccessful) return@withContext emptyList()

        val type = object : TypeToken<List<C2ChatMessage>>() {}.type
        gson.fromJson<List<C2ChatMessage>>(responseBody, type) ?: emptyList()
    }

    /**
     * 发送 Team Chat 消息: POST /api/chat/send
     */
    suspend fun sendChatMessage(message: String): Boolean = withContext(Dispatchers.IO) {
        val url = "${config.httpApiBaseUrl}/chat/send"
        val jsonBody = JsonObject().apply {
            addProperty("message", message)
        }.toString()

        val request = Request.Builder()
            .url(url)
            .addHeader("Authorization", "Bearer $accessToken")
            .post(jsonBody.toRequestBody(jsonMediaType))
            .build()

        val response = httpClient.newCall(request).execute()
        response.isSuccessful
    }

    /**
     * 获取 Targets 目标列表: GET /api/targets/list
     */
    suspend fun getTargets(): List<C2Target> = withContext(Dispatchers.IO) {
        val url = "${config.httpApiBaseUrl}/targets/list"
        val request = Request.Builder()
            .url(url)
            .addHeader("Authorization", "Bearer $accessToken")
            .get()
            .build()

        val response = httpClient.newCall(request).execute()
        val responseBody = response.body?.string() ?: ""

        if (!response.isSuccessful) return@withContext emptyList()

        val type = object : TypeToken<List<C2Target>>() {}.type
        gson.fromJson<List<C2Target>>(responseBody, type) ?: emptyList()
    }

    /**
     * 获取 Credentials 凭据列表: GET /api/creds/list
     */
    suspend fun getCredentials(): List<C2Credential> = withContext(Dispatchers.IO) {
        val url = "${config.httpApiBaseUrl}/creds/list"
        val request = Request.Builder()
            .url(url)
            .addHeader("Authorization", "Bearer $accessToken")
            .get()
            .build()

        val response = httpClient.newCall(request).execute()
        val responseBody = response.body?.string() ?: ""

        if (!response.isSuccessful) return@withContext emptyList()

        val type = object : TypeToken<List<C2Credential>>() {}.type
        gson.fromJson<List<C2Credential>>(responseBody, type) ?: emptyList()
    }

    private fun createUnsafeOkHttpClient(): OkHttpClient {
        return try {
            val trustAllCerts = arrayOf<TrustManager>(object : X509TrustManager {
                override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) {}
                override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) {}
                override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
            })

            val sslContext = SSLContext.getInstance("TLS")
            sslContext.init(null, trustAllCerts, SecureRandom())

            OkHttpClient.Builder()
                .sslSocketFactory(sslContext.socketFactory, trustAllCerts[0] as X509TrustManager)
                .hostnameVerifier { _, _ -> true }
                .connectTimeout(12, TimeUnit.SECONDS)
                .readTimeout(12, TimeUnit.SECONDS)
                .writeTimeout(12, TimeUnit.SECONDS)
                .build()
        } catch (e: Exception) {
            OkHttpClient.Builder().build()
        }
    }
}
