package com.example.adaptixapp.ui

import com.example.adaptixapp.model.AppInfo

/**
 * 应用类型筛选
 */
enum class AppFilterType(val label: String) {
    ALL("全部"),
    USER("用户应用"),
    SYSTEM("系统应用")
}

/**
 * 排序类型
 */
enum class AppSortType(val label: String) {
    NAME("名称"),
    INSTALL_TIME("安装时间"),
    UPDATE_TIME("更新时间"),
    SIZE("大小"),
    PACKAGE_NAME("包名")
}

/**
 * App Manager UI 状态定义
 */
data class AppManagerUiState(
    val isLoading: Boolean = true,
    val apps: List<AppInfo> = emptyList(),
    val filteredApps: List<AppInfo> = emptyList(),
    val searchQuery: String = "",
    val filterType: AppFilterType = AppFilterType.ALL,
    val sortType: AppSortType = AppSortType.NAME,
    val sortAscending: Boolean = true,
    val selectedApp: AppInfo? = null,
    val statusMessage: String? = null,
    val userAppCount: Int = 0,
    val systemAppCount: Int = 0
)
