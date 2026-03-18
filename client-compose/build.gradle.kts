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
        // nativeDistributions убраны — используем fatJar + нативные DLL из C++ билда
        jvmArgs("-Djava.library.path=${System.getenv("DRISCORD_NATIVE_LIB_DIR") ?: "."}")
    }
}

// ---------------------------------------------------------------------------
// fatJar — один uber-JAR со всеми Kotlin/Compose зависимостями внутри.
// Запускается: java -jar driscord.jar  (JRE должна быть на машине пользователя,
// но это тот же JDK, что нужен для сборки — никакого 500MB runtime bundle)
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
