package com.driscord.domain.model

data class Server(
    val id: Int,
    val name: String,
    val description: String?,
    val ownerId: Int,
)
