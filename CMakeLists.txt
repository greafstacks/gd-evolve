cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(GDEvolve VERSION 1.0.0)

file(GLOB SOURCES src/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

function(gdevo_patch_fmt)
    file(GLOB fmt_headers
        "${CMAKE_SOURCE_DIR}/cpm-cache/fmt/*/include/fmt/core.h"
        "${CMAKE_SOURCE_DIR}/cpm-cache/fmt/*/include/fmt/base.h")
    foreach(h ${fmt_headers})
        file(READ "${h}" c)
        string(REPLACE "define FMT_CONSTEVAL consteval" "define FMT_CONSTEVAL" c "${c}")
        file(WRITE "${h}" "${c}")
        message(STATUS "gd-evolve: patched ${h}")
    endforeach()
endfunction()
cmake_language(DEFER CALL gdevo_patch_fmt)

setup_geode_mod(${PROJECT_NAME})
