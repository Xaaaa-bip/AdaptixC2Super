package com.example.adaptixapp.ui

import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.drawable.BitmapDrawable
import android.graphics.drawable.Drawable
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
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
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Clear
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.Share
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.adaptixapp.model.AppInfo
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

// Adaptix 风格暗黑配色
private val AdaptixBackground = Color(0xFF0D1117)
private val AdaptixSurface = Color(0xFF161B22)
private val AdaptixCardBg = Color(0xFF1C2128)
private val AdaptixBorder = Color(0xFF30363D)
private val AdaptixAccentCyan = Color(0xFF38BDF8)
private val AdaptixAccentGreen = Color(0xFF3FB950)
private val AdaptixAccentAmber = Color(0xFFD29922)
private val AdaptixAccentRed = Color(0xFFF85149)
private val AdaptixTextPrimary = Color(0xFFF0F6FC)
private val AdaptixTextSecondary = Color(0xFF8B949E)

@Composable
fun AppManagerScreen(
    viewModel: AppManagerViewModel = viewModel()
) {
    val context = LocalContext.current
    val uiState by viewModel.uiState.collectAsState()
    val snackbarHostState = remember { SnackbarHostState() }

    // 首次载入应用数据
    LaunchedEffect(Unit) {
        viewModel.loadInstalledApps(context)
    }

    // 提示信息响应
    LaunchedEffect(uiState.statusMessage) {
        uiState.statusMessage?.let { msg ->
            snackbarHostState.showSnackbar(msg)
            viewModel.clearStatusMessage()
        }
    }

    Scaffold(
        containerColor = AdaptixBackground,
        snackbarHost = { SnackbarHost(snackbarHostState) },
        topBar = {
            AppManagerTopBar(
                userCount = uiState.userAppCount,
                systemCount = uiState.systemAppCount,
                totalCount = uiState.apps.size,
                isLoading = uiState.isLoading,
                onRefresh = { viewModel.loadInstalledApps(context) }
            )
        }
    ) { paddingValues ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(paddingValues)
                .padding(horizontal = 12.dp)
        ) {
            // 搜索与控制面板
            SearchAndControlBar(
                searchQuery = uiState.searchQuery,
                onQueryChange = { viewModel.setSearchQuery(it) },
                filterType = uiState.filterType,
                onFilterSelect = { viewModel.setFilterType(it) },
                sortType = uiState.sortType,
                sortAscending = uiState.sortAscending,
                onSortSelect = { viewModel.setSortType(it) }
            )

            Spacer(modifier = Modifier.height(8.dp))

            // 主列表或加载遮罩
            if (uiState.isLoading) {
                Box(
                    modifier = Modifier
                        .fillMaxSize()
                        .weight(1f),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        CircularProgressIndicator(color = AdaptixAccentCyan)
                        Spacer(modifier = Modifier.height(12.dp))
                        Text(
                            text = uiState.statusMessage ?: "正在读取应用数据…",
                            color = AdaptixTextSecondary,
                            fontSize = 13.sp
                        )
                    }
                }
            } else if (uiState.filteredApps.isEmpty()) {
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
                            modifier = Modifier.size(48.dp)
                        )
                        Spacer(modifier = Modifier.height(8.dp))
                        Text(
                            text = "未找到符合条件的应用",
                            color = AdaptixTextSecondary,
                            fontSize = 14.sp
                        )
                    }
                }
            } else {
                LazyColumn(
                    modifier = Modifier
                        .fillMaxSize()
                        .weight(1f),
                    verticalArrangement = Arrangement.spacedBy(8.dp)
                ) {
                    items(
                        items = uiState.filteredApps,
                        key = { it.packageName }
                    ) { app ->
                        AppItemCard(
                            app = app,
                            onAppClick = { viewModel.selectApp(app) },
                            onLaunchClick = { viewModel.launchApp(context, app.packageName) }
                        )
                    }
                }
            }
        }
    }

    // 详情 Modal 弹窗
    uiState.selectedApp?.let { app ->
        AppDetailDialog(
            app = app,
            onDismiss = { viewModel.selectApp(null) },
            onLaunch = {
                viewModel.launchApp(context, app.packageName)
                viewModel.selectApp(null)
            },
            onSettings = {
                viewModel.openAppSettings(context, app.packageName)
            },
            onUninstall = {
                viewModel.uninstallApp(context, app.packageName)
            },
            onCopyPackage = {
                viewModel.copyToClipboard(context, "包名", app.packageName)
            },
            onCopyPath = {
                viewModel.copyToClipboard(context, "安装路径", app.apkPath)
            }
        )
    }
}

