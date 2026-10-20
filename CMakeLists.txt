cmake_minimum_required(VERSION 3.24)
project(CnCGZHStudioV2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "CnCGZH Studio v2 currently targets Windows.")
endif()

include(FetchContent)

# Pinned docking build: the editor is a dockable tool, not a fixed demo UI.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG d88f238d1d363ef387382b2e29ad12cf96f826ac
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(imgui)

# Visual transform gizmos inside the viewport.
FetchContent_Declare(imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG 105e4fde45e4e74aae8973d98750820ef7aca7e2
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(imguizmo)

add_library(cncgzh_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imguizmo_SOURCE_DIR}/ImGuizmo.cpp
)
target_include_directories(cncgzh_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguizmo_SOURCE_DIR}
)
target_compile_definitions(cncgzh_imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS)
target_compile_options(cncgzh_imgui PRIVATE /utf-8)

a dd_executable_placeholder()
