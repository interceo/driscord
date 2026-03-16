rootProject.name = "driscord-compose"

// ---------------------------------------------------------------------------
// Redirect Gradle dependency/plugin caches into the project's builds folder.
// On Windows the workspace is mounted at Z:\, so builds lands at Z:\builds.
// Override via DRISCORD_BUILDS_DIR env var or -PbuildsDir=<path>.
// ---------------------------------------------------------------------------
val buildsRoot: String =
    (extra.properties["buildsDir"] as String?)
        ?: System.getenv("DRISCORD_BUILDS_DIR")
        ?: rootDir.parentFile.parentFile   // settings file lives in client-compose/
            .resolve("builds")
            .absolutePath

pluginManagement {
    repositories {
        gradlePluginPortal()
        mavenCentral()
        google()
        maven("https://maven.pkg.jetbrains.space/public/p/compose/dev")
    }
}

dependencyResolutionManagement {
    // Store downloaded jars/poms under builds/gradle-cache instead of ~/.gradle
    // The standard way is via GRADLE_USER_HOME; we set it in the build scripts.
    repositories {
        mavenCentral()
        google()
        maven("https://maven.pkg.jetbrains.space/public/p/compose/dev")
    }
}

// Gradle 7.4+ local build cache
buildCache {
    local {
        directory = file("$buildsRoot/gradle-build-cache")
    }
}
