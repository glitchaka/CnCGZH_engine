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
    add_executable(CnCGZHEngine WIN32
        runtime/src/main.cpp
    )
    target_link_libraries(CnCGZHEngine PRIVATE cncgzh_engine_core user32 gdi32 shell32 opengl32)
    target_compile_definitions(CnCGZHEngine PRIVATE UNICODE _UNICODE NOMINMAX)

    add_executable(CnCGZHStudio WIN32
        studio/src/main.cpp
    )
    target_link_libraries(CnCGZHStudio PRIVATE cncgzh_engine_core user32 gdi32 comdlg32 shell32)
    target_compile_definitions(CnCGZHStudio PRIVATE UNICODE _UNICODE NOMINMAX)
endif()

# EA's original Generals/Zero Hour source is preserved under upstream/ea-generals-zero-hour.
# The new runtime is the executable engine layer. Legacy Generals subsystems are integrated
# behind engine adapters progressively instead of making the editor itself the runtime.
