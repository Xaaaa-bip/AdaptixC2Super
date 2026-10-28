package com.example.adaptixapp.ui.c2

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.adaptixapp.data.NotificationHelper
import com.example.adaptixapp.data.ServerProfileManager
import com.example.adaptixapp.model.c2.C2Agent
import com.example.adaptixapp.model.c2.C2ChatMessage
import com.example.adaptixapp.model.c2.C2Credential
import com.example.adaptixapp.model.c2.C2Listener
import com.example.adaptixapp.model.c2.C2ServerConfig
import com.example.adaptixapp.model.c2.C2Target
import com.example.adaptixapp.network.AdaptixApiClient
import com.example.adaptixapp.network.AdaptixWebSocket
import com.example.adaptixapp.network.AdaptixWsListener
import com.google.gson.Gson
import com.google.gson.JsonObject
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

enum class ConnectionStatus {
    DISCONNECTED,
    CONNECTING,
    CONNECTED,
    ERROR
}

data class ConsoleLogItem(
    val id: String = System.currentTimeMillis().toString() + "_" + (0..10000).random(),
    val agentId: String = "",
    val text: String = "",
    val type: String = "info", // info, error, success
    val timestamp: Long = System.currentTimeMillis()
)

data class C2UiState(
    val connectionStatus: ConnectionStatus = ConnectionStatus.DISCONNECTED,
    val serverConfig: C2ServerConfig = C2ServerConfig(),
    val errorMessage: String? = null,
    val activeTab: Int = 0, // 0: Agents, 1: Listeners, 2: Terminal, 3: Chat, 4: Targets/Creds
    val agents: List<C2Agent> = emptyList(),
    val listeners: List<C2Listener> = emptyList(),
    val chatMessages: List<C2ChatMessage> = emptyList(),
    val targets: List<C2Target> = emptyList(),
    val credentials: List<C2Credential> = emptyList(),
    val consoleLogs: List<ConsoleLogItem> = emptyList(),
    val selectedAgentId: String? = null,
    val statusMessage: String? = null,
    val isSoundEnabled: Boolean = true,
    val isNotifEnabled: Boolean = true
)

class C2MainViewModel : ViewModel() {

    private val _uiState = MutableStateFlow(C2UiState())
    val uiState: StateFlow<C2UiState> = _uiState.asStateFlow()

    private val apiClient = AdaptixApiClient()
    private val wsClient = AdaptixWebSocket()
    private val gson = Gson()

    private var appContext: Context? = null
    private val knownOnlineAgentIds = mutableSetOf<String>()
    private var syncJob: Job? = null
    private var isManualDisconnect = false

    /**
     * 加载本地持久化保存的服务器配置与通知设置
     */
    fun loadSavedConfig(context: Context) {
        this.appContext = context.applicationContext
        val lastConfig = ServerProfileManager.getLastConfig(context)
        val soundOn = NotificationHelper.isSoundEnabled(context)
        val notifOn = NotificationHelper.isNotificationEnabled(context)

        _uiState.update {
            it.copy(
                serverConfig = lastConfig,
                isSoundEnabled = soundOn,
                isNotifEnabled = notifOn
            )
        }
    }

    fun toggleSoundEnabled(enabled: Boolean) {
        appContext?.let { NotificationHelper.setSoundEnabled(it, enabled) }
        _uiState.update { it.copy(isSoundEnabled = enabled) }
    }

    fun toggleNotifEnabled(enabled: Boolean) {
        appContext?.let { NotificationHelper.setNotificationEnabled(it, enabled) }
        _uiState.update { it.copy(isNotifEnabled = enabled) }
    }

    fun testSound() {
        appContext?.let { NotificationHelper.playNotificationTone(it) }
    }

    fun updateServerConfig(config: C2ServerConfig) {
        _uiState.update { it.copy(serverConfig = config) }
    }

    fun setActiveTab(index: Int) {
        _uiState.update { it.copy(activeTab = index) }
        if (index == 3) {
            refreshChatMessages()
        }
    }

