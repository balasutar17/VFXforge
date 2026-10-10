cmake_minimum_required(VERSION 3.24)

project(VFXForge VERSION 0.7.0 DESCRIPTION "VFX Forge" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VFX_BUILD_TESTS "Build the test suite" ON)
option(VFX_BUILD_APP "Build the desktop app (needs Qt 6.5 or newer)" OFF)
option(VFX_SANITIZE "Build with address and undefined-behaviour sanitizers (Clang or GCC)" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
    # Never fuse a multiply and an add into one instruction. Arm processors
    # (Apple Silicon) have that instruction and Intel builds here do not use
    # it, so allowing it makes the same arithmetic round differently on the
    # two. Reproducible effects need identical results on both.
    add_compile_options(-ffp-contract=off)
    if(VFX_SANITIZE)
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        add_link_options(-fsanitize=address,undefined)
    endif()
endif()

add_subdirectory(core)
add_subdirectory(editor)
add_subdirectory(tools/vfxcli)
add_subdirectory(tools/vfxshot)
add_subdirectory(tools/vfxref)
if(VFX_BUILD_APP)
    add_subdirectory(app)
endif()

if(VFX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(editor/tests)
endif()
