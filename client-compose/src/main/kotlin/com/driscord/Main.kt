package com.driscord

import androidx.compose.runtime.remember
import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import com.driscord.data.api.ApiClient
import com.driscord.data.api.AuthRepositoryImpl
import com.driscord.data.api.ServerRepositoryImpl
import com.driscord.data.audio.AudioServiceImpl
import com.driscord.data.config.ConfigRepositoryImpl
import com.driscord.data.connection.ConnectionServiceImpl
import com.driscord.data.video.VideoServiceImpl
import com.driscord.presentation.viewmodel.AppViewModel
import com.driscord.presentation.ui.MainScreen
import kotlin.system.exitProcess

fun main() {
    application {
        val viewModel = remember {
            val configRepo = ConfigRepositoryImpl()
            val cfg = configRepo.config.value
            val apiClient = ApiClient(cfg.apiBaseUrl)
            val authRepo = AuthRepositoryImpl(apiClient)
            val serverRepo = ServerRepositoryImpl(apiClient)
            val connectionSvc = ConnectionServiceImpl(cfg)
            val audioSvc = AudioServiceImpl()
            val videoSvc = VideoServiceImpl(cfg)
            AppViewModel(connectionSvc, audioSvc, videoSvc, configRepo, authRepo, serverRepo)
        }

        Window(
            onCloseRequest = { viewModel.close(); exitApplication() },
            title = "Driscord",
            state = rememberWindowState(width = 960.dp, height = 640.dp),
        ) {
            MainScreen(viewModel)
        }
    }
    // Compose's application() returned — window closed. Force JVM exit to
    // terminate any JNI-attached native threads (WebRTC/libdatachannel/audio)
    // that may still be alive and would otherwise block shutdown.
    exitProcess(0)
}
