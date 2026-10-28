package com.example.adaptixapp.model

import android.graphics.drawable.Drawable

/**
 * 代表一个已安装的 Android 应用信息
 */
data class AppInfo(
    val packageName: String,
    val appName: String,
    val versionName: String,
    val versionCode: Long,
    val isSystemApp: Boolean,
    val icon: Drawable? = null,
    val apkPath: String = "",
    val dataDir: String = "",
    val targetSdkVersion: Int = 0,
    val minSdkVersion: Int = 0,
    val firstInstallTime: Long = 0L,
    val lastUpdateTime: Long = 0L,
    val apkSize: Long = 0L,
    val isEnabled: Boolean = true,
    val uid: Int = 0,
    val permissions: List<String> = emptyList(),
    val launchActivity: String? = null
) {
    /**
     * 格式化格式后的文件大小字符串
     */
    val formattedSize: String
        get() {
            if (apkSize <= 0) return "未知大小"
            val mb = apkSize / (1024.0 * 1024.0)
            return if (mb >= 1.0) {
                String.format("%.1f MB", mb)
            } else {
                val kb = apkSize / 1024.0
                String.format("%.1f KB", kb)
            }
        }
}
