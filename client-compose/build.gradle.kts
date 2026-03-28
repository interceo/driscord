import java.nio.file.Paths

plugins {
    kotlin("multiplatform") version "2.1.20"
    kotlin("plugin.serialization") version "2.1.20"
    id("org.jetbrains.compose") version "1.8.0"
    id("org.jetbrains.kotlin.plugin.compose") version "2.1.20"
}

group = "com.driscord"
version = "1.0.0"

// ---------------------------------------------------------------------------
// Output directories — same logic as before.
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

// Path to the built C++ shared libraries (libdriscord_jni.so / libdriscord_capi.so).
val nativeLibDir: String =
    System.getenv("DRISCORD_NATIVE_LIB_DIR")
        ?: Paths.get(rootDir.parent, "build", "client").toString()

layout.buildDirectory.set(file("$buildsRoot/kotlin"))

// ---------------------------------------------------------------------------
// Kotlin Multiplatform targets
// ---------------------------------------------------------------------------
kotlin {
    jvmToolchain(21)

    // ---- JVM target — Compose Desktop (existing fat-JAR build) ----
    jvm()

    // ---- Kotlin/Native — Linux x64 executable ----
    linuxX64 {
        compilations["main"].cinterops {
            create("driscord") {
                defFile(project.file("src/nativeInterop/cinterop/driscord.def"))
                // Resolve the C API header relative to the project root
                includeDirs(project.file("../client/capi"))
            }
        }

        binaries {
            executable("driscord") {
                entryPoint = "com.driscord.main"
                // Link against libdriscord_capi.so; embed rpath so the binary finds it
                // at runtime next to the executable (or at the build output dir).
                linkerOpts(
                    "-L$nativeLibDir",
                    "-ldriscord_capi",
                    "-Wl,-rpath,$nativeLibDir",
                )
            }
        }
    }

    // ---------------------------------------------------------------------------
    // Source sets
    // ---------------------------------------------------------------------------
    sourceSets {
        val commonMain by getting {
            dependencies {
                // compose.runtime is multiplatform — required so the Compose compiler plugin
                // compiles cleanly on non-JVM targets even though they don't use any UI.
                implementation(compose.runtime)
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-core:1.10.1")
                implementation("org.jetbrains.kotlinx:kotlinx-serialization-json:1.8.0")
            }
        }

        val jvmMain by getting {
            // All existing Kotlin/Compose code lives in src/main/kotlin — keep it there.
            kotlin.srcDir("src/main/kotlin")
            resources.srcDirs("src/main/resources")

            dependencies {
                implementation(compose.desktop.currentOs)
                implementation(compose.components.resources)
                implementation("org.jetbrains.kotlinx:kotlinx-coroutines-swing:1.10.1")
            }
        }

        val linuxX64Main by getting {
            // Native-specific sources in src/linuxX64Main/kotlin/ (already default)
        }
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
// Produces builds/client-compose/driscord.jar  (runnable with plain `java -jar`).
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
