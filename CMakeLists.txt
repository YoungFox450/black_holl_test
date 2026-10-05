cmake_minimum_required(VERSION 3.16)
project(BlackHoleTest LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- OpenGL ---------------------------------------------------------------
find_package(OpenGL REQUIRED)

# --- GLFW (fenêtre + contexte OpenGL) -------------------------------------
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
    FetchContent_Declare(glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(glfw)
endif()

# --- Exécutable -----------------------------------------------------------
add_executable(blackhole
    src/main.cpp
    src/gl_loader.cpp
    src/shader.cpp
)
target_include_directories(blackhole PRIVATE src)
target_link_libraries(blackhole PRIVATE glfw OpenGL::GL)

# Les shaders sont recopiés à côté de l'exécutable à chaque compilation,
# même quand seul un fichier .vert/.frag a changé.
add_custom_target(copy_shaders ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:blackhole>/shaders
    COMMENT "Copie des shaders")
add_dependencies(copy_shaders blackhole)

if(MSVC)
    target_compile_options(blackhole PRIVATE /W4)
    set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT blackhole)
else()
    target_compile_options(blackhole PRIVATE -Wall -Wextra)
endif()
