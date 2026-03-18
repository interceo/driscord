package com.driscord.data.config
import com.driscord.AppConfig
import kotlinx.coroutines.flow.StateFlow

interface ConfigRepository {
    val config: StateFlow<AppConfig>
    fun save(newConfig: AppConfig)
}