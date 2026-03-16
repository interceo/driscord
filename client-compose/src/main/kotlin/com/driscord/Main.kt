package com.driscord

import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import com.driscord.ui.MainScreen

fun main() = application {
    val config = AppConfig.loadDefault()
    val app = DriscordApp(config)

    Window(
        onCloseRequest = {
            app.close()
            exitApplication()
        },
        title = "Driscord",
        state = rememberWindowState(width = 960.dp, height = 640.dp),
    ) {
        MainScreen(app)
    }
}
