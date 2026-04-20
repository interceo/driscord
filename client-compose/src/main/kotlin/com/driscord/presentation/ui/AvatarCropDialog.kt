@file:OptIn(ExperimentalComposeUiApi::class)

package com.driscord.presentation.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.*
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.input.pointer.PointerEventType
import androidx.compose.ui.input.pointer.onPointerEvent
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.*
import androidx.compose.ui.window.Dialog
import com.driscord.driscord_compose.generated.resources.*
import com.driscord.ui.*
import org.jetbrains.compose.resources.stringResource
import org.jetbrains.skia.EncodedImageFormat
import org.jetbrains.skia.Image as SkiaImage
import org.jetbrains.skia.Path as SkiaPath
import org.jetbrains.skia.Rect as SkiaRect
import org.jetbrains.skia.Surface
import androidx.compose.ui.graphics.toComposeImageBitmap
import kotlin.math.roundToInt

private const val PREVIEW_DP     = 300
private const val CIRCLE_FRAC    = 0.43f   // circle radius as fraction of preview width
private const val MAX_ZOOM       = 3f
private const val OUTPUT_SIZE    = 256      // exported PNG size in px

/**
 * A crop-and-preview dialog shown after the user picks an image file.
 * The user can pan (drag) and zoom (scroll wheel / pinch / slider) the image
 * within a fixed circular crop boundary. On confirm, the visible circle region
 * is rendered to a 256×256 circular PNG.
 */
