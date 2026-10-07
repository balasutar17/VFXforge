add_executable(vfx_editor_tests
    test_session.cpp
    test_sprite_mesh.cpp
)
target_link_libraries(vfx_editor_tests PRIVATE vfx::editor catch2_amalgamated)
target_compile_definitions(vfx_editor_tests PRIVATE
    VFX_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples"
)
add_test(NAME editor_tests COMMAND vfx_editor_tests)
