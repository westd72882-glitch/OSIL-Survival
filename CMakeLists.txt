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
    src/Core/Random.cpp
    src/Core/Text.cpp
    src/Core/Time.cpp
    src/World/Biome.cpp
    src/World/Environment.cpp
    src/World/Monuments.cpp
    src/World/Resources.cpp
    src/World/World.cpp
    src/World/WorldConfig.cpp
)
target_include_directories(osil_core PUBLIC ${CMAKE_SOURCE_DIR}/src)

find_package(Threads REQUIRED)
target_link_libraries(osil_core PUBLIC Threads::Threads)

# ---------------- Выделенный сервер ----------------
add_executable(osil_server
    src/Server/ConsoleHost.cpp
    src/Server/ServerApp.cpp
    src/Server/main.cpp
)
target_link_libraries(osil_server PRIVATE osil_core)

# ---------------- Утилита предпросмотра карты ----------------
add_executable(osil_mapgen
    src/Tools/mapgen/PngWriter.cpp
    src/Tools/mapgen/main.cpp
)
target_link_libraries(osil_mapgen PRIVATE osil_core)

# ---------------- Тесты ----------------
enable_testing()
add_executable(osil_tests
    tests/TestHarness.cpp
    tests/test_core.cpp
    tests/test_world.cpp
)
target_link_libraries(osil_tests PRIVATE osil_core)
add_test(NAME osil_tests COMMAND osil_tests)
