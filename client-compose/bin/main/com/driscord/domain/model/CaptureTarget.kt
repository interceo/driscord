package com.driscord.domain.model

import kotlinx.serialization.Serializable

@Serializable
data class CaptureTarget(
    val type: Int,     // 0 = Monitor, 1 = Window
    val id: String,
    val name: String,
    val width: Int,
    val height: Int,
    val x: Int,
    val y: Int,
)