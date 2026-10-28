package com.example.adaptixapp.ui.c2

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Chat
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.ExitToApp
import androidx.compose.material.icons.filled.Notifications
import androidx.compose.material.icons.filled.Radio
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material.icons.filled.TrackChanges
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.ScrollableTabRow
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRowDefaults
import androidx.compose.material3.TabRowDefaults.tabIndicatorOffset
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.lifecycle.viewmodel.compose.viewModel

private val AdaptixBackground = Color(0xFF0D1117)
private val AdaptixSurface = Color(0xFF161B22)
private val AdaptixCardBg = Color(0xFF1C2128)
private val AdaptixBorder = Color(0xFF30363D)
private val AdaptixAccentCyan = Color(0xFF38BDF8)
private val AdaptixAccentGreen = Color(0xFF3FB950)
private val AdaptixTextPrimary = Color(0xFFF0F6FC)
private val AdaptixTextSecondary = Color(0xFF8B949E)

private data class TabEntry(val title: String, val icon: ImageVector)

@Composable
fun C2DashboardScreen(
    viewModel: C2MainViewModel = viewModel(),
    onDisconnectClick: () -> Unit
) {
    val uiState by viewModel.uiState.collectAsState()
    val snackbarHostState = remember { SnackbarHostState() }
    var showNotifSettings by remember { mutableStateOf(false) }

    val tabs = remember {
        listOf(
            TabEntry("上线主机", Icons.Default.Computer),
            TabEntry("监听器", Icons.Default.Radio),
            TabEntry("终端", Icons.Default.Terminal),
            TabEntry("聊天室", Icons.Default.Chat),
            TabEntry("目标与凭据", Icons.Default.TrackChanges)
        )
    }

    LaunchedEffect(uiState.statusMessage) {
        uiState.statusMessage?.let { msg ->
            snackbarHostState.showSnackbar(msg)
        }
    }

    Scaffold(
        containerColor = AdaptixBackground,
        snackbarHost = { SnackbarHost(snackbarHostState) },
        topBar = {
            TopDashboardBar(
                serverUrl = uiState.serverConfig.httpApiBaseUrl,
                onRefresh = { viewModel.refreshAllData() },
                onNotifSettingsClick = { showNotifSettings = true },
                onDisconnect = {
                    viewModel.disconnect()
                    onDisconnectClick()
                }
            )
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
        ) {
            // Top Navigation Tabs
            ScrollableTabRow(
                selectedTabIndex = uiState.activeTab.coerceAtMost(tabs.size - 1),
                containerColor = AdaptixSurface,
                contentColor = AdaptixAccentCyan,
                edgePadding = 8.dp,
                indicator = { tabPositions ->
                    val safeIndex = uiState.activeTab.coerceAtMost(tabs.size - 1)
                    TabRowDefaults.Indicator(
                        modifier = Modifier.tabIndicatorOffset(tabPositions[safeIndex]),
                        color = AdaptixAccentCyan
                    )
                }
            ) {
                tabs.forEachIndexed { index, tab ->
                    val selected = uiState.activeTab == index
                    Tab(
                        selected = selected,
                        onClick = { viewModel.setActiveTab(index) },
                        text = {
                            Row(verticalAlignment = Alignment.CenterVertically) {
                                Icon(
                                    imageVector = tab.icon,
                                    contentDescription = null,
                                    modifier = Modifier.size(16.dp),
                                    tint = if (selected) AdaptixAccentCyan else AdaptixTextSecondary
                                )
                                Spacer(modifier = Modifier.width(4.dp))
                                Text(
                                    text = tab.title,
                                    fontSize = 12.5.sp,
                                    color = if (selected) AdaptixAccentCyan else AdaptixTextSecondary,
                                    fontWeight = if (selected) FontWeight.Bold else FontWeight.Normal
                                )
                            }
                        }
                    )
                }
            }

            // Tab Content Switcher
            Box(modifier = Modifier.weight(1f)) {
                when (uiState.activeTab) {
                    0 -> C2AgentsTab(
                        agents = uiState.agents,
                        onSelectAgentForConsole = { agent ->
                            viewModel.selectAgent(agent.a_id)
                            viewModel.setActiveTab(2)
                        }
                    )
                    1 -> C2ListenersTab(listeners = uiState.listeners)
                    2 -> C2ConsoleTab(
                        agents = uiState.agents,
                        selectedAgentId = uiState.selectedAgentId,
                        consoleLogs = uiState.consoleLogs,
                        onSelectAgent = { agentId -> viewModel.selectAgent(agentId) },
                        onSendCommand = { agentId, cmd -> viewModel.sendCommand(agentId, cmd) }
                    )
                    3 -> C2ChatTab(
                        messages = uiState.chatMessages,
                        onSendMessage = { msg -> viewModel.sendChatMessage(msg) }
                    )
                    4 -> C2TargetsTab(
                        targets = uiState.targets,
                        credentials = uiState.credentials
                    )
                    else -> C2AgentsTab(
                        agents = uiState.agents,
                        onSelectAgentForConsole = { agent ->
                            viewModel.selectAgent(agent.a_id)
                            viewModel.setActiveTab(2)
                        }
                    )
                }
            }
        }
    }

    if (showNotifSettings) {
        NotifSettingsDialog(
            isSoundEnabled = uiState.isSoundEnabled,
            isNotifEnabled = uiState.isNotifEnabled,
            onSoundToggle = { viewModel.toggleSoundEnabled(it) },
            onNotifToggle = { viewModel.toggleNotifEnabled(it) },
            onTestSound = { viewModel.testSound() },
            onDismiss = { showNotifSettings = false }
        )
    }
}

