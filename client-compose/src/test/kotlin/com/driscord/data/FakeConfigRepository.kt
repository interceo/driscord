package com.driscord.data

import com.driscord.AppConfig
import com.driscord.data.config.ConfigRepository
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow

class FakeConfigRepository(initial: AppConfig = AppConfig()) : ConfigRepository {

    override val config = MutableStateFlow(initial)

    var savedConfigs = mutableListOf<AppConfig>()

    override fun save(newConfig: AppConfig) {
        savedConfigs += newConfig
        config.value = newConfig
    }
}