    fun selectAgent(agentId: String?) {
        _uiState.update { it.copy(selectedAgentId = agentId) }
        if (!agentId.isNullOrBlank()) {
            fetchAgentTasks(agentId)
        }
    }

    /**
     * 连接 Adaptix C2 团队端并持久化保存配置
     */
    fun connectServer(context: Context, config: C2ServerConfig) {
        this.appContext = context.applicationContext
        isManualDisconnect = false
        _uiState.update {
            it.copy(
                connectionStatus = ConnectionStatus.CONNECTING,
                serverConfig = config,
                errorMessage = null
            )
        }

        viewModelScope.launch {
            try {
                apiClient.updateConfig(config)
                // 1. 登录 HTTP REST API
                apiClient.login()

                // 2. 生成 WebSocket 鉴权票据 (OTP)
                val wsOtp = apiClient.generateWsOtp()
                val wsUrl = "${config.wsConnectUrl}?otp=$wsOtp"

                // 3. 建立 WebSocket 连接
                wsClient.connect(wsUrl, object : AdaptixWsListener {
                    override fun onConnected() {
                        ServerProfileManager.saveLastConfig(context.applicationContext, config)

                        _uiState.update {
                            it.copy(
                                connectionStatus = ConnectionStatus.CONNECTED,
                                statusMessage = "已连接团队端: ${config.httpApiBaseUrl}"
                            )
                        }

                        // 开启后台定期轮询守护
                        startPeriodicSync()

                        // 触发团队端实时 sync 并拉取全量数据
                        viewModelScope.launch {
                            try { apiClient.triggerSync() } catch (e: Exception) { /* ignore */ }
                            refreshAllData()
                        }
                    }

                    override fun onDisconnected(reason: String, shouldReconnect: Boolean) {
                        if (isManualDisconnect) return

                        _uiState.update {
                            it.copy(
                                statusMessage = "WS 连接异常: $reason，正在尝试自动重连…"
                            )
                        }

                        // 断线自动重连机制
                        if (shouldReconnect && !isManualDisconnect) {
                            viewModelScope.launch {
                                delay(3000)
                                if (!isManualDisconnect && _uiState.value.connectionStatus == ConnectionStatus.CONNECTED) {
                                    reconnectWebSocket()
                                }
                            }
                        }
                    }

                    override fun onPacketReceived(type: Int, jsonObject: JsonObject) {
                        handleWsPacket(type, jsonObject)
                    }
                })

            } catch (e: Exception) {
                _uiState.update {
                    it.copy(
                        connectionStatus = ConnectionStatus.ERROR,
                        errorMessage = "连接团队端失败: ${e.localizedMessage}"
                    )
                }
            }
        }
    }

    private suspend fun reconnectWebSocket() {
        try {
            val wsOtp = apiClient.generateWsOtp()
            val wsUrl = "${_uiState.value.serverConfig.wsConnectUrl}?otp=$wsOtp"
            wsClient.connect(wsUrl, object : AdaptixWsListener {
                override fun onConnected() {
                    _uiState.update { it.copy(statusMessage = "WS 信道自动重连成功") }
                    viewModelScope.launch {
                        try { apiClient.triggerSync() } catch (e: Exception) { /* ignore */ }
                    }
                }

                override fun onDisconnected(reason: String, shouldReconnect: Boolean) {
                    if (shouldReconnect && !isManualDisconnect) {
                        viewModelScope.launch {
                            delay(5000)
                            if (!isManualDisconnect) reconnectWebSocket()
                        }
                    }
                }

                override fun onPacketReceived(type: Int, jsonObject: JsonObject) {
                    handleWsPacket(type, jsonObject)
                }
            })
        } catch (e: Exception) {
            // ignore reconnect error
        }
    }

    /**
     * 断开连接
     */
    fun disconnect() {
        isManualDisconnect = true
        syncJob?.cancel()
        syncJob = null
        knownOnlineAgentIds.clear()
        wsClient.disconnect()
        _uiState.update {
            it.copy(
                connectionStatus = ConnectionStatus.DISCONNECTED,
                statusMessage = "已断开团队端连接"
            )
        }
    }

