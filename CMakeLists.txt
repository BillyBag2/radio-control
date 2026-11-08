cmake_minimum_required(VERSION 3.16)

project(radio_control LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# Keep debug symbols alongside full release optimization for GCC and Clang.
add_compile_options(
    "$<$<AND:$<CONFIG:Release>,$<CXX_COMPILER_ID:GNU,Clang,AppleClang>>:-O3;-g>"
    "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall;-Wextra;-Wpedantic;-Werror;-Wconversion;-Wsign-conversion;-Wshadow;-Wformat=2;-Wundef>"
    "$<$<CXX_COMPILER_ID:MSVC>:/W4;/WX;/permissive->"
)

add_subdirectory(lib)
add_subdirectory(main)

include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
