package com.driscord.presentation.ui

import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.ui.Modifier
import androidx.compose.ui.awt.SwingPanel
import com.driscord.jni.NativeDriscord
import java.awt.Canvas
import java.awt.Color
import java.awt.event.ComponentAdapter
import java.awt.event.ComponentEvent
import javax.swing.SwingUtilities

/**
 * Embeds a heavyweight AWT [Canvas] whose native window surface is driven by
 * the C++ [VulkanRenderer].  Frames are rendered directly in C++ — no Skia
 * ImageBitmap round-trip.
 */
@Composable
internal fun VulkanSurface(
    peerId: String,
    modifier: Modifier = Modifier,
) {
    DisposableEffect(peerId) {
        onDispose { NativeDriscord.vulkanDetach(peerId) }
    }

    SwingPanel(
        factory = {
            object : Canvas() {
                private var attached = false

                override fun addNotify() {
                    super.addNotify()
                    background = Color.BLACK
                    // Native peer is now created — JAWT can extract the handle.
                    SwingUtilities.invokeLater {
                        if (width > 0 && height > 0 && !attached) {
                            attached = NativeDriscord.vulkanAttach(peerId, this, width, height)
                        }
                    }
                }

                override fun removeNotify() {
                    if (attached) {
                        NativeDriscord.vulkanDetach(peerId)
                        attached = false
                    }
                    super.removeNotify()
                }
            }.apply {
                addComponentListener(object : ComponentAdapter() {
                    override fun componentResized(e: ComponentEvent) {
                        val c = e.component
                        if (c.width > 0 && c.height > 0) {
                            NativeDriscord.vulkanResize(peerId, c.width, c.height)
                        }
                    }
                })
            }
        },
        modifier = modifier,
    )
}
