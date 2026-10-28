package com.example.adaptixapp.ui.c2

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
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
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Computer
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import com.example.adaptixapp.model.c2.C2Agent

private val AdaptixSurface = Color(0xFF161B22)
private val AdaptixCardBg = Color(0xFF1C2128)
private val AdaptixBorder = Color(0xFF30363D)
private val AdaptixAccentCyan = Color(0xFF38BDF8)
private val AdaptixAccentGreen = Color(0xFF3FB950)
private val AdaptixAccentAmber = Color(0xFFD29922)
private val AdaptixTextPrimary = Color(0xFFF0F6FC)
private val AdaptixTextSecondary = Color(0xFF8B949E)

@Composable
fun C2AgentsTab(
    agents: List<C2Agent>,
    onSelectAgentForConsole: (C2Agent) -> Unit
) {
    var searchQuery by remember { mutableStateOf("") }
    var selectedAgentDetail by remember { mutableStateOf<C2Agent?>(null) }
    val listState = rememberLazyListState()

    val filteredAgents = remember(agents, searchQuery) {
        if (searchQuery.isBlank()) agents else {
            val q = searchQuery.lowercase()
            agents.filter {
                (it.a_computer ?: "").lowercase().contains(q) ||
                        (it.a_username ?: "").lowercase().contains(q) ||
                        (it.a_internal_ip ?: "").lowercase().contains(q) ||
                        (it.a_external_ip ?: "").lowercase().contains(q) ||
                        (it.a_id).lowercase().contains(q)
            }
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(10.dp)
    ) {
        // Search & Count Bar
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedTextField(
                value = searchQuery,
                onValueChange = { searchQuery = it },
                placeholder = { Text("过滤主机 / IP / 用户 / ID", color = AdaptixTextSecondary, fontSize = 12.sp) },
                singleLine = true,
                modifier = Modifier.weight(1f),
                colors = OutlinedTextFieldDefaults.colors(
                    focusedContainerColor = AdaptixCardBg,
                    unfocusedContainerColor = AdaptixCardBg,
                    focusedBorderColor = AdaptixAccentCyan,
                    unfocusedBorderColor = AdaptixBorder,
                    focusedTextColor = AdaptixTextPrimary,
                    unfocusedTextColor = AdaptixTextPrimary
                ),
                shape = RoundedCornerShape(8.dp)
            )

            val onlineCount = agents.count { it.isOnline }
            Box(
                modifier = Modifier
                    .background(AdaptixSurface, shape = RoundedCornerShape(8.dp))
                    .border(1.dp, AdaptixBorder, shape = RoundedCornerShape(8.dp))
                    .padding(horizontal = 10.dp, vertical = 12.dp)
            ) {
                Text(
                    text = "在线 $onlineCount / ${agents.size}",
                    color = AdaptixAccentGreen,
                    fontSize = 12.sp,
                    fontFamily = FontFamily.Monospace,
                    fontWeight = FontWeight.Bold
                )
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Agents List
        if (filteredAgents.isEmpty()) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .weight(1f),
                contentAlignment = Alignment.Center
            ) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Icon(
                        imageVector = Icons.Default.Warning,
                        contentDescription = null,
                        tint = AdaptixTextSecondary,
                        modifier = Modifier.size(40.dp)
                    )
                    Spacer(modifier = Modifier.height(8.dp))
                    Text("暂无可用的 Agent 上线主机", color = AdaptixTextSecondary, fontSize = 13.sp)
                }
            }
        } else {
            LazyColumn(
                state = listState,
                modifier = Modifier
                    .fillMaxSize()
                    .weight(1f),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(filteredAgents, key = { it.a_id }) { agent ->
                    AgentCardItem(
                        agent = agent,
                        onClick = { selectedAgentDetail = agent },
                        onConsoleClick = { onSelectAgentForConsole(agent) }
                    )
                }
            }
        }
    }

    // Detail Dialog
    selectedAgentDetail?.let { agent ->
        AgentDetailDialog(
            agent = agent,
            onDismiss = { selectedAgentDetail = null },
            onConsoleClick = {
                onSelectAgentForConsole(agent)
                selectedAgentDetail = null
            }
        )
    }
}

