package com.driscord.jni

import java.io.File
import java.nio.file.Files
import java.util.jar.JarFile

/**
 * Loads libcore from the classpath (embedded in the JAR) or from
 * java.library.path (dev mode when running via gradlew run).
 */
object NativeLoader {
    private val isWindows = System.getProperty("os.name").startsWith("Windows")
    private val isMac = System.getProperty("os.name").startsWith("Mac")

    private val libName: String =
        when {
            isWindows -> "core.dll"
            isMac -> "libcore.dylib"
            else -> "libcore.so"
        }

    fun load() {
        // 1. Dev mode: try java.library.path first (set via DRISCORD_NATIVE_LIB_DIR)
        try {
            System.loadLibrary("core")
            return
        } catch (_: UnsatisfiedLinkError) {
        }

        // 2. Packaged mode: extract core.dll + any co-shipped runtime DLLs
        //    (FFmpeg avcodec-*.dll etc.) into a shared temp dir, then load
        //    core.dll from there. Windows's loader searches the loading DLL's
        //    own directory first, so dependencies resolve automatically.
        val tempDir = Files.createTempDirectory("driscord_").toFile().apply { deleteOnExit() }

        if (isWindows) {
            extractAllJarResources(tempDir, ".dll")
        }

        val stream =
            NativeLoader::class.java.getResourceAsStream("/$libName")
                ?: error(
                    "Native library '$libName' not found in classpath and not on java.library.path.\n" +
                        "Build the C++ JNI library first: scripts/build.sh (or build.bat on Windows).",
                )
        val coreFile = File(tempDir, libName).also { it.deleteOnExit() }
        // extractAllJarResources may have already written core.dll — overwrite
        // to guarantee a fresh copy before System.load.
        stream.use { input -> coreFile.outputStream().use { out -> input.copyTo(out) } }
        System.load(coreFile.absolutePath)
    }

    // Walks the JAR this class was loaded from and copies every top-level
    // resource with the given suffix into `tempDir`. Only used on Windows to
    // stage FFmpeg DLLs next to core.dll.
    private fun extractAllJarResources(tempDir: File, suffix: String) {
        val src = NativeLoader::class.java.protectionDomain?.codeSource?.location ?: return
        val file = runCatching { File(src.toURI()) }.getOrNull() ?: return
        if (!file.isFile) return  // running from classes dir (dev), nothing to extract

        JarFile(file).use { jar ->
            jar.entries().asSequence()
                .filter { !it.isDirectory && !it.name.contains('/') && it.name.endsWith(suffix) }
                .forEach { entry ->
                    val out = File(tempDir, entry.name).apply { deleteOnExit() }
                    jar.getInputStream(entry).use { input ->
                        out.outputStream().use(input::copyTo)
                    }
                }
        }
    }
}
