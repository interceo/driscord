package com.driscord.data.config

import com.driscord.AppConfig
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow

class ConfigRepositoryImpl : ConfigRepository {
    private val _config = MutableStateFlow(AppConfig.loadDefault())
    override val config: StateFlow<AppConfig> = _config.asStateFlow()

    override fun save(newConfig: AppConfig) {
        val validated = newConfig.validated()
        _config.value = validated
        val path = AppConfig.defaultConfigPath()
        try {
            AppConfig.save(validated, path)
        } catch (e: Exception) {
            println("[config] save failed: ${e.message}")
        }
    }
}