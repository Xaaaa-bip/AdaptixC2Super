package com.example.adaptixapp.data

import android.content.Context
import android.content.SharedPreferences
import com.example.adaptixapp.model.c2.C2ServerConfig
import com.google.gson.Gson
import com.google.gson.reflect.TypeToken

object ServerProfileManager {

    private const val PREF_NAME = "adaptix_c2_profiles"
    private const val KEY_LAST_CONFIG = "last_c2_config"
    private const val KEY_PROFILES = "saved_c2_profiles"
    private const val KEY_AUTO_CONNECT = "auto_connect_enabled"

    private val gson = Gson()

    private fun getPrefs(context: Context): SharedPreferences {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
    }

    /**
     * 获取最后一次连接使用的服务器配置
     */
    fun getLastConfig(context: Context): C2ServerConfig {
        val json = getPrefs(context).getString(KEY_LAST_CONFIG, null)
        return if (!json.isNullOrEmpty()) {
            try {
                gson.fromJson(json, C2ServerConfig::class.java) ?: C2ServerConfig()
            } catch (e: Exception) {
                C2ServerConfig()
            }
        } else {
            C2ServerConfig()
        }
    }

    /**
     * 保存最后一次使用的配置
     */
    fun saveLastConfig(context: Context, config: C2ServerConfig) {
        val json = gson.toJson(config)
        getPrefs(context).edit().putString(KEY_LAST_CONFIG, json).apply()
        saveProfile(context, config)
    }

    /**
     * 获取所有已保存的配置 Profiles
     */
    fun getAllProfiles(context: Context): List<C2ServerConfig> {
        val json = getPrefs(context).getString(KEY_PROFILES, null)
        if (json.isNullOrEmpty()) return emptyList()
        return try {
            val type = object : TypeToken<List<C2ServerConfig>>() {}.type
            gson.fromJson<List<C2ServerConfig>>(json, type) ?: emptyList()
        } catch (e: Exception) {
            emptyList()
        }
    }

    /**
     * 保存或更新 Profile
     */
    fun saveProfile(context: Context, config: C2ServerConfig) {
        val current = getAllProfiles(context).toMutableList()
        val index = current.indexOfFirst {
            it.serverUrl.equals(config.serverUrl, ignoreCase = true) &&
                    it.username.equals(config.username, ignoreCase = true)
        }
        if (index >= 0) {
            current[index] = config
        } else {
            current.add(0, config)
        }
        val json = gson.toJson(current)
        getPrefs(context).edit().putString(KEY_PROFILES, json).apply()
    }

    /**
     * 删除指定的 Profile
     */
    fun deleteProfile(context: Context, config: C2ServerConfig) {
        val current = getAllProfiles(context).toMutableList()
        current.removeAll {
            it.serverUrl.equals(config.serverUrl, ignoreCase = true) &&
                    it.username.equals(config.username, ignoreCase = true)
        }
        val json = gson.toJson(current)
        getPrefs(context).edit().putString(KEY_PROFILES, json).apply()
    }

    /**
     * 获取是否开启自动连接
     */
    fun isAutoConnectEnabled(context: Context): Boolean {
        return getPrefs(context).getBoolean(KEY_AUTO_CONNECT, false)
    }

    /**
     * 设置是否开启自动连接
     */
    fun setAutoConnectEnabled(context: Context, enabled: Boolean) {
        getPrefs(context).edit().putBoolean(KEY_AUTO_CONNECT, enabled).apply()
    }
}
