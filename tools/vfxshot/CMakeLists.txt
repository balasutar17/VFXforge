# Draws effects and presets into PNG files without a window or a graphics
# card, using the editor layer's reference renderer.
add_executable(vfxshot main.cpp)
target_link_libraries(vfxshot PRIVATE vfx::editor)