/**
 * 顶部导航与状态汇总栏
 */
@Composable
private fun AppManagerTopBar(
    userCount: Int,
    systemCount: Int,
    totalCount: Int,
    isLoading: Boolean,
    onRefresh: () -> Unit
) {
    Surface(
        color = AdaptixSurface,
        border = BorderStroke(1.dp, AdaptixBorder),
        shape = RoundedCornerShape(0.dp)
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp, vertical = 10.dp),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            Column {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = "Adaptix",
                        color = AdaptixAccentCyan,
                        fontSize = 16.sp,
                        fontWeight = FontWeight.Bold,
                        fontFamily = FontFamily.Monospace
                    )
                    Text(
                        text = " · 应用管理器",
                        color = AdaptixTextPrimary,
                        fontSize = 16.sp,
                        fontWeight = FontWeight.Bold
                    )
                }
                Spacer(modifier = Modifier.height(2.dp))
                Text(
                    text = "共 $totalCount 个应用 · 用户 $userCount · 系统 $systemCount",
                    color = AdaptixTextSecondary,
                    fontSize = 11.5.sp,
                    fontFamily = FontFamily.Monospace
                )
            }

            IconButton(
                onClick = onRefresh,
                enabled = !isLoading
            ) {
                Icon(
                    imageVector = Icons.Default.Refresh,
                    contentDescription = "刷新列表",
                    tint = if (isLoading) AdaptixTextSecondary else AdaptixAccentCyan
                )
            }
        }
    }
}

/**
 * 搜索框与筛选排序控制条
 */
@Composable
private fun SearchAndControlBar(
    searchQuery: String,
    onQueryChange: (String) -> Unit,
    filterType: AppFilterType,
    onFilterSelect: (AppFilterType) -> Unit,
    sortType: AppSortType,
    sortAscending: Boolean,
    onSortSelect: (AppSortType) -> Unit
) {
    var showSortMenu by remember { mutableStateOf(false) }

    Column(modifier = Modifier.padding(top = 8.dp)) {
        // 搜索文本输入框
        OutlinedTextField(
            value = searchQuery,
            onValueChange = onQueryChange,
            modifier = Modifier.fillMaxWidth(),
            placeholder = { Text("搜索应用名称 / 包名 / 路径", color = AdaptixTextSecondary, fontSize = 13.sp) },
            leadingIcon = { Icon(Icons.Default.Search, contentDescription = null, tint = AdaptixTextSecondary) },
            trailingIcon = {
                if (searchQuery.isNotEmpty()) {
                    IconButton(onClick = { onQueryChange("") }) {
                        Icon(Icons.Default.Clear, contentDescription = "清除搜索", tint = AdaptixTextSecondary)
                    }
                }
            },
            singleLine = true,
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

        Spacer(modifier = Modifier.height(6.dp))

        // 筛选 Chip & 排序按钮 Row
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.SpaceBetween
        ) {
            // 类型筛选 Chip
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                AppFilterType.values().forEach { type ->
                    val selected = filterType == type
                    FilterChip(
                        selected = selected,
                        onClick = { onFilterSelect(type) },
                        label = { Text(type.label, fontSize = 12.sp) },
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = AdaptixAccentCyan.copy(alpha = 0.2f),
                            selectedLabelColor = AdaptixAccentCyan,
                            containerColor = AdaptixSurface,
                            labelColor = AdaptixTextSecondary
                        ),
                        border = FilterChipDefaults.filterChipBorder(
                            enabled = true,
                            selected = selected,
                            borderColor = AdaptixBorder,
                            selectedBorderColor = AdaptixAccentCyan
                        )
                    )
                }
            }

            // 排序按钮
            Box {
                OutlinedButton(
                    onClick = { showSortMenu = true },
                    border = BorderStroke(1.dp, AdaptixBorder),
                    colors = ButtonDefaults.outlinedButtonColors(
                        contentColor = AdaptixTextPrimary,
                        containerColor = AdaptixSurface
                    ),
                    shape = RoundedCornerShape(6.dp),
                    contentPadding = ButtonDefaults.ContentPadding
                ) {
                    val arrow = if (sortAscending) "↑" else "↓"
                    Text(
                        text = "${sortType.label} $arrow",
                        fontSize = 11.5.sp,
                        fontFamily = FontFamily.Monospace,
                        color = AdaptixAccentCyan
                    )
                }

                DropdownMenu(
                    expanded = showSortMenu,
                    onDismissRequest = { showSortMenu = false },
                    modifier = Modifier.background(AdaptixSurface)
                ) {
                    AppSortType.values().forEach { sort ->
                        DropdownMenuItem(
                            text = {
                                Text(
                                    text = sort.label,
                                    color = if (sortType == sort) AdaptixAccentCyan else AdaptixTextPrimary,
                                    fontSize = 13.sp
                                )
                            },
                            onClick = {
                                onSortSelect(sort)
                                showSortMenu = false
                            }
                        )
                    }
                }
            }
        }
    }
}