@Composable
private fun TopDashboardBar(
    serverUrl: String,
    onRefresh: () -> Unit,
    onNotifSettingsClick: () -> Unit,
    onDisconnect: () -> Unit
) {
    Surface(
        color = AdaptixSurface,
        border = BorderStroke(1.dp, AdaptixBorder)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                // Connection Indicator Green Dot
                Box(
                    modifier = Modifier
                        .size(10.dp)
                        .background(AdaptixAccentGreen, shape = CircleShape)
                )

                Spacer(modifier = Modifier.width(8.dp))

                Column {
                    Text(
                        text = "Adaptix C2 Teamserver",
                        color = AdaptixTextPrimary,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                    Text(
                        text = "$serverUrl · 在线连接",
                        color = AdaptixTextSecondary,
                        fontSize = 11.sp,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }

            Row {
                IconButton(onClick = onNotifSettingsClick) {
                    Icon(
                        imageVector = Icons.Default.Notifications,
                        contentDescription = "通知设置",
                        tint = AdaptixAccentCyan
                    )
                }

                IconButton(onClick = onRefresh) {
                    Icon(
                        imageVector = Icons.Default.Refresh,
                        contentDescription = "刷新",
                        tint = AdaptixAccentCyan
                    )
                }

                IconButton(onClick = onDisconnect) {
                    Icon(
                        imageVector = Icons.Default.ExitToApp,
                        contentDescription = "断开",
                        tint = AdaptixTextSecondary
                    )
                }
            }
        }
    }
}

@Composable
private fun NotifSettingsDialog(
    isSoundEnabled: Boolean,
    isNotifEnabled: Boolean,
    onSoundToggle: (Boolean) -> Unit,
    onNotifToggle: (Boolean) -> Unit,
    onTestSound: () -> Unit,
    onDismiss: () -> Unit
) {
    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 16.dp),
            shape = RoundedCornerShape(12.dp),
            color = AdaptixSurface,
            border = BorderStroke(1.dp, AdaptixBorder)
        ) {
            Column(modifier = Modifier.padding(18.dp)) {
                Text(
                    text = "上线与通知设置",
                    color = AdaptixAccentCyan,
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )

                Spacer(modifier = Modifier.height(14.dp))

                // Sound Toggle
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text("音效提示", color = AdaptixTextPrimary, fontSize = 14.sp)
                        Text("新主机上线/收到团队消息时播放音效", color = AdaptixTextSecondary, fontSize = 11.sp)
                    }
                    Switch(
                        checked = isSoundEnabled,
                        onCheckedChange = onSoundToggle,
                        colors = SwitchDefaults.colors(checkedThumbColor = AdaptixAccentCyan, checkedTrackColor = AdaptixAccentCyan.copy(alpha = 0.3f))
                    )
                }

                Spacer(modifier = Modifier.height(10.dp))

                // Notification Toggle
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    Column {
                        Text("系统状态栏通知", color = AdaptixTextPrimary, fontSize = 14.sp)
                        Text("新 Agent 上线时弹出系统通知栏提醒", color = AdaptixTextSecondary, fontSize = 11.sp)
                    }
                    Switch(
                        checked = isNotifEnabled,
                        onCheckedChange = onNotifToggle,
                        colors = SwitchDefaults.colors(checkedThumbColor = AdaptixAccentCyan, checkedTrackColor = AdaptixAccentCyan.copy(alpha = 0.3f))
                    )
                }

                Spacer(modifier = Modifier.height(16.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    OutlinedButton(
                        onClick = onTestSound,
                        modifier = Modifier.weight(1f),
                        border = BorderStroke(1.dp, AdaptixAccentCyan.copy(alpha = 0.5f)),
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = AdaptixAccentCyan),
                        shape = RoundedCornerShape(6.dp)
                    ) {
                        Text("测试提示音", fontSize = 12.sp)
                    }

                    Button(
                        onClick = onDismiss,
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = AdaptixAccentCyan),
                        shape = RoundedCornerShape(6.dp)
                    ) {
                        Text("完成", color = Color.Black, fontWeight = FontWeight.Bold, fontSize = 12.sp)
                    }
                }
            }
        }
    }
}
