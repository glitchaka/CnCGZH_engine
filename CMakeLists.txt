cmake_minimum_required(VERSION 3.24)
project(CnCGZHStudio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT WIN32)
    message(FATAL_ERROR "CnCGZH Studio currently targets Windows.")
endif()

include(FetchContent)

FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG d88f238d1d363ef387382b2e29ad12cf96f826ac
    GIT_SHALLOW FALSE
)
FetchContent_MakeAvailable(imgui)

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
    ${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp
)
target_include_directories(cncgzh_imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${imguizmo_SOURCE_DIR}/src
)
target_compile_definitions(cncgzh_imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(cncgzh_imgui PRIVATE /utf-8)
target_link_libraries(cncgzh_imgui PUBLIC d3d11 dxgi d3dcompiler)

add_executable(CnCGZHStudio WIN32
    editor/src/main.cpp
    editor/src/StudioModel.h
)
target_include_directories(CnCGZHStudio PRIVATE editor/src)
target_link_libraries(CnCGZHStudio PRIVATE cncgzh_imgui d3d11 dxgi d3dcompiler user32 gdi32 shell32)
target_compile_definitions(CnCGZHStudio PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(CnCGZHStudio PRIVATE /utf-8 /W4 /permissive-)

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT CnCGZHStudio)
