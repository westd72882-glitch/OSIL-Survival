// Проект Android-клиента OSIL Survival. Нативный код лежит НЕ здесь, а в общем дереве
// репозитория (src/), и подключается через app/src/main/cpp/CMakeLists.txt — так один и
// тот же код собирается и в APK, и в настольную отладочную сборку.
pluginManagement {
    repositories {
        google()
        mavenCentral()
        gradlePluginPortal()
    }
}
dependencyResolutionManagement {
    repositoriesMode.set(RepositoriesMode.FAIL_ON_PROJECT_REPOS)
    repositories {
        google()
        mavenCentral()
    }
}

rootProject.name = "OSILSurvival"
include(":app")
