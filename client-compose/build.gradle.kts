import org.jetbrains.compose.desktop.application.dsl.TargetFormat
import java.nio.file.Paths

plugins {
    kotlin("jvm") version "2.1.20"
    kotlin("plugin.serialization") version "2.1.20"
    id("org.jetbrains.compose") version "1.8.0"
    id("org.jetbrains.kotlin.plugin.compose") version "2.1.20"
}

group = "com.driscord"
version = "1.0.0"

// ---------------------------------------------------------------------------
// Output directory: ../builds  (on Windows mapped as Z:\)
// Can be overridden via DRISCORD_BUILDS_DIR env var or -PbuildsDir=<path>
// ---------------------------------------------------------------------------
val buildsRoot: String = (
    findProperty("buildsDir") as String?
        ?: System.getenv("DRISCORD_BUILDS_DIR")
        ?: Paths.get(rootDir.parent, "builds").toString()
)

layout.buildDirectory.set(file("$buildsRoot/kotlin"))

kotlin {
    jvmToolchain(21)
}

dependencies {
    implementation(compose.desktop.currentOs)
    implementation(compose.components.resources)

    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.1")
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-swing:1.10.1")
    implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.8.0")
}

compose.desktop {
    application {
        mainClass = "com.driscord.MainKt"

        nativeDistributions {
            // createDistributable — просто папка с exe + bundled JRE, без установщика
            targetFormats(
                TargetFormat.AppImage, // Linux: portable папка
            )

            packageName = "driscord"
            packageVersion = "1.0.0"
            description = "Driscord desktop client"
            copyright = "© 2026 driscord"

            outputBaseDir.set(file("$buildsRoot/dist"))

            windows {
                val jniDir = System.getenv("DRISCORD_NATIVE_LIB_DIR") ?: ""
                if (jniDir.isNotEmpty()) {
                    appResourcesRootDir.set(file(jniDir))
                }
            }

            linux {
                packageName = "driscord"
                val jniDir = System.getenv("DRISCORD_NATIVE_LIB_DIR") ?: ""
                if (jniDir.isNotEmpty()) {
                    appResourcesRootDir.set(file(jniDir))
                }
            }
        }

        // Directory containing libdriscord_jni.so/.dll — set via DRISCORD_NATIVE_LIB_DIR env var
        jvmArgs("-Djava.library.path=${System.getenv("DRISCORD_NATIVE_LIB_DIR") ?: "."}")
    }
}
