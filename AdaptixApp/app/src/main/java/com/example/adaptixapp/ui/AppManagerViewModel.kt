package com.example.adaptixapp.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.provider.Settings
import androidx.core.content.pm.PackageInfoCompat
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.example.adaptixapp.model.AppInfo
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

class AppManagerViewModel : ViewModel() {

    private val _uiState = MutableStateFlow(AppManagerUiState())
    val uiState: StateFlow<AppManagerUiState> = _uiState.asStateFlow()

    /**
     * 异步加载设备上已安装的应用列表
     */
    fun loadInstalledApps(context: Context) {
        _uiState.update { it.copy(isLoading = true, statusMessage = "正在拉取安装包列表…") }

        viewModelScope.launch {
            val appList = withContext(Dispatchers.IO) {
                fetchAppsFromPackageManager(context.applicationContext)
            }

            val userCount = appList.count { !it.isSystemApp }
            val systemCount = appList.count { it.isSystemApp }

            _uiState.update { currentState ->
                val newFiltered = applyFilterAndSort(
                    appList = appList,
                    query = currentState.searchQuery,
                    filter = currentState.filterType,
                    sort = currentState.sortType,
                    ascending = currentState.sortAscending
                )

                currentState.copy(
                    isLoading = false,
                    apps = appList,
                    filteredApps = newFiltered,
                    userAppCount = userCount,
                    systemAppCount = systemCount,
                    statusMessage = "加载完毕 · 共 ${appList.size} 个应用"
                )
            }
        }
    }

    /**
     * 设置搜索查询关键词
     */
    fun setSearchQuery(query: String) {
        _uiState.update { state ->
            val filtered = applyFilterAndSort(
                appList = state.apps,
                query = query,
                filter = state.filterType,
                sort = state.sortType,
                ascending = state.sortAscending
            )
            state.copy(searchQuery = query, filteredApps = filtered)
        }
    }

    /**
     * 设置筛选类型（全部 / 用户应用 / 系统应用）
     */
    fun setFilterType(filterType: AppFilterType) {
        _uiState.update { state ->
            val filtered = applyFilterAndSort(
                appList = state.apps,
                query = state.searchQuery,
                filter = filterType,
                sort = state.sortType,
                ascending = state.sortAscending
            )
            state.copy(filterType = filterType, filteredApps = filtered)
        }
    }

    /**
     * 设置排序类型
     */
    fun setSortType(sortType: AppSortType) {
        _uiState.update { state ->
            val isSame = state.sortType == sortType
            val newAscending = if (isSame) !state.sortAscending else true
            val filtered = applyFilterAndSort(
                appList = state.apps,
                query = state.searchQuery,
                filter = state.filterType,
                sort = sortType,
                ascending = newAscending
            )
            state.copy(sortType = sortType, sortAscending = newAscending, filteredApps = filtered)
        }
    }

    /**
     * 选中应用查看详情
     */
    fun selectApp(appInfo: AppInfo?) {
        _uiState.update { it.copy(selectedApp = appInfo) }
    }

    /**
     * 清除状态消息
     */
    fun clearStatusMessage() {
        _uiState.update { it.copy(statusMessage = null) }
    }

    /**
     * 启动应用
     */
    fun launchApp(context: Context, packageName: String): Boolean {
        return try {
            val intent = context.packageManager.getLaunchIntentForPackage(packageName)
            if (intent != null) {
                intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                context.startActivity(intent)
                _uiState.update { it.copy(statusMessage = "已启动应用: $packageName") }
                true
            } else {
                _uiState.update { it.copy(statusMessage = "无法启动：没有可调用的主界面") }
                false
            }
        } catch (e: Exception) {
            _uiState.update { it.copy(statusMessage = "启动失败: ${e.localizedMessage}") }
            false
        }
    }

    /**
     * 打开系统应用设置界面
     */
    fun openAppSettings(context: Context, packageName: String) {
        try {
            val intent = Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS).apply {
                data = Uri.fromParts("package", packageName, null)
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
        } catch (e: Exception) {
            _uiState.update { it.copy(statusMessage = "打开设置失败: ${e.localizedMessage}") }
        }
    }

    /**
     * 卸载应用
     */
    fun uninstallApp(context: Context, packageName: String) {
        try {
            val intent = Intent(Intent.ACTION_DELETE).apply {
                data = Uri.parse("package:$packageName")
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            }
            context.startActivity(intent)
        } catch (e: Exception) {
            _uiState.update { it.copy(statusMessage = "发起卸载失败: ${e.localizedMessage}") }
        }
    }

