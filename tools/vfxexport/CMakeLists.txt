# Writes the same Unity (and Unreal) exports as the app's Export button,
# from a terminal: for testing in a real engine, and for batch work.
add_executable(vfxexport main.cpp)
target_link_libraries(vfxexport PRIVATE vfx::editor)
