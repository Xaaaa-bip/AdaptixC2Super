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
import androidx.compose.material.icons.filled.Send
import androidx.compose.material.icons.filled.Terminal
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.adaptixapp.model.c2.C2Agent

private val AdaptixBackground = Color(0xFF0D1117)
private val AdaptixSurface = Color(0xFF161B22)
private val AdaptixCardBg = Color(0xFF1C2128)
private val AdaptixBorder = Color(0xFF30363D)
private val AdaptixAccentCyan = Color(0xFF38BDF8)
private val AdaptixAccentGreen = Color(0xFF3FB950)
private val AdaptixErrorRed = Color(0xFFF85149)
private val AdaptixTextPrimary = Color(0xFFF0F6FC)
private val AdaptixTextSecondary = Color(0xFF8B949E)

@Composable
fun C2ConsoleTab(
    agents: List<C2Agent>,
    selectedAgentId: String?,
    consoleLogs: List<ConsoleLogItem>,
    onSelectAgent: (String?) -> Unit,
    onSendCommand: (String, String) -> Unit
) {
    var commandInput by remember { mutableStateOf("") }
    var showAgentDropdown by remember { mutableStateOf(false) }
    val listState = rememberLazyListState()

    val selectedAgent = remember(agents, selectedAgentId) {
        agents.find { it.a_id == selectedAgentId } ?: agents.firstOrNull()
    }

    // 只过滤展示当前选中的 Agent 的控制台与任务日志
    val filteredLogs = remember(consoleLogs, selectedAgent?.a_id) {
        val currentId = selectedAgent?.a_id ?: ""
        if (currentId.isBlank()) consoleLogs else {
            consoleLogs.filter { it.agentId.isEmpty() || it.agentId == currentId }
        }
    }

    // 自动滚动到最新控制台日志
    LaunchedEffect(filteredLogs.size) {
        if (filteredLogs.isNotEmpty()) {
            listState.animateScrollToItem(filteredLogs.size - 1)
        }
    }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .padding(10.dp)
    ) {
        // Agent Selector Row
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Default.Terminal, contentDescription = null, tint = AdaptixAccentCyan)
                Spacer(modifier = Modifier.width(6.dp))
                Text("交互终端", color = AdaptixTextPrimary, fontWeight = FontWeight.Bold, fontSize = 15.sp)
            }

            Box {
                Button(
                    onClick = { showAgentDropdown = true },
                    colors = ButtonDefaults.buttonColors(containerColor = AdaptixSurface),
                    border = BorderStroke(1.dp, AdaptixBorder),
                    shape = RoundedCornerShape(6.dp)
                ) {
                    Text(
                        text = selectedAgent?.let { "${it.a_computer ?: it.a_id} (${it.a_internal_ip ?: "0.0.0.0"})" } ?: "选择 Agent",
                        color = AdaptixAccentCyan,
                        fontSize = 12.sp,
                        fontFamily = FontFamily.Monospace
                    )
                }

                DropdownMenu(
                    expanded = showAgentDropdown,
                    onDismissRequest = { showAgentDropdown = false },
                    modifier = Modifier.background(AdaptixSurface)
                ) {
                    agents.forEach { agent ->
                        DropdownMenuItem(
                            text = {
                                Text(
                                    text = "${agent.a_computer ?: agent.a_id} (${agent.a_internal_ip ?: "0.0.0.0"})",
                                    color = if (agent.a_id == selectedAgent?.a_id) AdaptixAccentCyan else AdaptixTextPrimary,
                                    fontSize = 12.sp,
                                    fontFamily = FontFamily.Monospace
                                )
                            },
                            onClick = {
                                onSelectAgent(agent.a_id)
                                showAgentDropdown = false
                            }
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Quick Command Chips Row (点击可快速填入命令)
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            listOf("sysinfo", "ps", "pwd", "ls", "whoami", "sleep 5").forEach { cmd ->
                Box(
                    modifier = Modifier
                        .background(AdaptixSurface, shape = RoundedCornerShape(4.dp))
                        .border(0.5.dp, AdaptixBorder, shape = RoundedCornerShape(4.dp))
                        .clickable { commandInput = cmd }
                        .padding(horizontal = 8.dp, vertical = 4.dp)
                ) {
                    Text(
                        text = cmd,
                        color = AdaptixAccentCyan,
                        fontSize = 11.sp,
                        fontFamily = FontFamily.Monospace
                    )
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Console Log Terminal Output Box
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .weight(1f)
                .background(AdaptixBackground, shape = RoundedCornerShape(8.dp))
                .border(1.dp, AdaptixBorder, shape = RoundedCornerShape(8.dp))
                .padding(8.dp)
        ) {
            if (filteredLogs.isEmpty()) {
                Text(
                    text = "终端准备就绪。在上方选择 Agent 并发送 C2 命令…",
                    color = AdaptixTextSecondary,
                    fontSize = 12.sp,
                    fontFamily = FontFamily.Monospace
                )
            } else {
                LazyColumn(state = listState) {
                    items(filteredLogs, key = { it.id }) { item ->
                        val color = when (item.type) {
                            "error" -> AdaptixErrorRed
                            "success" -> AdaptixAccentGreen
                            else -> AdaptixTextPrimary
                        }
                        Text(
                            text = item.text,
                            color = color,
                            fontSize = 12.sp,
                            fontFamily = FontFamily.Monospace,
                            modifier = Modifier.padding(vertical = 1.dp)
                        )
                    }
                }
            }
        }

        Spacer(modifier = Modifier.height(8.dp))

        // Command Prompt Input
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp)
        ) {
            OutlinedTextField(
                value = commandInput,
                onValueChange = { commandInput = it },
                placeholder = { Text("输入 C2 命令…", color = AdaptixTextSecondary, fontSize = 13.sp) },
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

            IconButton(
                onClick = {
                    selectedAgent?.let { agent ->
                        if (commandInput.isNotBlank()) {
                            onSendCommand(agent.a_id, commandInput.trim())
                            commandInput = ""
                        }
                    }
                },
                enabled = selectedAgent != null && commandInput.isNotBlank(),
                modifier = Modifier.background(AdaptixAccentCyan, shape = RoundedCornerShape(8.dp))
            ) {
                Icon(
                    imageVector = Icons.Default.Send,
                    contentDescription = "发送",
                    tint = AdaptixBackground,
                    modifier = Modifier.size(20.dp)
                )
            }
        }
    }
}