/**
 * 单个应用卡片 View
 */
@Composable
private fun AppItemCard(
    app: AppInfo,
    onAppClick: () -> Unit,
    onLaunchClick: () -> Unit
) {
    Card(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onAppClick() },
        colors = CardDefaults.cardColors(containerColor = AdaptixCardBg),
        border = BorderStroke(1.dp, AdaptixBorder),
        shape = RoundedCornerShape(8.dp)
    ) {
        Row(
            modifier = Modifier
                .padding(10.dp)
                .fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically
        ) {
            // 应用图标
            AppIconImage(
                drawable = app.icon,
                modifier = Modifier
                    .size(44.dp)
                    .clip(RoundedCornerShape(8.dp))
            )

            Spacer(modifier = Modifier.width(10.dp))

            // 应用核心信息
            Column(modifier = Modifier.weight(1f)) {
                Row(
                    verticalAlignment = Alignment.CenterVertically,
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Text(
                        text = app.appName,
                        color = AdaptixTextPrimary,
                        fontSize = 14.sp,
                        fontWeight = FontWeight.Bold,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        modifier = Modifier.weight(1f, fill = false)
                    )

                    // 系统应用 / 用户应用 标牌
                    TypeBadge(isSystem = app.isSystemApp)
                }

                Spacer(modifier = Modifier.height(2.dp))

                Text(
                    text = app.packageName,
                    color = AdaptixTextSecondary,
                    fontSize = 11.5.sp,
                    fontFamily = FontFamily.Monospace,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis
                )

                Spacer(modifier = Modifier.height(4.dp))

                // 额外属性 Badge 列表
                Row(
                    horizontalArrangement = Arrangement.spacedBy(6.dp),
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    MetaBadge(text = "v${app.versionName}")
                    MetaBadge(text = app.formattedSize)
                    MetaBadge(text = "Target ${app.targetSdkVersion}")
                }
            }

            Spacer(modifier = Modifier.width(8.dp))

            // 快速启动按钮
            OutlinedButton(
                onClick = onLaunchClick,
                border = BorderStroke(1.dp, AdaptixAccentGreen.copy(alpha = 0.5f)),
                colors = ButtonDefaults.outlinedButtonColors(
                    contentColor = AdaptixAccentGreen,
                    containerColor = AdaptixAccentGreen.copy(alpha = 0.08f)
                ),
                shape = RoundedCornerShape(6.dp),
                contentPadding = ButtonDefaults.ContentPadding
            ) {
                Icon(
                    imageVector = Icons.Default.PlayArrow,
                    contentDescription = "启动",
                    modifier = Modifier.size(14.dp)
                )
                Spacer(modifier = Modifier.width(2.dp))
                Text(text = "打开", fontSize = 11.5.sp, fontWeight = FontWeight.Bold)
            }
        }
    }
}

/**
 * 类型 Badge (系统/用户)
 */
@Composable
private fun TypeBadge(isSystem: Boolean) {
    val bg = if (isSystem) AdaptixAccentAmber.copy(alpha = 0.15f) else AdaptixAccentGreen.copy(alpha = 0.15f)
    val color = if (isSystem) AdaptixAccentAmber else AdaptixAccentGreen
    val label = if (isSystem) "系统" else "用户"

    Box(
        modifier = Modifier
            .background(bg, shape = RoundedCornerShape(4.dp))
            .border(0.5.dp, color.copy(alpha = 0.4f), shape = RoundedCornerShape(4.dp))
            .padding(horizontal = 4.dp, vertical = 1.dp)
    ) {
        Text(
            text = label,
            color = color,
            fontSize = 10.sp,
            fontWeight = FontWeight.Bold
        )
    }
}