@Composable
fun AvatarCropDialog(
    imageBytes: ByteArray,
    onConfirm : (ByteArray) -> Unit,
    onDismiss : () -> Unit,
) {
    val skiaImage   = remember { SkiaImage.makeFromEncoded(imageBytes) }
    val imageBitmap = remember { skiaImage.toComposeImageBitmap() }

    val density        = LocalDensity.current
    val previewSizePx  = remember(density) { with(density) { PREVIEW_DP.dp.toPx() } }
    val circleRadiusPx = previewSizePx * CIRCLE_FRAC

    val imgW = skiaImage.width.toFloat()
    val imgH = skiaImage.height.toFloat()

    // Minimum scale: the image just covers the circle (no black bars visible)
    val baseScale = remember(imgW, imgH, circleRadiusPx) {
        maxOf(circleRadiusPx * 2f / imgW, circleRadiusPx * 2f / imgH)
    }

    // zoomFactor ∈ [1, MAX_ZOOM] on top of baseScale
    // offsetX/Y: displacement of image centre from preview centre, in screen px
    var zoomFactor by remember { mutableStateOf(1f) }
    var offsetX    by remember { mutableStateOf(0f) }
    var offsetY    by remember { mutableStateOf(0f) }

    // Clamp offset so the circle is always fully covered by the image
    fun clampOffset(ox: Float, oy: Float, zoom: Float): Pair<Float, Float> {
        val s    = baseScale * zoom
        val maxX = (imgW * s / 2f - circleRadiusPx).coerceAtLeast(0f)
        val maxY = (imgH * s / 2f - circleRadiusPx).coerceAtLeast(0f)
        return ox.coerceIn(-maxX, maxX) to oy.coerceIn(-maxY, maxY)
    }

    Dialog(onDismissRequest = onDismiss) {
        Surface(
            modifier = Modifier.width(340.dp),
            shape    = RoundedCornerShape(8.dp),
            color    = VoiceBg,
            elevation = 16.dp,
        ) {
            Column(
                modifier              = Modifier.padding(16.dp),
                verticalArrangement   = Arrangement.spacedBy(12.dp),
                horizontalAlignment   = Alignment.CenterHorizontally,
            ) {

                Text(
                    text       = stringResource(Res.string.edit_avatar),
                    color      = TextPrimary,
                    fontSize   = 15.sp,
                    fontWeight = FontWeight.SemiBold,
                )

                // ── Crop preview ──────────────────────────────────────────
                Canvas(
                    modifier = Modifier
                        .size(PREVIEW_DP.dp)
                        .background(Color.Black)
                        .clipToBounds()
                        // Drag / pinch-zoom
                        .pointerInput(Unit) {
                            detectTransformGestures { _, pan, zoom, _ ->
                                val newZoom = (zoomFactor * zoom).coerceIn(1f, MAX_ZOOM)
                                val (nx, ny) = clampOffset(
                                    offsetX + pan.x, offsetY + pan.y, newZoom,
                                )
                                zoomFactor = newZoom
                                offsetX    = nx
                                offsetY    = ny
                            }
                        }
                        // Scroll-wheel zoom
                        .onPointerEvent(PointerEventType.Scroll) { evt ->
                            val delta  = evt.changes.firstOrNull()?.scrollDelta?.y ?: 0f
                            val factor = if (delta < 0f) 1.1f else 0.9f
                            val newZoom = (zoomFactor * factor).coerceIn(1f, MAX_ZOOM)
                            val (nx, ny) = clampOffset(offsetX, offsetY, newZoom)
                            zoomFactor = newZoom
                            offsetX    = nx
                            offsetY    = ny
                        },
                ) {
                    val s  = baseScale * zoomFactor
                    val cx = size.width  / 2f
                    val cy = size.height / 2f
                    val cr = size.width  * CIRCLE_FRAC  // circle radius in canvas px

                    val imgLeft = cx + offsetX - imgW * s / 2f
                    val imgTop  = cy + offsetY - imgH * s / 2f

                    // 1. Draw scaled image
                    drawImage(
                        image         = imageBitmap,
                        srcOffset     = IntOffset.Zero,
                        srcSize       = IntSize(imageBitmap.width, imageBitmap.height),
                        dstOffset     = IntOffset(imgLeft.roundToInt(), imgTop.roundToInt()),
                        dstSize       = IntSize((imgW * s).roundToInt(), (imgH * s).roundToInt()),
                        filterQuality = FilterQuality.Medium,
                    )

                    // 2. Semi-transparent dark overlay with a circular hole
                    drawIntoCanvas { canvas ->
                        canvas.saveLayer(Rect(0f, 0f, size.width, size.height), Paint())
                        // Fill layer with dark colour
                        canvas.drawRect(
                            Rect(0f, 0f, size.width, size.height),
                            Paint().also { it.color = Color.Black.copy(alpha = 0.65f) },
                        )
                        // Punch out the circle (BlendMode.Clear → transparent hole)
                        canvas.drawCircle(
                            Offset(cx, cy),
                            cr,
                            Paint().also { it.blendMode = BlendMode.Clear },
                        )
                        canvas.restore()
                    }

                    // 3. Circle border
                    drawCircle(
                        color  = Color.White.copy(alpha = 0.85f),
                        radius = cr,
                        style  = Stroke(width = 2f),
                    )
                }

                // ── Zoom slider ───────────────────────────────────────────
                Column(modifier = Modifier.fillMaxWidth()) {
                    Text(
                        text     = stringResource(Res.string.scale),
                        color    = TextMuted,
                        fontSize = 10.sp,
                        modifier = Modifier.padding(start = 4.dp),
                    )
                    Slider(
                        value         = (zoomFactor - 1f) / (MAX_ZOOM - 1f),
                        onValueChange = { v ->
                            val newZoom = 1f + v * (MAX_ZOOM - 1f)
                            val (nx, ny) = clampOffset(offsetX, offsetY, newZoom)
                            zoomFactor = newZoom
                            offsetX    = nx
                            offsetY    = ny
                        },
                        colors = SliderDefaults.colors(
                            thumbColor         = Blurple,
                            activeTrackColor   = Blurple,
                            inactiveTrackColor = FieldBg,
                        ),
                    )
                }

                // ── Buttons ───────────────────────────────────────────────
                Row(
                    modifier              = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.spacedBy(8.dp),
                ) {
                    TextButton(onClick = onDismiss, modifier = Modifier.weight(1f)) {
                        Text(stringResource(Res.string.cancel), color = TextMuted)
                    }
                    Button(
                        onClick = {
                            onConfirm(
                                cropToCircle(
                                    skiaImage, offsetX, offsetY,
                                    baseScale, zoomFactor, previewSizePx, circleRadiusPx,
                                )
                            )
                        },
                        modifier = Modifier.weight(1f),
                        colors   = ButtonDefaults.buttonColors(backgroundColor = Blurple),
                        shape    = RoundedCornerShape(4.dp),
                    ) {
                        Text(stringResource(Res.string.apply), color = Color.White)
                    }
                }
            }
        }
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Off-screen Skia crop
// ────────────────────────────────────────────────────────────────────────────

/**
 * Renders the portion of [source] that is visible inside the crop circle to a
 * square [outputSize]×[outputSize] PNG, clipped to a circle.
 *
 * All float arguments use the same coordinate space as the Canvas in
 * [AvatarCropDialog] — i.e. screen pixels at the device's density.
 */
private fun cropToCircle(
    source        : SkiaImage,
    offsetX       : Float,
    offsetY       : Float,
    baseScale     : Float,
    zoomFactor    : Float,
    previewSizePx : Float,
    circleRadiusPx: Float,
    outputSize    : Int = OUTPUT_SIZE,
): ByteArray {
    val s  = baseScale * zoomFactor
    val cx = previewSizePx / 2f
    val cy = previewSizePx / 2f

    // Top-left of the displayed image in preview coordinates
    val imgLeft = cx + offsetX - source.width  * s / 2f
    val imgTop  = cy + offsetY - source.height * s / 2f

    // Circle bounding box in preview coordinates
    val cLeft = cx - circleRadiusPx
    val cTop  = cy - circleRadiusPx

    // Map circle bounds back to source-image pixel coordinates
    val srcL = ((cLeft                   - imgLeft) / s).coerceIn(0f, source.width.toFloat())
    val srcT = ((cTop                    - imgTop ) / s).coerceIn(0f, source.height.toFloat())
    val srcR = ((cLeft + circleRadiusPx * 2f - imgLeft) / s).coerceIn(0f, source.width.toFloat())
    val srcB = ((cTop  + circleRadiusPx * 2f - imgTop ) / s).coerceIn(0f, source.height.toFloat())

    if (srcR <= srcL || srcB <= srcT) return ByteArray(0)

    val outF    = outputSize.toFloat()
    val surface = Surface.makeRasterN32Premul(outputSize, outputSize)
    surface.canvas.apply {
        val circlePath = SkiaPath().apply { addCircle(outF / 2f, outF / 2f, outF / 2f) }
        clipPath(circlePath)
        drawImageRect(
            source,
            src = SkiaRect.makeLTRB(srcL, srcT, srcR, srcB),
            dst = SkiaRect.makeWH(outF, outF),
        )
    }

    return surface.makeImageSnapshot()
        .encodeToData(EncodedImageFormat.PNG)
        ?.bytes
        ?: ByteArray(0)
}
