package com.example.adaptixapp.ui.c2

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
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
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Link
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Security
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
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
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.PasswordVisualTransformation
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.adaptixapp.data.ServerProfileManager
import com.example.adaptixapp.model.c2.C2ServerConfig

private val AdaptixBackground = Color(0xFF0D1117)
private val AdaptixSurface = Color(0xFF161B22)
private val AdaptixCardBg = Color(0xFF1C2128)
private val AdaptixBorder = Color(0xFF30363D)
private val AdaptixAccentCyan = Color(0xFF38BDF8)
private val AdaptixAccentGreen = Color(0xFF3FB950)
private val AdaptixTextPrimary = Color(0xFFF0F6FC)
private val AdaptixTextSecondary = Color(0xFF8B949E)
private val AdaptixErrorRed = Color(0xFFF85149)

@Composable
fun C2ConnectScreen(
    currentConfig: C2ServerConfig,
    connectionStatus: ConnectionStatus,
    errorMessage: String?,
    onConnectClick: (C2ServerConfig) -> Unit
) {
    val context = LocalContext.current

    var serverUrl by remember { mutableStateOf(currentConfig.serverUrl) }
    var username by remember { mutableStateOf(currentConfig.username) }
    var password by remember { mutableStateOf(currentConfig.password) }
    var savedProfiles by remember { mutableStateOf(ServerProfileManager.getAllProfiles(context)) }

    // 当 currentConfig （从持久化数据读取）更新时同步文本框
    LaunchedEffect(currentConfig) {
        if (currentConfig.serverUrl.isNotBlank()) serverUrl = currentConfig.serverUrl
        if (currentConfig.username.isNotBlank()) username = currentConfig.username
        if (currentConfig.password.isNotBlank()) password = currentConfig.password
    }

    Box(
        modifier = Modifier
            .fillMaxSize()
            .background(AdaptixBackground)
            .padding(16.dp),
        contentAlignment = Alignment.Center
    ) {
        Column(
            modifier = Modifier.fillMaxWidth(),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Card(
                modifier = Modifier.fillMaxWidth(),
                colors = CardDefaults.cardColors(containerColor = AdaptixCardBg),
                border = BorderStroke(1.dp, AdaptixBorder),
                shape = RoundedCornerShape(12.dp)
            ) {
                Column(
                    modifier = Modifier
                        .padding(20.dp)
                        .fillMaxWidth()
                ) {
                    // Title Banner
                    Row(
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(10.dp)
                    ) {
                        Icon(
                            imageVector = Icons.Default.Security,
                            contentDescription = null,
                            tint = AdaptixAccentCyan,
                            modifier = Modifier.size(36.dp)
                        )
                        Column {
                            Text(
                                text = "Adaptix C2",
                                color = AdaptixAccentCyan,
                                fontSize = 20.sp,
                                fontWeight = FontWeight.Bold,
                                fontFamily = FontFamily.Monospace
                            )
                            Text(
                                text = "Android Teamserver 控制端",
                                color = AdaptixTextSecondary,
                                fontSize = 12.sp
                            )
                        }
                    }

                    Spacer(modifier = Modifier.height(16.dp))

                    // Server Details
                    Text(
                        text = "Server Details",
                        color = AdaptixAccentCyan,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )

                    Spacer(modifier = Modifier.height(6.dp))

                    OutlinedTextField(
                        value = serverUrl,
                        onValueChange = { serverUrl = it },
                        label = { Text("URL (例: http://127.0.0.1:8080)") },
                        leadingIcon = { Icon(Icons.Default.Link, contentDescription = null, tint = AdaptixTextSecondary) },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        colors = textInputColors(),
                        shape = RoundedCornerShape(8.dp)
                    )

                    Spacer(modifier = Modifier.height(14.dp))

                    // User Info
                    Text(
                        text = "User Info",
                        color = AdaptixAccentCyan,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )

                    Spacer(modifier = Modifier.height(6.dp))

                    OutlinedTextField(
                        value = username,
                        onValueChange = { username = it },
                        label = { Text("User") },
                        leadingIcon = { Icon(Icons.Default.Person, contentDescription = null, tint = AdaptixTextSecondary) },
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        colors = textInputColors(),
                        shape = RoundedCornerShape(8.dp)
                    )

                    Spacer(modifier = Modifier.height(8.dp))

                    OutlinedTextField(
                        value = password,
                        onValueChange = { password = it },
                        label = { Text("Password") },
                        leadingIcon = { Icon(Icons.Default.Lock, contentDescription = null, tint = AdaptixTextSecondary) },
                        visualTransformation = PasswordVisualTransformation(),
                        singleLine = true,
                        modifier = Modifier.fillMaxWidth(),
                        colors = textInputColors(),
                        shape = RoundedCornerShape(8.dp)
                    )

                    Spacer(modifier = Modifier.height(14.dp))

                    // Error Message
                    if (!errorMessage.isNullOrEmpty()) {
                        Text(
                            text = errorMessage,
                            color = AdaptixErrorRed,
                            fontSize = 12.sp,
                            modifier = Modifier.padding(bottom = 10.dp)
                        )
                    }

                    // Connect Button
                    val isConnecting = connectionStatus == ConnectionStatus.CONNECTING
                    Button(
                        onClick = {
                            val config = C2ServerConfig(
                                serverUrl = serverUrl.trim(),
                                username = username.trim(),
                                password = password
                            )
                            onConnectClick(config)
                        },
                        enabled = !isConnecting && serverUrl.isNotBlank() && username.isNotBlank(),
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(48.dp),
                        colors = ButtonDefaults.buttonColors(containerColor = AdaptixAccentGreen),
                        shape = RoundedCornerShape(8.dp)
                    ) {
                        if (isConnecting) {
                            CircularProgressIndicator(
                                modifier = Modifier.size(20.dp),
                                color = AdaptixBackground,
                                strokeWidth = 2.dp
                            )
                            Spacer(modifier = Modifier.width(8.dp))
                            Text("建立 C2 信道中…", color = AdaptixBackground, fontWeight = FontWeight.Bold)
                        } else {
                            Icon(Icons.Default.PlayArrow, contentDescription = null, tint = AdaptixBackground)
                            Spacer(modifier = Modifier.width(6.dp))
                            Text("Connect", color = AdaptixBackground, fontWeight = FontWeight.Bold, fontSize = 16.sp)
                        }
                    }
                }
            }

            // 已保存的历史配置 Profiles
            if (savedProfiles.isNotEmpty()) {
                Spacer(modifier = Modifier.height(12.dp))

                Text(
                    text = "已保存的连接配置 (${savedProfiles.size})",
                    color = AdaptixTextSecondary,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(start = 4.dp, bottom = 6.dp)
                )

                LazyColumn(
                    modifier = Modifier.fillMaxWidth(),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    items(savedProfiles) { profile ->
                        Card(
                            modifier = Modifier
                                .fillMaxWidth()
                                .clickable {
                                    serverUrl = profile.serverUrl
                                    username = profile.username
                                    password = profile.password
                                },
                            colors = CardDefaults.cardColors(containerColor = AdaptixSurface),
                            border = BorderStroke(1.dp, AdaptixBorder),
                            shape = RoundedCornerShape(6.dp)
                        ) {
                            Row(
                                modifier = Modifier
                                    .padding(horizontal = 12.dp, vertical = 8.dp)
                                    .fillMaxWidth(),
                                horizontalArrangement = Arrangement.SpaceBetween,
                                verticalAlignment = Alignment.CenterVertically
                            ) {
                                Column(modifier = Modifier.weight(1f)) {
                                    Text(
                                        text = "${profile.username} @ ${profile.serverUrl}",
                                        color = AdaptixAccentCyan,
                                        fontSize = 12.5.sp,
                                        fontWeight = FontWeight.Bold,
                                        fontFamily = FontFamily.Monospace
                                    )
                                }

                                IconButton(
                                    onClick = {
                                        ServerProfileManager.deleteProfile(context, profile)
                                        savedProfiles = ServerProfileManager.getAllProfiles(context)
                                    },
                                    modifier = Modifier.size(24.dp)
                                ) {
                                    Icon(
                                        imageVector = Icons.Default.Delete,
                                        contentDescription = "删除配置",
                                        tint = AdaptixTextSecondary,
                                        modifier = Modifier.size(16.dp)
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun textInputColors() = OutlinedTextFieldDefaults.colors(
    focusedContainerColor = AdaptixSurface,
    unfocusedContainerColor = AdaptixSurface,
    focusedBorderColor = AdaptixAccentCyan,
    unfocusedBorderColor = AdaptixBorder,
    focusedTextColor = AdaptixTextPrimary,
    unfocusedTextColor = AdaptixTextPrimary,
    focusedLabelColor = AdaptixAccentCyan,
    unfocusedLabelColor = AdaptixTextSecondary
)