/**
 * 属性 Badge
 */
@Composable
private fun MetaBadge(text: String) {
    Box(
        modifier = Modifier
            .background(AdaptixSurface, shape = RoundedCornerShape(4.dp))
            .border(0.5.dp, AdaptixBorder, shape = RoundedCornerShape(4.dp))
            .padding(horizontal = 4.dp, vertical = 1.dp)
    ) {
        Text(
            text = text,
            color = AdaptixTextSecondary,
            fontSize = 10.sp,
            fontFamily = FontFamily.Monospace
        )
    }
}

/**
 * 转换 Drawable 为 Compose Image
 */
@Composable
private fun AppIconImage(
    drawable: Drawable?,
    modifier: Modifier = Modifier
) {
    val imageBitmap: ImageBitmap? = remember(drawable) {
        drawable?.toImageBitmap()
    }

    if (imageBitmap != null) {
        Image(
            bitmap = imageBitmap,
            contentDescription = null,
            modifier = modifier
        )
    } else {
        Box(
            modifier = modifier.background(AdaptixSurface, shape = RoundedCornerShape(8.dp)),
            contentAlignment = Alignment.Center
        ) {
            Icon(
                imageVector = Icons.Default.Info,
                contentDescription = null,
                tint = AdaptixTextSecondary
            )
        }
    }
}

/**
 * 详细信息 Dialog 弹窗
 */