@Composable
private fun AgentCardItem(
    agent: C2Agent,
    onClick: () -> Unit,
    onConsoleClick: () -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick() },
        colors = CardDefaults.cardColors(containerColor = AdaptixCardBg),
        border = BorderStroke(1.dp, if (agent.isOnline) AdaptixAccentGreen.copy(alpha = 0.4f) else AdaptixBorder),
        shape = RoundedCornerShape(8.dp)
    ) {
        Row(
            modifier = Modifier
                .padding(12.dp)
                .fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // OS Badge
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .background(AdaptixSurface, shape = RoundedCornerShape(8.dp))
                    .border(1.dp, AdaptixBorder, shape = RoundedCornerShape(8.dp)),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = Icons.Default.Computer,
                    contentDescription = null,
                    tint = if (agent.isOnline) AdaptixAccentGreen else AdaptixTextSecondary,
                    modifier = Modifier.size(24.dp)
                )
            }

            Spacer(modifier = Modifier.width(10.dp))

            // Computer Info
            Column(modifier = Modifier.weight(1f)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text(
                        text = agent.a_computer ?: agent.a_id,
                        color = AdaptixTextPrimary,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis
                    )

                    // Online Tag
                    StatusPill(isOnline = agent.isOnline)
                }

                Spacer(modifier = Modifier.height(2.dp))

                Text(
                    text = "${agent.a_username ?: "—"} @ ${agent.a_internal_ip ?: "0.0.0.0"}",
                    color = AdaptixAccentCyan,
                    fontSize = 12.sp,
                    fontFamily = FontFamily.Monospace,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )

                Spacer(modifier = Modifier.height(4.dp))

                Row(
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    TagBadge(text = agent.osName)
                    TagBadge(text = "PID ${agent.a_pid ?: "-"}")
                    TagBadge(text = "Sleep ${agent.a_sleep ?: 10}s")
                }
            }

            Spacer(modifier = Modifier.width(8.dp))

            // Console Button
            OutlinedButton(
                onClick = onConsoleClick,
                border = BorderStroke(1.dp, AdaptixAccentCyan.copy(alpha = 0.5f)),
                colors = ButtonDefaults.outlinedButtonColors(
                    contentColor = AdaptixAccentCyan,
                    containerColor = AdaptixAccentCyan.copy(alpha = 0.1f)
                ),
                shape = RoundedCornerShape(6.dp)
            ) {
                Icon(Icons.Default.Terminal, contentDescription = "终端", modifier = Modifier.size(16.dp))
                Spacer(modifier = Modifier.width(4.dp))
                Text("终端", fontSize = 12.sp, fontWeight = FontWeight.Bold)
            }
        }
    }
}

@Composable
private fun StatusPill(isOnline: Boolean) {
    val bg = if (isOnline) AdaptixAccentGreen.copy(alpha = 0.15f) else AdaptixTextSecondary.copy(alpha = 0.15f)
    val color = if (isOnline) AdaptixAccentGreen else AdaptixTextSecondary
    val label = if (isOnline) "在线" else "离线"

    Box(
        modifier = Modifier
            .background(bg, shape = RoundedCornerShape(4.dp))
            .border(0.5.dp, color.copy(alpha = 0.4f), shape = RoundedCornerShape(4.dp))
            .padding(horizontal = 4.dp, vertical = 1.dp)
    ) {
        Text(text = label, color = color, fontSize = 10.sp, fontWeight = FontWeight.Bold)
    }
}

@Composable
private fun TagBadge(text: String) {
    Box(
        modifier = Modifier
            .background(AdaptixSurface, shape = RoundedCornerShape(4.dp))
            .border(0.5.dp, AdaptixBorder, shape = RoundedCornerShape(4.dp))
            .padding(horizontal = 4.dp, vertical = 1.dp)
    ) {
        Text(text = text, color = AdaptixTextSecondary, fontSize = 10.sp, fontFamily = FontFamily.Monospace)
    }
}

@Composable
private fun AgentDetailDialog(
    agent: C2Agent,
    onDismiss: () -> Unit,
    onConsoleClick: () -> Unit
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
            Column(modifier = Modifier.padding(16.dp)) {
                Text(
                    text = "Agent 主机详情",
                    color = AdaptixAccentCyan,
                    fontSize = 16.sp,
                    fontWeight = FontWeight.Bold,
                    fontFamily = FontFamily.Monospace
                )

                Spacer(modifier = Modifier.height(10.dp))

                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(AdaptixCardBg, shape = RoundedCornerShape(8.dp))
                        .border(1.dp, AdaptixBorder, shape = RoundedCornerShape(8.dp))
                        .padding(10.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    DetailItem("Agent ID", agent.a_id)
                    DetailItem("主机名", agent.a_computer ?: "—")
                    DetailItem("用户名", agent.a_username ?: "—")
                    DetailItem("内网 IP", agent.a_internal_ip ?: "—")
                    DetailItem("外网 IP", agent.a_external_ip ?: "—")
                    DetailItem("操作系统", agent.osName)
                    DetailItem("进程名", agent.a_process ?: "—")
                    DetailItem("PID / TID", "${agent.a_pid ?: "-"} / ${agent.a_tid ?: "-"}")
                    DetailItem("Sleep / Jitter", "${agent.a_sleep ?: 10}s / ${agent.a_jitter ?: 0}%")
                    DetailItem("权限状态", if (agent.a_elevated == true) "高权限 (Admin/Root)" else "普通权限")
                    DetailItem("监听器", agent.a_listener ?: "—")
                }

                Spacer(modifier = Modifier.height(14.dp))

                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    Button(
                        onClick = onConsoleClick,
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = AdaptixAccentCyan),
                        shape = RoundedCornerShape(6.dp)
                    ) {
                        Text("打开控制台", color = Color.Black, fontWeight = FontWeight.Bold)
                    }

                    OutlinedButton(
                        onClick = onDismiss,
                        modifier = Modifier.weight(1f),
                        border = BorderStroke(1.dp, AdaptixBorder),
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = AdaptixTextPrimary),
                        shape = RoundedCornerShape(6.dp)
                    ) {
                        Text("关闭")
                    }
                }
            }
        }
    }
}

@Composable
private fun DetailItem(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(text = label, color = AdaptixTextSecondary, fontSize = 11.5.sp)
        Text(
            text = value,
            color = AdaptixTextPrimary,
            fontSize = 11.5.sp,
            fontFamily = FontFamily.Monospace,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis
        )
    }
}
