package com.example.adaptixapp.data

import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.SharedPreferences
import android.media.AudioManager
import android.media.RingtoneManager
import android.media.ToneGenerator
import android.os.Build
import androidx.core.app.NotificationCompat
import com.example.adaptixapp.MainActivity
import com.example.adaptixapp.R

object NotificationHelper {

    private const val PREF_NAME = "adaptix_c2_notifications"
    private const val KEY_SOUND_ENABLED = "sound_enabled"
    private const val KEY_NOTIF_ENABLED = "notif_enabled"
    private const val CHANNEL_ID = "adaptix_c2_alerts"

    private fun getPrefs(context: Context): SharedPreferences {
        return context.getSharedPreferences(PREF_NAME, Context.MODE_PRIVATE)
    }

    fun isSoundEnabled(context: Context): Boolean {
        return getPrefs(context).getBoolean(KEY_SOUND_ENABLED, true)
    }

    fun setSoundEnabled(context: Context, enabled: Boolean) {
        getPrefs(context).edit().putBoolean(KEY_SOUND_ENABLED, enabled).apply()
    }

    fun isNotificationEnabled(context: Context): Boolean {
        return getPrefs(context).getBoolean(KEY_NOTIF_ENABLED, true)
    }

    fun setNotificationEnabled(context: Context, enabled: Boolean) {
        getPrefs(context).edit().putBoolean(KEY_NOTIF_ENABLED, enabled).apply()
    }

    /**
     * 播放提示音 (RingtoneManager 或 ToneGenerator 蜂鸣)
     */
    fun playNotificationTone(context: Context) {
        if (!isSoundEnabled(context)) return
        try {
            val notificationUri = RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION)
            val ringtone = RingtoneManager.getRingtone(context.applicationContext, notificationUri)
            if (ringtone != null) {
                ringtone.play()
            } else {
                playFallbackBeep()
            }
        } catch (e: Exception) {
            playFallbackBeep()
        }
    }

    private fun playFallbackBeep() {
        try {
            val toneG = ToneGenerator(AudioManager.STREAM_NOTIFICATION, 90)
            toneG.startTone(ToneGenerator.TONE_PROP_BEEP, 250)
        } catch (e: Exception) {
            // ignore
        }
    }

    /**
     * 发送主机上线通知 (声音 + 状态栏 Notification)
     */
    fun notifyAgentOnline(context: Context, computerName: String, ip: String) {
        playNotificationTone(context)

        if (!isNotificationEnabled(context)) return

        try {
            val notificationManager = context.getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager

            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                val channel = NotificationChannel(
                    CHANNEL_ID,
                    "Adaptix C2 上线通知",
                    NotificationManager.IMPORTANCE_HIGH
                ).apply {
                    description = "新 Agent/主机上线提醒"
                    enableVibration(true)
                }
                notificationManager.createNotificationChannel(channel)
            }

            val intent = Intent(context, MainActivity::class.java).apply {
                flags = Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_SINGLE_TOP
            }
            val pendingIntent = PendingIntent.getActivity(
                context,
                0,
                intent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )

            val notification = NotificationCompat.Builder(context, CHANNEL_ID)
                .setSmallIcon(R.mipmap.ic_launcher)
                .setContentTitle("Adaptix C2 · 主机上线通知")
                .setContentText("主机 $computerName ($ip) 已成功上线!")
                .setPriority(NotificationCompat.PRIORITY_HIGH)
                .setAutoCancel(true)
                .setContentIntent(pendingIntent)
                .build()

            notificationManager.notify((System.currentTimeMillis() % 10000).toInt(), notification)
        } catch (e: Exception) {
            // ignore notification dispatch error
        }
    }

    /**
     * 收到团队消息通知
     */
    fun notifyChatMessage(context: Context, username: String, message: String) {
        playNotificationTone(context)
    }
}