@Composable
private fun AppDetailDialog(
    app: AppInfo,
    onDismiss: () -> Unit,
    onLaunch: () -> Unit,
    onSettings: () -> Unit,
    onUninstall: () -> Unit,
    onCopyPackage: () -> Unit,
    onCopyPath: () -> Unit
) {
    val dateFormat = remember { SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()) }
    var showPermissions by remember { mutableStateOf(false) }

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 16.dp),
            shape = RoundedCornerShape(12.dp),
            color = AdaptixSurface,
            border = BorderStroke(1.dp, AdaptixBorder)
        ) {
            Column(
                modifier = Modifier
                    .padding(16.dp)
            ) {
                // 头部应用信息
                Row(verticalAlignment = Alignment.CenterVertically) {
                    AppIconImage(
                        drawable = app.icon,
                        modifier = Modifier
                            .size(52.dp)
                            .clip(RoundedCornerShape(10.dp))
                    )
                    Spacer(modifier = Modifier.width(12.dp))
                    Column(modifier = Modifier.weight(1f)) {
                        Text(
                            text = app.appName,
                            color = AdaptixTextPrimary,
                            fontSize = 16.sp,
                            fontWeight = FontWeight.Bold
                        )
                        Spacer(modifier = Modifier.height(2.dp))
                        Text(
                            text = app.packageName,
                            color = AdaptixTextSecondary,
                            fontSize = 11.5.sp,
                            fontFamily = FontFamily.Monospace
                        )
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // 快捷操作栏
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    Button(
                        onClick = onLaunch,
                        modifier = Modifier.weight(1f),
                        colors = ButtonDefaults.buttonColors(containerColor = AdaptixAccentCyan),
                        shape = RoundedCornerShape(6.dp)
                    ) {
                        Icon(Icons.Default.PlayArrow, contentDescription = null, modifier = Modifier.size(16.dp))
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("打开", fontSize = 12.sp, color = AdaptixBackground, fontWeight = FontWeight.Bold)
                    }

                    OutlinedButton(
                        onClick = onSettings,
                        modifier = Modifier.weight(1f),
                        border = BorderStroke(1.dp, AdaptixBorder),
                        colors = ButtonDefaults.outlinedButtonColors(contentColor = AdaptixTextPrimary),
                        shape = RoundedCornerShape(6.dp)
                    ) {
                        Icon(Icons.Default.Settings, contentDescription = null, modifier = Modifier.size(16.dp))
                        Spacer(modifier = Modifier.width(4.dp))
                        Text("设置", fontSize = 12.sp)
                    }

                    if (!app.isSystemApp) {
                        OutlinedButton(
                            onClick = onUninstall,
                            modifier = Modifier.weight(1f),
                            border = BorderStroke(1.dp, AdaptixAccentRed.copy(alpha = 0.5f)),
                            colors = ButtonDefaults.outlinedButtonColors(contentColor = AdaptixAccentRed),
                            shape = RoundedCornerShape(6.dp)
                        ) {
                            Text("卸载", fontSize = 12.sp, fontWeight = FontWeight.Bold)
                        }
                    }
                }

                Spacer(modifier = Modifier.height(12.dp))

                // 详细元数据表格
                Column(
                    modifier = Modifier
                        .fillMaxWidth()
                        .background(AdaptixCardBg, shape = RoundedCornerShape(8.dp))
                        .border(1.dp, AdaptixBorder, shape = RoundedCornerShape(8.dp))
                        .padding(10.dp),
                    verticalArrangement = Arrangement.spacedBy(6.dp)
                ) {
                    DetailRow("版本", "${app.versionName} (${app.versionCode})")
                    DetailRow("文件大小", app.formattedSize)
                    DetailRow("SDK 要求", "Min ${app.minSdkVersion} / Target ${app.targetSdkVersion}")
                    DetailRow("UID", app.uid.toString())
                    DetailRow("安装时间", dateFormat.format(Date(app.firstInstallTime)))
                    DetailRow("更新时间", dateFormat.format(Date(app.lastUpdateTime)))
                    DetailRow("APK 路径", app.apkPath, onClick = onCopyPath)
                    DetailRow("数据目录", app.dataDir)
                    if (!app.launchActivity.isNullOrEmpty()) {
                        DetailRow("启动入口", app.launchActivity)
                    }
                }

                Spacer(modifier = Modifier.height(10.dp))

                // 复制与权限展开
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically
                ) {
                    TextButton(onClick = onCopyPackage) {
                        Text("复制包名", color = AdaptixAccentCyan, fontSize = 12.sp)
                    }

                    TextButton(onClick = { showPermissions = !showPermissions }) {
                        Text(
                            text = if (showPermissions) "收起权限" else "查看权限 (${app.permissions.size})",
                            color = AdaptixTextSecondary,
                            fontSize = 12.sp
                        )
                    }
                }

                // 权限列表折叠
                AnimatedVisibility(visible = showPermissions) {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(140.dp)
                            .background(AdaptixCardBg, shape = RoundedCornerShape(6.dp))
                            .padding(8.dp)
                    ) {
                        LazyColumn(verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            items(app.permissions) { perm ->
                                Text(
                                    text = perm.removePrefix("android.permission."),
                                    color = AdaptixTextSecondary,
                                    fontSize = 10.5.sp,
                                    fontFamily = FontFamily.Monospace
                                )
                            }
                        }
                    }
                }

                Spacer(modifier = Modifier.height(8.dp))

                Button(
                    onClick = onDismiss,
                    modifier = Modifier.fillMaxWidth(),
                    colors = ButtonDefaults.buttonColors(containerColor = AdaptixSurface),
                    border = BorderStroke(1.dp, AdaptixBorder),
                    shape = RoundedCornerShape(6.dp)
                ) {
                    Text("关闭", color = AdaptixTextPrimary)
                }
            }
        }
    }
}

@Composable
private fun DetailRow(
    label: String,
    value: String,
    onClick: (() -> Unit)? = null
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .then(if (onClick != null) Modifier.clickable { onClick() } else Modifier),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(
            text = label,
            color = AdaptixTextSecondary,
            fontSize = 11.5.sp
        )
        Text(
            text = value,
            color = AdaptixTextPrimary,
            fontSize = 11.5.sp,
            fontFamily = FontFamily.Monospace,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
            modifier = Modifier.padding(start = 12.dp)
        )
    }
}

/**
 * 转换 Drawable 为 ImageBitmap 扩展方法
 */
private fun Drawable.toImageBitmap(): ImageBitmap? {
    return try {
        if (this is BitmapDrawable && this.bitmap != null) {
            this.bitmap.asImageBitmap()
        } else {
            val width = if (intrinsicWidth > 0) intrinsicWidth else 96
            val height = if (intrinsicHeight > 0) intrinsicHeight else 96
            val bitmap = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
            val canvas = Canvas(bitmap)
            setBounds(0, 0, canvas.width, canvas.height)
            draw(canvas)
            bitmap.asImageBitmap()
        }
    } catch (e: Exception) {
        null
    }
}
