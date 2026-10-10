# The editor layer: what an editing session is, with no window attached.
#
# It owns the document, undo, the live program, the simulation and the
# playback clock, and turns a simulated frame into triangles. Like the core,
# it knows nothing about any UI toolkit or graphics API, so every rule of an
# editing session can be tested without opening a window. The desktop app is
# a thin skin over this.
include(${CMAKE_CURRENT_SOURCE_DIR}/EmbedUnityFiles.cmake)
vfx_embed_unity_files(${PROJECT_SOURCE_DIR}/unity ${CMAKE_CURRENT_BINARY_DIR}/UnityFiles.cpp)

add_library(vfx_editor STATIC
    src/Archive.cpp
    src/Audio.cpp
    src/Frames.cpp
    src/Image.cpp
    src/Picture.cpp
    src/Compare.cpp
    src/EffectTools.cpp
    src/Presets.cpp
    src/Reconstruct.cpp
    src/Refine.cpp
    src/Reference.cpp
    src/ReferenceStill.cpp
    src/ReferenceTime.cpp
    src/Session.cpp
    src/Shapes.cpp
    src/SoundLibrary.cpp
    src/SpriteMesh.cpp
    src/UnityExport.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/UnityFiles.cpp
)
add_library(vfx::editor ALIAS vfx_editor)

target_include_directories(vfx_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vfx_editor PUBLIC vfx::core)
target_include_directories(vfx_editor SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
