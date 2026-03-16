package com.driscord

import androidx.compose.ui.unit.dp
import androidx.compose.ui.window.Window
import androidx.compose.ui.window.application
import androidx.compose.ui.window.rememberWindowState
import com.driscord.ui.MainScreen

fun main() = application {
    val app = DriscordApp() // reads host/port from env or defaults

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