    /**
     * 复制文本到剪贴板
     */
    fun copyToClipboard(context: Context, label: String, text: String) {
        try {
            val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            val clip = ClipData.newPlainText(label, text)
            clipboard.setPrimaryClip(clip)
            _uiState.update { it.copy(statusMessage = "已复制 $label 到剪贴板") }
        } catch (e: Exception) {
            _uiState.update { it.copy(statusMessage = "复制失败: ${e.localizedMessage}") }
        }
    }

    private fun fetchAppsFromPackageManager(context: Context): List<AppInfo> {
        val pm = context.packageManager
        val flags = PackageManager.GET_PERMISSIONS or PackageManager.GET_META_DATA
        val packages: List<PackageInfo> = try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                pm.getInstalledPackages(PackageManager.PackageInfoFlags.of(flags.toLong()))
            } else {
                @Suppress("DEPRECATION")
                pm.getInstalledPackages(flags)
            }
        } catch (e: Exception) {
            emptyList()
        }

        return packages.mapNotNull { pkg ->
            try {
                val appInfo = pkg.applicationInfo ?: return@mapNotNull null
                val appName = appInfo.loadLabel(pm).toString().ifEmpty { pkg.packageName }
                val isSystem = (appInfo.flags and ApplicationInfo.FLAG_SYSTEM) != 0 ||
                        (appInfo.flags and ApplicationInfo.FLAG_UPDATED_SYSTEM_APP) != 0

                val icon = try {
                    appInfo.loadIcon(pm)
                } catch (e: Exception) {
                    null
                }

                val apkPath = appInfo.sourceDir ?: ""
                val apkSize = if (apkPath.isNotEmpty()) {
                    try {
                        File(apkPath).length()
                    } catch (e: Exception) {
                        0L
                    }
                } else 0L

                val launchIntent = pm.getLaunchIntentForPackage(pkg.packageName)
                val launchActivity = launchIntent?.component?.className

                val minSdk = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.N) {
                    appInfo.minSdkVersion
                } else 0

                AppInfo(
                    packageName = pkg.packageName,
                    appName = appName,
                    versionName = pkg.versionName ?: "1.0",
                    versionCode = PackageInfoCompat.getLongVersionCode(pkg),
                    isSystemApp = isSystem,
                    icon = icon,
                    apkPath = apkPath,
                    dataDir = appInfo.dataDir ?: "",
                    targetSdkVersion = appInfo.targetSdkVersion,
                    minSdkVersion = minSdk,
                    firstInstallTime = pkg.firstInstallTime,
                    lastUpdateTime = pkg.lastUpdateTime,
                    apkSize = apkSize,
                    isEnabled = appInfo.enabled,
                    uid = appInfo.uid,
                    permissions = pkg.requestedPermissions?.toList() ?: emptyList(),
                    launchActivity = launchActivity
                )
            } catch (e: Exception) {
                null
            }
        }.sortedBy { it.appName.lowercase() }
    }

    private fun applyFilterAndSort(
        appList: List<AppInfo>,
        query: String,
        filter: AppFilterType,
        sort: AppSortType,
        ascending: Boolean
    ): List<AppInfo> {
        var filtered = appList

        // 步骤 1: 类型过滤
        filtered = when (filter) {
            AppFilterType.ALL -> filtered
            AppFilterType.USER -> filtered.filter { !it.isSystemApp }
            AppFilterType.SYSTEM -> filtered.filter { it.isSystemApp }
        }

        // 步骤 2: 关键词过滤
        if (query.isNotBlank()) {
            val q = query.trim().lowercase()
            filtered = filtered.filter { app ->
                app.appName.lowercase().contains(q) ||
                        app.packageName.lowercase().contains(q) ||
                        app.apkPath.lowercase().contains(q)
            }
        }

        // 步骤 3: 排序
        val comparator = when (sort) {
            AppSortType.NAME -> compareBy<AppInfo> { it.appName.lowercase() }
            AppSortType.PACKAGE_NAME -> compareBy { it.packageName.lowercase() }
            AppSortType.INSTALL_TIME -> compareBy { it.firstInstallTime }
            AppSortType.UPDATE_TIME -> compareBy { it.lastUpdateTime }
            AppSortType.SIZE -> compareBy { it.apkSize }
        }

        return if (ascending) {
            filtered.sortedWith(comparator)
        } else {
            filtered.sortedWith(comparator.reversed())
        }
    }
}
