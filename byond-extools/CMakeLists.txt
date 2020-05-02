if (WIN32)
    # Needed for MSVC_RUNTIME_LIBRARY
    cmake_minimum_required(VERSION 3.15.0)
    set(CMAKE_GENERATOR_TOOLSET host=x86)
else()
    cmake_minimum_required(VERSION 3.10.0)
endif()

project(byond-extools)
if (WIN32)
    enable_language(ASM)
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
if (WIN32)
    file(GLOB_RECURSE SRC_FILES
        "${SRC_DIR}/*.cpp"
        "${SRC_DIR}/*.h"
        "${SRC_DIR}/*.hpp"
        "${SRC_DIR}/*.s"
    )
else()
    file(GLOB_RECURSE SRC_FILES
        "${SRC_DIR}/*.cpp"
        "${SRC_DIR}/*.h"
        "${SRC_DIR}/*.hpp"
    )
endif()
set(SRC_FILES ${SRC_FILES} "${SRC_DIR}/third_party/subhook/subhook.c")

add_library(byond-extools SHARED ${SRC_FILES})
source_group(TREE ${SRC_DIR} FILES ${SRC_FILES})
if (WIN32)
    set_property(TARGET byond-extools PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if (MSVC)
        target_compile_options(byond-extools PRIVATE "/MP")
    else()
        target_link_libraries(byond-extools PRIVATE "ws2_32" "psapi" "-static-libgcc" "-static-libstdc++" "-static")
    endif()
    target_compile_definitions(byond-extools PRIVATE WIN32_LEAN_AND_MEAN SUBHOOK_IMPLEMENTATION)
else()
    set_target_properties(byond-extools PROPERTIES COMPILE_OPTIONS "-m32" LINK_FLAGS "-m32")
endif()
