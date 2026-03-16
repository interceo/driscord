package com.driscord

import java.io.File

/**
 * Loads libdriscord_jni from the classpath (embedded in the JAR) or from
 * java.library.path (dev mode when running via gradlew run).
 */
object NativeLoader {

    private val libName: String = when {
        System.getProperty("os.name").startsWith("Windows") -> "driscord_jni.dll"
        System.getProperty("os.name").startsWith("Mac")     -> "libdriscord_jni.dylib"
        else                                                 -> "libdriscord_jni.so"
    }

    fun load() {
        // 1. Dev mode: try java.library.path first (set via DRISCORD_NATIVE_LIB_DIR)
        try {
            System.loadLibrary("driscord_jni")
            return
        } catch (_: UnsatisfiedLinkError) {}

        // 2. Packaged mode: extract from JAR resources → temp file → load
        val stream = NativeLoader::class.java.getResourceAsStream("/$libName")
            ?: error(
                "Native library '$libName' not found in classpath and not on java.library.path.\n" +
                "Build the C++ JNI library first: scripts/build.sh (or build.bat on Windows)."
            )

        val temp = File.createTempFile("driscord_", "_$libName").also { it.deleteOnExit() }
        stream.use { it.copyTo(temp.outputStream()) }
        System.load(temp.absolutePath)
    }
}
