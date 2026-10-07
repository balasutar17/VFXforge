# The core library: data model, commands and undo, the .vfx format, the
# compile step and the CPU particle simulation.
#
# Rule: nothing here may depend on a UI toolkit or a graphics API. Only the
# C++ standard library and the vendored JSON header are on its include path,
# so a stray dependency fails to build.
add_library(vfx_core STATIC
    src/Clock.cpp
    src/CommandStack.cpp
    src/Commands.cpp
    src/Compile.cpp
    src/DetMath.cpp
    src/Document.cpp
    src/Effect.cpp
    src/FileIO.cpp
    src/Id.cpp
    src/JsonWriter.cpp
    src/Metadata.cpp
    src/Path.cpp
    src/Serialize.cpp
    src/Simulation.cpp
    src/Templates.cpp
    src/Value.cpp
)
add_library(vfx::core ALIAS vfx_core)

target_include_directories(vfx_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# SYSTEM keeps third-party headers out of our warning set.
target_include_directories(vfx_core SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
