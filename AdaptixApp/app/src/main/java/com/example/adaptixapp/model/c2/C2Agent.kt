package com.example.adaptixapp.model.c2

data class C2Agent(
    val a_id: String,
    val a_name: String? = null,
    val a_listener: String? = null,
    val a_external_ip: String? = null,
    val a_internal_ip: String? = null,
    val a_domain: String? = null,
    val a_computer: String? = null,
    val a_username: String? = null,
    val a_os: Int? = null,
    val a_os_desc: String? = null,
    val a_process: String? = null,
    val a_pid: String? = null,
    val a_tid: String? = null,
    val a_tags: String? = null,
    val a_mark: String? = null,
    val a_create_time: Long? = null,
    val a_last_tick: Long? = null,
    val a_sleep: Int? = null,
    val a_jitter: Int? = null,
    val a_elevated: Boolean? = null,
    val a_arch: String? = null
) {
    val isOnline: Boolean
        get() {
            val last = a_last_tick ?: return false
            val sleepSec = a_sleep ?: 10
            val timeoutSec = (sleepSec * 3) + 30
            val currentSec = System.currentTimeMillis() / 1000
            return (currentSec - last) < timeoutSec
        }

    val osName: String
        get() = when (a_os) {
            1 -> "Windows"
            2 -> "Linux"
            3 -> "macOS"
            else -> a_os_desc ?: "Unknown OS"
        }
}
