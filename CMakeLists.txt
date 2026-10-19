cmake_minimum_required(VERSION 3.20)
project(CnCGZHEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cncgzh_engine_core STATIC
    engine/core/BlockGraph.cpp
    engine/runtime/Runtime.cpp
)
target_include_directories(cncgzh_engine_core PUBLIC engine)

if(WIN32)
    add_custom_target(cncgzh_assets ALL
        COMMAND ${CMAKE_COMMAND}
            -DCNCGZH_SOURCE_DIR=${CMAKE_SOURCE_DIR}
            -DCNCGZH_BINARY_DIR=${CMAKE_BINARY_DIR}
            -P ${CMAKE_SOURCE_DIR}/cmake/FetchKayKit.cmake
        COMMENT "Preparing built-in KayKit sample assets"
        VERBATIM
    )

    add_executable(CnCGZHEngine WIN32
        runtime/src/PlayerMain.cpp
    )
    target_link_libraries(CnCGZHEngine PRIVATE cncgzh_engine_core user32 gdi32 shell32 opengl32)
    target_compile_definitions(CnCGZHEngine PRIVATE UNICODE _UNICODE NOMINMAX)
    target_compile_options(CnCGZHEngine PRIVATE /utf-8)
    add_dependencies(CnCGZHEngine cncgzh_assets)

    add_executable(CnCGZHStudio WIN32
        studio/src/main.cpp
    )
    target_link_libraries(CnCGZHStudio PRIVATE cncgzh_engine_core user32 gdi32 comctl32 shell32 opengl32 glu32)
    target_compile_definitions(CnCGZHStudio PRIVATE UNICODE _UNICODE NOMINMAX)
    target_compile_options(CnCGZHStudio PRIVATE /utf-8)
    add_dependencies(CnCGZHStudio cncgzh_assets)

    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    set(USE_EXTERNAL_GLFW OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
        URL https://github.com/raysan5/raylib/archive/refs/tags/6.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP OFF
    )
    FetchContent_MakeAvailable(raylib)

    add_executable(CnCGZHCharacterLab WIN32
        tools/character_lab/CharacterLab.cpp
    )
    target_link_libraries(CnCGZHCharacterLab PRIVATE raylib shell32 user32)
    target_compile_definitions(CnCGZHCharacterLab PRIVATE UNICODE _UNICODE NOMINMAX)
    target_compile_options(CnCGZHCharacterLab PRIVATE /utf-8 /FI${CMAKE_SOURCE_DIR}/tools/character_lab/WinRaylibCompat.h)
    add_dependencies(CnCGZHCharacterLab cncgzh_assets)
endif()

# EA's original Generals/Zero Hour source is preserved under upstream/ea-generals-zero-hour.
# The runtime and Studio use the build/project root containing assets + projects, so the
# bundled sample content works from a clean clone without manual asset import.
