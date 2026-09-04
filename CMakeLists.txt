# ==================== СБОРКА OSIL SURVIVAL ====================
# Три цели этапа 1:
#   osil_core   — статическая библиотека: ядро (математика, шум, конфиг, лог, время)
#                 и мир (генерация, биомы, ресурсы, монументы, погода);
#   osil_server — выделенный сервер (консольный, без графики и без SDL);
#   osil_mapgen — утилита предпросмотра карты (PNG + текстовый отчёт);
#   osil_tests  — тесты ядра и мира.
#
# Клиент (SDL2 + OpenGL, движок перенесён из A.N.O.D.E) появится на 5-м этапе отдельной
# целью osil_client — поэтому сервер намеренно НЕ зависит ни от SDL, ни от GL: его должно
# быть можно собрать на голой VPS без иксов и видеодрайверов.
cmake_minimum_required(VERSION 3.16)
project(OSILSurvival LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "тип сборки" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
    add_compile_options(/W4)
else()
    # -ffp-contract=off важен для детерминизма: без него компилятор волен склеивать
    # умножение со сложением в FMA, и высота одной и той же точки на разных машинах
    # разойдётся в последнем бите — а мир обязан совпадать у сервера и клиента.
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -ffp-contract=off)
endif()

# ---------------- Ядро и мир ----------------
add_library(osil_core STATIC
    src/Core/Config.cpp
    src/Core/Json.cpp
    src/Core/Log.cpp
    src/Core/Math.cpp
    src/Core/Noise.cpp
    src/Core/PngWriter.cpp
    src/Core/Random.cpp
    src/Core/Text.cpp
    src/Core/Time.cpp
    src/World/Biome.cpp
    src/World/Blocks.cpp
    src/World/Environment.cpp
    src/World/Monuments.cpp
    src/World/Resources.cpp
    src/World/World.cpp
    src/World/WorldConfig.cpp
    src/World/VoxelWorld.cpp
)
target_include_directories(osil_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
target_link_libraries(osil_core PUBLIC Threads::Threads)

# ---------------- TLS для настольной сборки ----------------
# Нужен ровно для одного: зайти на сервер, который хостинг отдаёт по https (render.com
# и все прочие облака). На Android эту работу делает системный HttpsURLConnection через
# JNI, и OpenSSL там не нужен. Если библиотеки нет — сборка не ломается, просто https
# в этой сборке не открывается.
find_package(OpenSSL QUIET)
if(OpenSSL_FOUND)
    message(STATUS "OSIL: OpenSSL найден — https в клиенте доступен")
else()
    message(STATUS "OSIL: OpenSSL не найден — https в настольной сборке отключён")
endif()

# ---------------- Выделенный сервер ----------------
add_executable(osil_server
    src/Server/ConsoleHost.cpp
    src/Server/NetServer.cpp
    src/Server/ServerApp.cpp
    src/Server/main.cpp
    src/Net/Http.cpp
    src/Net/Protocol.cpp
)
target_link_libraries(osil_server PRIVATE osil_core)
if(OpenSSL_FOUND)
    target_compile_definitions(osil_server PRIVATE OSIL_HAVE_OPENSSL)
    target_link_libraries(osil_server PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()

# ---------------- Утилита предпросмотра карты ----------------
add_executable(osil_mapgen
    src/Tools/mapgen/main.cpp
)
target_link_libraries(osil_mapgen PRIVATE osil_core)

# ---------------- Клиент (настольная сборка для отладки) ----------------
# Боевая платформа — Android: там клиент собирается Gradle+NDK по android/app/src/main/cpp/
# CMakeLists.txt, который тянет SDL2 через FetchContent. Здесь же — сборка того же кода на
# настольной Linux, чтобы ошибки компиляции ловились локально за секунды, а не за 10 минут
# в CI. Если SDL2 в системе нет, цель просто не создаётся и остальной проект собирается.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET sdl2)
    pkg_check_modules(SDL2_IMAGE QUIET SDL2_image)
    pkg_check_modules(SDL2_TTF QUIET SDL2_ttf)
    pkg_check_modules(SDL2_MIXER QUIET SDL2_mixer)
endif()

if(SDL2_FOUND AND SDL2_IMAGE_FOUND AND SDL2_TTF_FOUND AND SDL2_MIXER_FOUND)
    add_executable(osil_client
        src/Engine/Core/Audio.cpp
        src/Engine/Core/Console.cpp
        src/Engine/Core/Paths.cpp
        src/Engine/Core/Settings.cpp
        src/Engine/Core/Window.cpp
        src/Engine/Render/AssetLoader.cpp
        src/Engine/Render/BlockTextures.cpp
        src/Engine/Render/Framebuffer.cpp
        src/Engine/Render/Mesh.cpp
        src/Engine/Render/Primitives.cpp
        src/Engine/Render/Shaders.cpp
        src/Engine/Render/VoxelChunks.cpp
        src/Engine/Render/UIDraw.cpp
        src/Engine/Render/UIStyle.cpp
        src/Client/GameClient.cpp
        src/Client/Inventory.cpp
        src/Client/NetClient.cpp
        src/Net/Http.cpp
        src/Net/Protocol.cpp
        src/Client/Survivor.cpp
        src/Client/TouchControls.cpp
        src/Client/TouchControlsRender.cpp
        src/Client/main.cpp
    )
    target_include_directories(osil_client PRIVATE
        ${SDL2_INCLUDE_DIRS} ${SDL2_IMAGE_INCLUDE_DIRS} ${SDL2_TTF_INCLUDE_DIRS} ${SDL2_MIXER_INCLUDE_DIRS})
    target_link_libraries(osil_client PRIVATE osil_core
        ${SDL2_LIBRARIES} ${SDL2_IMAGE_LIBRARIES} ${SDL2_TTF_LIBRARIES} ${SDL2_MIXER_LIBRARIES} GLESv2)
    if(OpenSSL_FOUND)
        target_compile_definitions(osil_client PRIVATE OSIL_HAVE_OPENSSL)
        target_link_libraries(osil_client PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    endif()
    message(STATUS "OSIL: настольная сборка клиента включена")
else()
    message(STATUS "OSIL: SDL2 не найдена — цель osil_client пропущена (Android-сборка не затронута)")
endif()

# ---------------- Тесты ----------------
enable_testing()
set(OSIL_TEST_SOURCES
    tests/TestHarness.cpp
    tests/test_core.cpp
    tests/test_world.cpp
    tests/test_voxels.cpp
    tests/test_assets.cpp
    tests/test_survivor.cpp
    src/Client/Survivor.cpp
    src/Client/Inventory.cpp
)
# Тесту набора картинок нужен путь к репозиторию: сами файлы лежат рядом с исходниками,
# а тест запускается из каталога сборки.
set_source_files_properties(tests/test_assets.cpp PROPERTIES
    COMPILE_DEFINITIONS "OSIL_REPO_DIR=\"${CMAKE_SOURCE_DIR}\"")
# Тесты сенсорного управления требуют только типы событий SDL и сами настройки —
# ни окна, ни графики. Если SDL2 в системе есть, они собираются вместе с остальными.
if(SDL2_FOUND)
    list(APPEND OSIL_TEST_SOURCES
        tests/test_input.cpp
        src/Client/TouchControls.cpp
        src/Engine/Core/Settings.cpp
        src/Engine/Core/Paths.cpp
    )
endif()

add_executable(osil_tests ${OSIL_TEST_SOURCES})
target_link_libraries(osil_tests PRIVATE osil_core)
if(SDL2_FOUND)
    target_include_directories(osil_tests PRIVATE ${SDL2_INCLUDE_DIRS})
    target_link_libraries(osil_tests PRIVATE ${SDL2_LIBRARIES})
endif()
add_test(NAME osil_tests COMMAND osil_tests)