    /**
     * 后台定期同步资产数据 (不刷新控制台，保障终端平滑无卡顿)
     */
    private fun startPeriodicSync() {
        syncJob?.cancel()
        syncJob = viewModelScope.launch {
            while (uiState.value.connectionStatus == ConnectionStatus.CONNECTED) {
                delay(8000)
                refreshBaseData()
            }
        }
    }

    /**
     * 刷新基础资产数据 (Agents, Listeners, Chat, Targets, Creds)
     */
    private fun refreshBaseData() {
        viewModelScope.launch {
            try {
                refreshAgents()
                val listeners = apiClient.getListeners()
                val chat = apiClient.getChatMessages()
                val targets = apiClient.getTargets()
                val creds = apiClient.getCredentials()

                _uiState.update { state ->
                    val mergedChat = mergeChatMessages(state.chatMessages, chat)
                    state.copy(
                        listeners = listeners,
                        chatMessages = mergedChat,
                        targets = targets,
                        credentials = creds
                    )
                }
            } catch (e: Exception) {
                // ignore quiet background refresh error
            }
        }
    }

    /**
     * 刷新全量数据 (包含选择的 Agent 任务)
     */
    fun refreshAllData() {
        refreshBaseData()
        val currentAgent = _uiState.value.selectedAgentId ?: _uiState.value.agents.firstOrNull()?.a_id
        if (!currentAgent.isNullOrBlank()) {
            fetchAgentTasks(currentAgent)
        }
    }

    fun refreshChatMessages() {
        viewModelScope.launch {
            try {
                val chat = apiClient.getChatMessages()
                _uiState.update { state ->
                    state.copy(chatMessages = mergeChatMessages(state.chatMessages, chat))
                }
            } catch (e: Exception) {
                // ignore
            }
        }
    }

    /**
     * 下发 C2 Agent 指令
     */
    fun sendCommand(agentId: String, cmdline: String) {
        if (cmdline.isBlank()) return

        val localCmdId = "cmd_${agentId}_${System.currentTimeMillis()}"
        addOrUpdateConsoleLog(ConsoleLogItem(id = localCmdId, agentId = agentId, text = "> $cmdline", type = "info"))

        viewModelScope.launch {
            try {
                val ok = apiClient.sendAgentCommand(agentId, cmdline)
                if (ok) {
                    delay(1500)
                    fetchAgentTasks(agentId)
                } else {
                    addOrUpdateConsoleLog(ConsoleLogItem(id = "err_${System.currentTimeMillis()}", agentId = agentId, text = "下发指令到 $agentId 失败", type = "error"))
                }
            } catch (e: Exception) {
                addOrUpdateConsoleLog(ConsoleLogItem(id = "err_${System.currentTimeMillis()}", agentId = agentId, text = "发送命令失败: ${e.localizedMessage}", type = "error"))
            }
        }
    }

    /**
     * 拉取指定 Agent 的历史任务与回显结果 (使用稳定的 task_id 防重复与滚动重置)
     */
    fun fetchAgentTasks(agentId: String) {
        if (agentId.isBlank()) return

        viewModelScope.launch {
            try {
                val tasks = apiClient.getAgentTasks(agentId)
                tasks.forEach { task ->
                    val taskId = task.a_task_id ?: "${task.a_cmdline}_${task.a_start_time}"
                    val cmd = task.a_cmdline ?: ""
                    val msg = listOfNotNull(task.a_message, task.a_text)
                        .filter { it.isNotBlank() }
                        .joinToString("\n")
                        .trim()

                    if (cmd.isNotBlank()) {
                        val cmdItem = ConsoleLogItem(
                            id = "task_cmd_$taskId",
                            agentId = agentId,
                            text = "> $cmd",
                            type = "info",
                            timestamp = (task.a_start_time ?: 0) * 1000
                        )
                        addOrUpdateConsoleLog(cmdItem)
                    }

                    if (msg.isNotBlank()) {
                        val outItem = ConsoleLogItem(
                            id = "task_out_$taskId",
                            agentId = agentId,
                            text = msg,
                            type = if (task.a_completed == false) "info" else "success",
                            timestamp = (task.a_finish_time ?: task.a_start_time ?: 0) * 1000
                        )
                        addOrUpdateConsoleLog(outItem)
                    }
                }
            } catch (e: Exception) {
                // ignore task fetch error
            }
        }
    }

