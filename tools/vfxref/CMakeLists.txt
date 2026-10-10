# vfxref: runs the Reference to VFX steps from a terminal, for testing and
# for looking at results without the app.
add_executable(vfxref main.cpp)
target_link_libraries(vfxref PRIVATE vfx::editor)
