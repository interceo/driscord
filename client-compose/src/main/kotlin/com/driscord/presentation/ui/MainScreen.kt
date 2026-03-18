package com.driscord.presentation.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import com.driscord.presentation.AppIntent
import com.driscord.presentation.AppViewModel
import com.driscord.presentation.viewmodel.AppViewModel

@Composable
fun MainScreen(viewModel: AppViewModel) {
    val state by viewModel.state.collectAsState()
    val onIntent = viewModel::onIntent

    Row(modifier = Modifier.fillMaxSize().background(Color(0xFF313338))) {
        Sidebar(
            state = state,
            onIntent = onIntent,
            onGetPeerVolume = viewModel::getPeerVolume,
        )
        Box(modifier = Modifier.fillMaxSize()) {
            ContentPanel(
                state = state,
                onIntent = onIntent,
                onGetPeerVolume = viewModel::getPeerVolume,
                onStreamVolume = viewModel::getStreamVolume,
                onListTargets = viewModel::listCaptureTargets,
                onGrabThumbnail = viewModel::grabThumbnail,
            )
        }
    }
}