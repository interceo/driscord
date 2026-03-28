package com.driscord.presentation.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import com.driscord.data.audio.AudioInputDevice
import com.driscord.presentation.viewmodel.AppViewModel
import com.driscord.ui.ContentBg

@Composable
fun MainScreen(viewModel: AppViewModel) {
    val state by viewModel.state.collectAsState()
    val onIntent = viewModel::onIntent

    Row(modifier = Modifier.fillMaxSize().background(ContentBg)) {
        Sidebar(
            state = state,
            onIntent = onIntent,
            onGetPeerVolume = viewModel::getPeerVolume,
            onListInputDevices = viewModel::listInputDevices,
            onListOutputDevices = viewModel::listOutputDevices,
        )
        ContentPanel(
            modifier = Modifier.weight(1f),
            state = state,
            onIntent = onIntent,
            onGetPeerVolume = viewModel::getPeerVolume,
            onStreamVolume = viewModel::getStreamVolume,
            onListTargets = viewModel::listCaptureTargets,
            onGrabThumbnail = viewModel::grabThumbnail,
        )
    }
}
