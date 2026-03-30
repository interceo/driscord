import java.nio.file.Paths
import org.jetbrains.kotlin.gradle.plugin.mpp.KotlinNativeTarget

plugins {
    kotlin("multiplatform") version "2.1.20"
    kotlin("plugin.serialization") version "2.1.20"
    id("org.jetbrains.compose") version "1.8.0"
    id("org.jetbrains.kotlin.plugin.compose") version "2.1.20"
}

group = "com.driscord"
version = "1.0.0"

// ---------------------------------------------------------------------------
// Output directories
// Override via -PbuildsDir / DRISCORD_BUILDS_DIR
//              or -PclientBuildDir / DRISCORD_CLIENT_BUILD_DIR
// ---------------------------------------------------------------------------
val buildsRoot: String = (
    findProperty("buildsDir") as String?
        ?: System.getenv("DRISCORD_BUILDS_DIR")
        ?: Paths.get(rootDir.parent, "builds").toString()
)

val clientBuildDir: String = (
    findProperty("clientBuildDir") as String?
        ?: System.getenv("DRISCORD_CLIENT_BUILD_DIR")
        ?: Paths.get(buildsRoot, "client-compose").toString()
)

// Path to the C++ shared libraries (libdriscord_jni.so / libdriscord_capi.so / .dll / .dylib).
// Set DRISCORD_NATIVE_LIB_DIR to override (points to CMake client output dir).
val nativeLibDir: String =
    System.getenv("DRISCORD_NATIVE_LIB_DIR")
        ?: Paths.get(rootDir.parent, "build", "client").toString()

layout.buildDirectory.set(file("$buildsRoot/kotlin"))

// ---------------------------------------------------------------------------
// Helper: configure a Kotlin/Native target with cinterop + executable
// ---------------------------------------------------------------------------
fun KotlinNativeTarget.configureNativeTarget(extraLinkerOpts: List<String> = emptyList()) {
    compilations["main"].cinterops {
        create("driscord") {
            defFile(project.file("src/nativeInterop/cinterop/driscord.def"))
            includeDirs(project.file("../client/capi"))
        }
    }
    binaries {
        executable("driscord") {
            entryPoint = "com.driscord.main"
            linkerOpts(extraLinkerOpts)
        }
    }
}

// ---------------------------------------------------------------------------
// Kotlin Multiplatform targets
// ---------------------------------------------------------------------------
kotlin {
    jvmToolchain(21)

    // ---- JVM — Compose Desktop ----
    jvm()

    // ---- Kotlin/Native targets ----
    linuxX64 {
        configureNativeTarget(listOf(
            "-L$nativeLibDir", "-ldriscord_capi",
            "-Wl,-rpath,$nativeLibDir",
        ))
    }

    macosX64 {
        configureNativeTarget(listOf(
            "-L$nativeLibDir", "-ldriscord_capi",
            "-Wl,-rpath,$nativeLibDir",
        ))
    }

    macosArm64 {
        configureNativeTarget(listOf(
            "-L$nativeLibDir", "-ldriscord_capi",
            "-Wl,-rpath,$nativeLibDir",
        ))
    }

    mingwX64 {
        configureNativeTarget(listOf(
            // On Windows the DLL is located via PATH; no rpath.
            "-L$nativeLibDir", "-ldriscord_capi",
        ))
    }

    // ---------------------------------------------------------------------------
    // Source sets
    // ---------------------------------------------------------------------------
    sourceSets {
        val commonMain by getting {
            dependencies {
                // compose.runtime is multiplatform — keeps the Compose compiler plugin
                // happy on native targets that don't render any UI.
                implementation(compose.runtime)
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.1")
                implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.8.0")
            }
        }

        val jvmMain by getting {
            // All existing Kotlin/Compose code stays in src/main/kotlin.
            kotlin.srcDir("src/main/kotlin")
            resources.srcDirs("src/main/resources")

            dependencies {
                implementation(compose.desktop.currentOs)
                implementation(compose.components.resources)
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-swing:1.10.1")
            }
        }

        // Shared source set for all Kotlin/Native targets:
        // NativeDriscord.kt (cinterop wrapper) + Main.kt (CLI entry point).
        val nativeMain by creating {
            dependsOn(commonMain)
        }

        val linuxX64Main by getting   { dependsOn(nativeMain) }
        val macosX64Main by getting   { dependsOn(nativeMain) }
        val macosArm64Main by getting { dependsOn(nativeMain) }
        val mingwX64Main by getting   { dependsOn(nativeMain) }
    }
}

// ---------------------------------------------------------------------------
// Compose Desktop application config (JVM target)
// ---------------------------------------------------------------------------
compose.desktop {
    application {
        mainClass = "com.driscord.MainKt"
        jvmArgs("-Djava.library.path=$nativeLibDir")
    }
}

// ---------------------------------------------------------------------------
// fatJar — uber-JAR with all JVM runtime dependencies.
// Produces builds/client-compose/driscord.jar
// ---------------------------------------------------------------------------
tasks.register<Jar>("fatJar") {
    group = "build"
    description = "Assembles a self-contained uber-JAR with all runtime dependencies"

    archiveFileName.set("driscord.jar")
    destinationDirectory.set(file(clientBuildDir))

    manifest {
        attributes(
            "Main-Class" to "com.driscord.MainKt",
            "Implementation-Title" to "driscord",
            "Implementation-Version" to project.version,
        )
    }

    duplicatesStrategy = DuplicatesStrategy.EXCLUDE

    val jvmCompilation = kotlin.jvm().compilations.getByName("main")
    dependsOn(jvmCompilation.compileTaskProvider)

    from(jvmCompilation.runtimeDependencyFiles.map { if (it.isDirectory) it else zipTree(it) })
    from(jvmCompilation.output.allOutputs)

    exclude("META-INF/*.SF", "META-INF/*.DSA", "META-INF/*.RSA")
}
