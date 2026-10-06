cmake_minimum_required(VERSION 3.16)
project(BlackHole LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Type de build" FORCE)
endif()

# --- GLFW : fenêtre + contexte OpenGL --------------------------------------
# On utilise la version installée sur le système si elle existe,
# sinon CMake la télécharge automatiquement depuis GitHub.
find_package(glfw3 3.3 QUIET)
if(NOT glfw3_FOUND)
    message(STATUS "GLFW introuvable sur le système : téléchargement via FetchContent")
    include(FetchContent)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    # Sous Linux on compile seulement le backend X11 (fonctionne aussi sous
    # Wayland via XWayland) : moins de dépendances à installer.
    set(GLFW_BUILD_WAYLAND  OFF CACHE BOOL "Backend Wayland de GLFW")
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(glfw)
endif()

# --- glad : chargeur des fonctions OpenGL 3.3 core (fichiers générés) -------
add_library(glad STATIC external/glad/src/gl.c)
target_include_directories(glad PUBLIC external/glad/include)

# --- Dear ImGui : panneau de contrôle (sources incluses dans external/imgui) --
add_library(imgui STATIC
    external/imgui/imgui.cpp
    external/imgui/imgui_demo.cpp
    external/imgui/imgui_draw.cpp
    external/imgui/imgui_tables.cpp
    external/imgui/imgui_widgets.cpp
    external/imgui/backends/imgui_impl_glfw.cpp
    external/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC external/imgui external/imgui/backends)
target_link_libraries(imgui PUBLIC glfw)
if(MSVC)
    target_compile_options(imgui PRIVATE /utf-8)
endif()

# --- Exécutable --------------------------------------------------------------
add_executable(blackhole
    src/main.cpp
    src/shader.cpp
    src/ui.cpp
    src/ui_kit.cpp
    src/star.cpp
    src/activity.cpp
    src/ui_activity.cpp
    src/asteroids.cpp
    src/ui_compact.cpp
    src/ui_asteroids.cpp
    src/ui_simulation.cpp
    src/ui_keys.cpp
    src/binary.cpp
    src/binary_gfx.cpp
    src/ui_binary.cpp
    src/sky_image.cpp
    src/sky_loaders.cpp
    src/ui_sky.cpp
)
target_link_libraries(blackhole PRIVATE glfw glad imgui)
# Lecture des images du fond de ciel (en-têtes seuls) : stb_image et tinyexr.
target_include_directories(blackhole PRIVATE external/stb external/tinyexr)
# Code tiers : pas d'avertissements.
if(MSVC)
    set_source_files_properties(src/sky_loaders.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(src/sky_loaders.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

# Les shaders sont lus depuis les sources : on peut les modifier et
# appuyer sur R pour les recharger sans recompiler.
target_compile_definitions(blackhole PRIVATE
    BH_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    BH_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

# Exécutable toujours dans build/bin (même avec Visual Studio multi-config),
# pour que .vscode/launch.json le trouve.
set_target_properties(blackhole PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>")

if(MSVC)
    # /utf-8 : les textes accentués du panneau sont en UTF-8 dans les sources.
    target_compile_options(blackhole PRIVATE /W4 /utf-8)
else()
    target_compile_options(blackhole PRIVATE -Wall -Wextra)
endif()
