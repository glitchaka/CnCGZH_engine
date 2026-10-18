cmake_minimum_required(VERSION 3.20)
project(CnCGZHEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cncgzh_engine_core STATIC
    engine/core/BlockGraph.cpp
)
target_include_directories(cncgzh_engine_core PUBLIC engine)

if(WIN32)
    add_executable(CnCGZHStudio WIN32
        studio/src/main.cpp
    )
    target_link_libraries(CnCGZHStudio PRIVATE cncgzh_engine_core user32 gdi32 comdlg32)
    target_compile_definitions(CnCGZHStudio PRIVATE UNICODE _UNICODE NOMINMAX)
endif()

# The preserved EA source is intentionally not built by this top-level project.
# It lives under upstream/ and is progressively wrapped/replaced by engine adapters.
