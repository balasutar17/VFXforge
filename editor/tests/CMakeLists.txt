add_executable(vfx_editor_tests
    test_archive.cpp
    test_frames.cpp
    test_image.cpp
    test_presets.cpp
    test_session.cpp
    test_shapes.cpp
    test_sprite_mesh.cpp
    test_unity.cpp
)
target_link_libraries(vfx_editor_tests PRIVATE vfx::editor catch2_amalgamated)
target_compile_definitions(vfx_editor_tests PRIVATE
    VFX_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples"
    VFX_UNITY_DIR="${PROJECT_SOURCE_DIR}/unity"
    VFX_EDITOR_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
add_test(NAME editor_tests COMMAND vfx_editor_tests)
