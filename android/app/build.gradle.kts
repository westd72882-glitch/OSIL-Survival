plugins {
    id("com.android.application")
}

// Значения из gradle.properties (см. комментарий там же).
val cfgAppName: String = (project.findProperty("appName") as String?) ?: "OSIL Survival"
val cfgAppVersion: String = (project.findProperty("appVersion") as String?) ?: "0.1.0"
// Номер сборки приходит из CI (github.run_number). Локально — единица.
val cfgVersionCode: Int = ((project.findProperty("buildNumber") as String?) ?: "1").toInt()

android {
    namespace = "com.osil.survival"
    compileSdk = 34
    ndkVersion = "26.3.11579264"

    defaultConfig {
        applicationId = "com.osil.survival"
        // 24 (Android 7) — нижняя граница, где GLES 3.0 есть практически на всех
        // устройствах; ниже опускаться смысла нет, там и памяти под мир не хватит.
        minSdk = 24
        targetSdk = 34
        versionCode = cfgVersionCode
        versionName = cfgAppVersion

        resValue("string", "app_name", cfgAppName)

        externalNativeBuild {
            cmake {
                // Имя и версия передаются в C++ как cache-переменные CMake, а не как
                // C-строковые литералы в cppFlags: кавычки не переживают путь
                // Kotlin -> AGP -> CMAKE_CXX_FLAGS -> ninja -> shell.
                arguments += listOf(
                    "-DANDROID_STL=c++_shared",
                    "-DGAME_APP_NAME=$cfgAppName",
                    "-DGAME_APP_VERSION=$cfgAppVersion"
                )
                cppFlags += listOf("-std=c++17")
            }
        }
        ndk {
            // 64-битная сборка — основная; 32-битная оставлена для старых телефонов.
            abiFilters += listOf("arm64-v8a", "armeabi-v7a")
        }
    }

    externalNativeBuild {
        cmake {
            path = file("src/main/cpp/CMakeLists.txt")
            version = "3.22.1"
        }
    }

    buildTypes {
        release {
            isMinifyEnabled = false
        }
    }

    packaging {
        jniLibs {
            useLegacyPackaging = false
        }
    }

    sourceSets {
        getByName("main") {
            // Текстуры и иконки лежат в общей папке assets/ в корне репозитория: их
            // использует и настольная сборка, и APK. Подключаем её как второй источник
            // ассетов, чтобы не держать копию файлов внутри android/.
            assets.srcDirs("src/main/assets", rootProject.file("../assets"))
        }
    }
}
