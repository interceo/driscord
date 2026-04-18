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
// Output directories
//   buildsRoot      — top-level .builds/ folder (gradle cache, kotlin class output)
//   clientBuildDir  — final staging dir for the distributable client package
//
// Override via:
//   -PbuildsDir=<path>      or  DRISCORD_BUILDS_DIR env var
//   -PclientBuildDir=<path> or  DRISCORD_CLIENT_BUILD_DIR env var
// ---------------------------------------------------------------------------
val buildsRoot: String = (
    findProperty("buildsDir") as String?
        ?: System.getenv("DRISCORD_BUILDS_DIR")
        ?: Paths.get(rootDir.parent, ".builds").toString()
)

val clientBuildDir: String = (
    findProperty("clientBuildDir") as String?
        ?: System.getenv("DRISCORD_CLIENT_BUILD_DIR")
        ?: Paths.get(buildsRoot, "client-compose").toString()
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

    testImplementation(kotlin("test"))
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.10.1")
    testImplementation("app.cash.turbine:turbine:1.2.0")
}

tasks.withType<Test> {
    useJUnitPlatform()
    testLogging {
        events("passed", "failed", "skipped")
        showStandardStreams = true
    }
}

// ---------------------------------------------------------------------------
// Native resource embedding
//
// DRISCORD_NATIVE_LIB_DIR (env var) points to the directory containing the
// platform native library built by CMake.  When set, the library is copied
// into Gradle's processed-resources output so it ends up inside the app JAR
// (fatJar and nativeDistributions both pick it up automatically).
//
// At runtime NativeLoader extracts the bundled library to a temp file and
// loads it — zero path-configuration required from the user.
//
// DRISCORD_EXTRA_NATIVE_DIR may point to a second platform's lib directory
// (e.g. the Windows DLL dir when cross-compiling from Linux), producing a
// cross-platform JAR.
// ---------------------------------------------------------------------------
val generatedNativeRes = layout.buildDirectory.dir("generated-native-resources")

val embedNativeLib by tasks.registering(Copy::class) {
    group = "build"
    description = "Copies the native JNI library into the classpath resources"
    val dirs = listOfNotNull(
        System.getenv("DRISCORD_NATIVE_LIB_DIR"),
        System.getenv("DRISCORD_EXTRA_NATIVE_DIR"),
    ).filter { it.isNotBlank() }
    enabled = dirs.isNotEmpty()
    dirs.forEach { dir ->
        from(dir) { include("libcore.so", "core.dll", "libcore.dylib") }
    }
    into(generatedNativeRes)
}

val embedDefaultConfig by tasks.registering(Copy::class) {
    group = "build"
    description = "Bundles driscord.json as /driscord_defaults.json in the classpath"
    val configFile = rootDir.parentFile.resolve("driscord.json")
    enabled = configFile.exists()
    if (configFile.exists()) {
        from(configFile) { rename { "driscord_defaults.json" } }
    }
    into(generatedNativeRes)
}

// Always add the generated dir as a resources source — it may be empty in
// dev mode (when DRISCORD_NATIVE_LIB_DIR isn't set) which is fine.
sourceSets.main.get().resources.srcDir(generatedNativeRes)
tasks.named("processResources") { dependsOn(embedNativeLib, embedDefaultConfig) }

// ---------------------------------------------------------------------------
// Compose Desktop application
// ---------------------------------------------------------------------------
compose.desktop {
    application {
        mainClass = "com.driscord.MainKt"

        // Dev mode (gradlew run): load native lib directly from the CMake build
        // directory without JAR extraction.  During packaging (DRISCORD_PACKAGING=1)
        // this arg is omitted so no build-machine path is baked into the launcher.
        val isPackaging = System.getenv("DRISCORD_PACKAGING") != null
        val devLibDir = System.getenv("DRISCORD_NATIVE_LIB_DIR")
        if (!isPackaging && devLibDir != null) {
            jvmArgs("-Djava.library.path=$devLibDir")
        }

        nativeDistributions {
            // Linux AppImage — single self-contained executable.
            // Windows Exe   — NSIS installer (requires jpackage on Windows;
            //                 from Linux a portable zip is created by build.sh).
            targetFormats(TargetFormat.AppImage)

            packageName = "driscord"
            packageVersion = project.version.toString()
            description = "P2P voice and screen sharing"
            vendor = "Driscord"
            copyright = "© 2025 Driscord contributors"

            linux {
                packageName = "driscord"
                appRelease = "1"
                menuGroup = "Network"
                val icon = rootDir.parentFile.resolve("packaging/icon.png")
                if (icon.exists()) iconFile.set(icon)
            }

            windows {
                shortcut = true
                menuGroup = "Driscord"
                val icon = rootDir.parentFile.resolve("packaging/icon.ico")
                if (icon.exists()) iconFile.set(icon)
            }
        }
    }
}

// ---------------------------------------------------------------------------
// fatJar — uber-JAR with all Kotlin/Compose dependencies merged in.
// When DRISCORD_NATIVE_LIB_DIR is set, the native library is also embedded
// (via processResources → generatedNativeRes) making the JAR fully
// self-contained for users who already have Java 21 installed.
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
    from(configurations.runtimeClasspath.get().map { if (it.isDirectory) it else zipTree(it) })
    from(sourceSets.main.get().output)

    exclude("META-INF/*.SF", "META-INF/*.DSA", "META-INF/*.RSA")

    dependsOn(tasks.named("compileKotlin"), tasks.named("processResources"))
}
