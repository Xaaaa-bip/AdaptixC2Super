package com.example.adaptixapp.model.c2

data class C2Listener(
    val l_name: String,
    val l_reg_name: String? = null,
    val l_protocol: String? = null,
    val l_type: String? = null,
    val l_bind_host: String? = null,
    val l_bind_port: String? = null,
    val l_agent_addr: String? = null,
    val l_status: String? = null,
    val a_create_time: Long? = null
)
