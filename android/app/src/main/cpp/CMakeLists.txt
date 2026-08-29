# ==================== СБОРКА НАТИВНОЙ ЧАСТИ APK ====================
# Собирается libmain.so, которую грузит MainActivity через SDL. Сам код игры лежит НЕ в
# папке android/, а в общем дереве репозитория (src/) — тот же самый, что собирается в
# настольный клиент и в выделенный сервер. Копий исходников нет: путь наверх из
# android/app/src/main/cpp — это пять уровней до корня репозитория.
#
# SDL и её расширения тянутся исходниками через FetchContent. Причина: в Android-сборке
# нет системных библиотек, а держать их копию в репозитории — это мегабайты чужого кода
# и ручное обновление. Плата — время первой сборки в CI (кэш Gradle его потом снимает).
cmake_minimum_required(VERSION 3.22.1)
project(OSILSurvival)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Детерминизм мира: клиент и сервер обязаны получать из одного сида одну и ту же карту,
# а слияние умножения со сложением в FMA даёт расхождение в последнем бите.
add_compile_options(-ffp-contract=off)

set(OSIL_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)

include(FetchContent)

# ---------- SDL2 ----------
FetchContent_Declare(
    SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-2.30.6
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2)

# ---------- SDL2_image ----------
set(SDL2IMAGE_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2IMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2IMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2_image
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
    GIT_TAG release-2.8.2
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2_image)

# ---------- SDL2_ttf ----------
set(SDL2TTF_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2TTF_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2TTF_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2_ttf
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
    GIT_TAG release-2.22.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2_ttf)

# ---------- SDL2_mixer ----------
# Форматы: ogg/wav/mp3 — то, чем пользуется игра; остальное отключено, чтобы не тянуть
# лишние кодеки в APK.
set(SDL2MIXER_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_VENDORED ON CACHE BOOL "" FORCE)
set(SDL2MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3 ON CACHE BOOL "" FORCE)
set(SDL2MIXER_OGG ON CACHE BOOL "" FORCE)
set(SDL2MIXER_WAVE ON CACHE BOOL "" FORCE)
set(SDL2MIXER_FLAC OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_OPUS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    SDL2_mixer
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
    GIT_TAG release-2.8.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL2_mixer)

# ---------- Заголовки в стиле <SDL2/...> ----------
# Игра подключает <SDL2/SDL_image.h>, а репозитории расширений кладут заголовок прямо в
# include/. Собираем зеркало нужной раскладки во временной папке сборки.
set(SDL2_SHIM_DIR "${CMAKE_BINARY_DIR}/sdl2_shim_include")
file(MAKE_DIRECTORY "${SDL2_SHIM_DIR}/SDL2")
file(GLOB SDL2_IMAGE_HEADERS "${sdl2_image_SOURCE_DIR}/include/*.h")
file(GLOB SDL2_TTF_HEADERS   "${sdl2_ttf_SOURCE_DIR}/*.h")
file(GLOB SDL2_MIXER_HEADERS "${sdl2_mixer_SOURCE_DIR}/include/*.h")
file(COPY ${SDL2_IMAGE_HEADERS} DESTINATION "${SDL2_SHIM_DIR}/SDL2")
file(COPY ${SDL2_TTF_HEADERS}   DESTINATION "${SDL2_SHIM_DIR}/SDL2")
file(COPY ${SDL2_MIXER_HEADERS} DESTINATION "${SDL2_SHIM_DIR}/SDL2")

# ---------- Сама игра ----------
# Берём только те части дерева, что нужны клиенту: ядро, мир, движок и клиентский слой.
# Server/ и Tools/ содержат собственные main() и в APK им делать нечего.
file(GLOB_RECURSE OSIL_SOURCES
    ${OSIL_ROOT}/src/Core/*.cpp
    ${OSIL_ROOT}/src/World/*.cpp
    ${OSIL_ROOT}/src/Engine/*.cpp
    ${OSIL_ROOT}/src/Client/*.cpp
)
list(LENGTH OSIL_SOURCES OSIL_SOURCE_COUNT)
message(STATUS "OSIL Survival: собираю ${OSIL_SOURCE_COUNT} исходников из ${OSIL_ROOT}/src")

add_library(main SHARED ${OSIL_SOURCES})

target_include_directories(main PRIVATE
    ${OSIL_ROOT}/src
    "${SDL2_SHIM_DIR}"
    ${sdl2_image_SOURCE_DIR}/include
    ${sdl2_ttf_SOURCE_DIR}
    ${sdl2_mixer_SOURCE_DIR}/include
)

if(DEFINED GAME_APP_NAME)
    target_compile_definitions(main PRIVATE APP_NAME="${GAME_APP_NAME}")
endif()
if(DEFINED GAME_APP_VERSION)
    target_compile_definitions(main PRIVATE APP_VERSION="${GAME_APP_VERSION}")
endif()

add_dependencies(main SDL2_image SDL2_ttf SDL2_mixer)

target_link_libraries(main
    SDL2::SDL2
    SDL2::SDL2main
    SDL2_image::SDL2_image
    SDL2_ttf::SDL2_ttf
    SDL2_mixer::SDL2_mixer
    GLESv3
    EGL
    log
    android
)
