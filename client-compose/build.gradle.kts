import java.nio.file.Paths
import org.jetbrains.compose.desktop.application.dsl.TargetFormat

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
//   buildsRoot      — top-level builds/ folder (gradle cache, kotlin class output)
//   clientBuildDir  — final staging dir for the distributable client package
//
// Override via:
//   -PbuildsDir=<path>      or  DRISCORD_BUILDS_DIR env var
//   -PclientBuildDir=<path> or  DRISCORD_CLIENT_BUILD_DIR env var
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

val nativeLibDir: String = (
    findProperty("nativeLibDir") as String?
        ?: System.getenv("DRISCORD_NATIVE_LIB_DIR")
        ?: Paths.get(rootDir.parent, "build", "client").toString()
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
            packageName = "Driscord"
            packageVersion = "1.0.0"
            description = "Driscord Voice"

            // Files in appResources/windows/ are placed into the app/ directory
            // alongside the JARs — $APPDIR points there at runtime.
            appResourcesRootDir.set(project.layout.projectDirectory.dir("src/main/appResources"))

            // $APPDIR is a jpackage macro expanded to the app/ dir at runtime.
            jvmArgs("-Djava.library.path=\$APPDIR")

            windows {
                menuGroup = "Driscord"
                shortcut = true
                dirChooser = true
            }
        }
    }
}

// For `./gradlew run` during development — override java.library.path with local build dir.
tasks.withType<JavaExec>().configureEach {
    if (name == "run") {
        jvmArgs("-Djava.library.path=$nativeLibDir")
    }
}

// ---------------------------------------------------------------------------
// copyNativeDlls — stage driscord_jni.dll into appResources before packaging.
// FFmpeg is linked statically into the DLL, so no extra DLLs are needed.
// ---------------------------------------------------------------------------
tasks.register<Copy>("copyNativeDlls") {
    group = "build"
    description = "Copies driscord_jni.dll into appResources for native distribution"

    from(nativeLibDir) {
        include("driscord_jni.dll")
    }
    into(project.layout.projectDirectory.dir("src/main/appResources/windows"))
}

afterEvaluate {
    listOf("prepareAppResources", "createDistributable", "createReleaseDistributable",
           "packageExe", "packageMsi", "packageReleaseExe", "packageReleaseMsi")
        .mapNotNull { tasks.findByName(it) }
        .forEach { it.dependsOn("copyNativeDlls") }
}

// ---------------------------------------------------------------------------
// fatJar — один uber-JAR со всеми Kotlin/Compose зависимостями внутри.
// Используется для Linux-билда (build.sh). На Windows используется createDistributable.
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

    // Merge all runtime deps into one jar, skip duplicate META-INF signatures
    duplicatesStrategy = DuplicatesStrategy.EXCLUDE
    from(configurations.runtimeClasspath.get().map { if (it.isDirectory) it else zipTree(it) })
    from(sourceSets.main.get().output)

    exclude("META-INF/*.SF", "META-INF/*.DSA", "META-INF/*.RSA")

    dependsOn(tasks.named("compileKotlin"))
}
