add_library(catch2_amalgamated STATIC ${PROJECT_SOURCE_DIR}/third_party/catch2/catch_amalgamated.cpp)
target_include_directories(catch2_amalgamated SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party)
if(NOT MSVC)
    target_compile_options(catch2_amalgamated PRIVATE -w)
endif()

add_executable(vfx_tests
    test_clock.cpp
    test_commands.cpp
    test_compile.cpp
    test_detmath.cpp
    test_determinism.cpp
    test_fileio.cpp
    test_fuzz.cpp
    test_ids_paths.cpp
    test_metadata.cpp
    test_random_edits.cpp
    test_serialize.cpp
    test_simulation.cpp
    test_stack.cpp
)
target_link_libraries(vfx_tests PRIVATE vfx::core catch2_amalgamated)
# Tests may look at internals; applications may not.
target_include_directories(vfx_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/src)
target_include_directories(vfx_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party)
target_compile_definitions(vfx_tests PRIVATE
    VFX_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    VFX_SAMPLES_DIR="${PROJECT_SOURCE_DIR}/samples"
)

add_test(NAME core_tests COMMAND vfx_tests)
add_test(NAME cli_validates_samples
    COMMAND vfxcli validate --strict ${PROJECT_SOURCE_DIR}/samples/coin_burst.vfx)
add_test(NAME cli_schema COMMAND vfxcli schema)
add_test(NAME cli_simulates_sample
    COMMAND vfxcli simulate ${PROJECT_SOURCE_DIR}/samples/coin_burst.vfx --every 0.5)
