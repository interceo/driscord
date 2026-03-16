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
            // Exe  — Windows self-contained installer (bundled JRE, no Java required)
            // Msi  — Windows MSI package
            // Deb  — Debian/Ubuntu package
            // Rpm  — Fedora/RHEL package
            targetFormats(
                TargetFormat.Exe,
                TargetFormat.Msi,
                TargetFormat.Deb,
                TargetFormat.Rpm,
                TargetFormat.Dmg,
            )

            packageName = "driscord"
            packageVersion = "1.0.0"
            description = "Driscord desktop client"
            copyright = "© 2026 driscord"

            // Put the finished installers into builds/dist/  (same root as build outputs)
            outputBaseDir.set(file("$buildsRoot/dist"))

            windows {
                // Show a proper name in Add/Remove Programs
                menuGroup = "Driscord"
                // Create a desktop shortcut
                shortcut = true
                // Unique upgrade GUID — change only when you want to break the upgrade chain
                upgradeUuid = "8F3A2D1C-4B5E-4F6A-9C7D-2E0F1A3B5C7D"
                // Bundle the native JNI DLL into the app image
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