    /**
     * 发送团队聊天消息
     */
    fun sendChatMessage(message: String) {
        if (message.isBlank()) return

        val myUsername = _uiState.value.serverConfig.username.ifBlank { "Operator" }
        val myMsg = C2ChatMessage(
            id = System.currentTimeMillis().toString(),
            username = myUsername,
            message = message,
            date = System.currentTimeMillis() / 1000
        )

        // 本地立即打出，流畅响应
        _uiState.update { state ->
            state.copy(chatMessages = mergeChatMessages(state.chatMessages, listOf(myMsg)))
        }

        viewModelScope.launch {
            try {
                val ok = apiClient.sendChatMessage(message)
                if (ok) {
                    delay(500)
                    refreshChatMessages()
                } else {
                    _uiState.update { it.copy(statusMessage = "聊天消息发送失败") }
                }
            } catch (e: Exception) {
                _uiState.update { it.copy(statusMessage = "发送聊天消息失败: ${e.localizedMessage}") }
            }
        }
    }

    /**
     * 处理 WebSocket 实时推送数据包
     */
    private fun handleWsPacket(type: Int, json: JsonObject) {
        when (type) {
            0x44 -> { // AGENT_TICK: 原位更新心跳时间戳，不发 REST 网络请求，不上跳置顶
                val tick = json.get("a_last_tick")?.asLong ?: (System.currentTimeMillis() / 1000)
                val ids: List<String> = when {
                    json.has("a_id") && json.get("a_id").isJsonArray -> {
                        json.getAsJsonArray("a_id").map { it.asString }
                    }
                    json.has("a_id") && !json.get("a_id").isJsonNull -> {
                        listOf(json.get("a_id").asString)
                    }
                    else -> emptyList()
                }

                if (ids.isNotEmpty()) {
                    _uiState.update { state ->
                        val updatedList = state.agents.map { agent ->
                            if (ids.contains(agent.a_id)) {
                                agent.copy(a_last_tick = tick)
                            } else {
                                agent
                            }
                        }
                        state.copy(agents = updatedList)
                    }
                }
            }
            0x41, 0x42, 0x43 -> { // AGENT_NEW, AGENT_UPDATE, AGENT_REMOVE
                refreshAgents()
            }
            0x31, 0x32, 0x33 -> { // LISTENER_START, LISTENER_EDIT, LISTENER_STOP
                refreshListeners()
            }
            0x18 -> { // CHAT_MESSAGE
                val user = extractJsonString(json, "username", "c_username", "user")
                val msg = extractJsonString(json, "message", "c_message", "msg")
                val date = json.get("date")?.asLong ?: json.get("c_date")?.asLong ?: (System.currentTimeMillis() / 1000)

                if (msg.isNotBlank()) {
                    val chat = C2ChatMessage(
                        id = System.currentTimeMillis().toString(),
                        username = user.ifBlank { "Operator" },
                        message = msg,
                        date = date
                    )
                    _uiState.update { state ->
                        state.copy(chatMessages = mergeChatMessages(state.chatMessages, listOf(chat)))
                    }

                    if (user != _uiState.value.serverConfig.username) {
                        appContext?.let { NotificationHelper.notifyChatMessage(it, user, msg) }
                    }
                }
            }
            // CONSOLE & TASK REALTIME PACKETS
            0x67, 0x68, 0x69, 0x6a, 0x6b, 0x49, 0x4a, 0x4b -> {
                val taskId = extractJsonString(json, "a_task_id", "task_id")
                val agentId = extractJsonString(json, "a_id", "agent_id", "id")
                val cmdline = extractJsonString(json, "a_cmdline", "cmdline", "command")
                val message = extractJsonString(json, "a_message", "message")
                val text = extractJsonString(json, "a_text", "text")

                val logType = if (type == 0x68) "error" else "success"
                val bodyText = listOf(message, text).filter { it.isNotBlank() }.joinToString("\n").trim()

                if (bodyText.isNotEmpty()) {
                    val logId = if (taskId.isNotBlank()) "task_out_$taskId" else "ws_out_${System.currentTimeMillis()}"
                    addOrUpdateConsoleLog(ConsoleLogItem(id = logId, agentId = agentId, text = bodyText, type = logType))
                } else if (cmdline.isNotEmpty()) {
                    val formattedCmd = if (cmdline.startsWith(">")) cmdline else "> $cmdline"
                    val logId = if (taskId.isNotBlank()) "task_cmd_$taskId" else "ws_cmd_${System.currentTimeMillis()}"
                    addOrUpdateConsoleLog(ConsoleLogItem(id = logId, agentId = agentId, text = formattedCmd, type = "info"))
                }
            }
        }
    }

