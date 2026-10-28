package com.example.adaptixapp.network

import com.google.gson.Gson
import com.google.gson.JsonObject
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.Response
import okhttp3.WebSocket
import okhttp3.WebSocketListener
import java.security.SecureRandom
import java.security.cert.X509Certificate
import java.util.concurrent.TimeUnit
import javax.net.ssl.SSLContext
import javax.net.ssl.TrustManager
import javax.net.ssl.X509TrustManager

interface AdaptixWsListener {
    fun onConnected()
    fun onDisconnected(reason: String, shouldReconnect: Boolean)
    fun onPacketReceived(type: Int, jsonObject: JsonObject)
}

class AdaptixWebSocket {

    private val gson = Gson()
    private var webSocket: WebSocket? = null
    private var client: OkHttpClient = createUnsafeOkHttpClient()
    private var isUserClosed = false

    fun connect(wsUrlWithTicket: String, listener: AdaptixWsListener) {
        disconnect()
        isUserClosed = false

        val request = Request.Builder()
            .url(wsUrlWithTicket)
            .build()

        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                listener.onConnected()
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                try {
                    text.split("\n")
                        .map { it.trim() }
                        .filter { it.isNotEmpty() }
                        .forEach { line ->
                            val json = gson.fromJson(line, JsonObject::class.java)
                            val type = json.get("type")?.asInt ?: json.get("Type")?.asInt ?: 0
                            listener.onPacketReceived(type, json)
                        }
                } catch (e: Exception) {
                    // ignore parse error
                }
            }

            override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
                if (!isUserClosed) {
                    listener.onDisconnected("连接已关闭 [$code]: $reason", true)
                }
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                if (!isUserClosed) {
                    val cause = t.localizedMessage ?: t.message ?: t.javaClass.simpleName
                    listener.onDisconnected("WS 连接中断: $cause", true)
                }
            }
        })
    }

    fun disconnect() {
        isUserClosed = true
        webSocket?.close(1000, "User logout")
        webSocket = null
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
                .pingInterval(15, TimeUnit.SECONDS)
                .connectTimeout(15, TimeUnit.SECONDS)
                .readTimeout(15, TimeUnit.SECONDS)
                .writeTimeout(15, TimeUnit.SECONDS)
                .build()
        } catch (e: Exception) {
            OkHttpClient.Builder().build()
        }
    }
}
