# The editor layer: what an editing session is, with no window attached.
#
# It owns the document, undo, the live program, the simulation and the
# playback clock, and turns a simulated frame into triangles. Like the core,
# it knows nothing about any UI toolkit or graphics API, so every rule of an
# editing session can be tested without opening a window. The desktop app is
# a thin skin over this.
add_library(vfx_editor STATIC
    src/Session.cpp
    src/SpriteMesh.cpp
)
add_library(vfx::editor ALIAS vfx_editor)

target_include_directories(vfx_editor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vfx_editor PUBLIC vfx::core)