    private fun mergeChatMessages(existing: List<C2ChatMessage>, newMsgs: List<C2ChatMessage>): List<C2ChatMessage> {
        val map = LinkedHashMap<String, C2ChatMessage>()
        existing.forEach { msg ->
            val key = "${msg.username}_${msg.message}_${msg.date / 5}" // 5秒内去重
            map[key] = msg
        }
        newMsgs.forEach { msg ->
            val key = "${msg.username}_${msg.message}_${msg.date / 5}"
            map[key] = msg
        }
        return map.values.sortedBy { it.date }
    }

    private fun extractJsonString(json: JsonObject, vararg keys: String): String {
        for (k in keys) {
            if (json.has(k) && !json.get(k).isJsonNull) {
                val elem = json.get(k)
                if (elem.isJsonPrimitive) {
                    val v = elem.asString
                    if (v.isNotBlank()) return v
                }
            }
        }
        return ""
    }

    private fun refreshAgents() {
        viewModelScope.launch {
            val fetchedAgents = apiClient.getAgents()
            val previousKnown = knownOnlineAgentIds.toSet()
            val isInitial = previousKnown.isEmpty()

            fetchedAgents.forEach { agent ->
                if (agent.isOnline) {
                    if (!isInitial && !previousKnown.contains(agent.a_id)) {
                        appContext?.let { ctx ->
                            NotificationHelper.notifyAgentOnline(
                                ctx,
                                agent.a_computer ?: agent.a_id,
                                agent.a_internal_ip ?: "0.0.0.0"
                            )
                        }
                    }
                    knownOnlineAgentIds.add(agent.a_id)
                }
            }

            _uiState.update { state ->
                val merged = mergeAgentsList(state.agents, fetchedAgents)
                state.copy(agents = merged)
            }
        }
    }

    private fun mergeAgentsList(existing: List<C2Agent>, fetched: List<C2Agent>): List<C2Agent> {
        if (existing.isEmpty()) return fetched
        val fetchedMap = fetched.associateBy { it.a_id }
        val result = mutableListOf<C2Agent>()

        // 保持现有列表的相对顺序，仅合并修改过的字段
        existing.forEach { old ->
            val updated = fetchedMap[old.a_id]
            if (updated != null) {
                result.add(if (old == updated) old else updated)
            }
        }

        // 追加新上线的 Agent
        val existingIds = existing.map { it.a_id }.toSet()
        fetched.forEach { newAgent ->
            if (!existingIds.contains(newAgent.a_id)) {
                result.add(newAgent)
            }
        }

        return result
    }

    private fun refreshListeners() {
        viewModelScope.launch {
            val listeners = apiClient.getListeners()
            _uiState.update { it.copy(listeners = listeners) }
        }
    }

    private fun addOrUpdateConsoleLog(item: ConsoleLogItem) {
        _uiState.update { state ->
            val existingList = state.consoleLogs.toMutableList()
            val index = existingList.indexOfFirst { it.id == item.id }
            if (index >= 0) {
                if (existingList[index].text != item.text) {
                    existingList[index] = item
                }
            } else {
                existingList.add(item)
            }
            state.copy(consoleLogs = existingList)
        }
    }
}
