cmake_minimum_required(VERSION 3.15.0)
project(byond-extools)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SRC_DIR ${CMAKE_SOURCE_DIR}/src)
file(GLOB_RECURSE SRC_FILES
    "${SRC_DIR}/*.cpp"
    "${SRC_DIR}/*.h"
    "${SRC_DIR}/*.hpp"
)

# get rid of polyhook/asmjit/capstone so we don't compile it every time. we already got the static libs!
foreach (TMP_PATH ${SRC_FILES})
    string (FIND ${TMP_PATH} "polyhook" EXCLUDE_DIR_FOUND)
    if (NOT ${EXCLUDE_DIR_FOUND} EQUAL -1)
        list (REMOVE_ITEM SRC_FILES ${TMP_PATH})
    endif ()
endforeach(TMP_PATH)


add_library(byond-extools SHARED ${SRC_FILES})
source_group(TREE ${SRC_DIR} FILES ${SRC_FILES})
#source_group("core"                 FILES ${CORE})
#source_group("tffi"                 FILES ${TFFI})
if (WIN32)
    set_property(TARGET byond-extools PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_include_directories(byond-extools PUBLIC src/polyhook)
    target_link_directories(byond-extools PUBLIC src/polyhook)
    target_link_libraries(byond-extools asmjit)
    target_link_libraries(byond-extools capstone)
    target_link_libraries(byond-extools PolyHook_2)
else()
    set_target_properties(byond-extools PROPERTIES COMPILE_OPTIONS "-m32" LINK_FLAGS "-m32")
endif()