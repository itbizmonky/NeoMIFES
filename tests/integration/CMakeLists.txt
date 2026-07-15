add_executable(neomifes_startup_measure_test
    startup_measure_test.cpp
)

target_link_libraries(neomifes_startup_measure_test
    PRIVATE
        neomifes::compile_options
        GTest::gtest
)

set_target_properties(neomifes_startup_measure_test PROPERTIES
    FOLDER "tests"
)

# Register with CTest, passing NeoMIFES.exe path as argv[1].
add_test(
    NAME    startup_measure
    COMMAND neomifes_startup_measure_test $<TARGET_FILE:NeoMIFES>
)

# Ensure the executable-under-test is built before running the integration test.
set_tests_properties(startup_measure PROPERTIES
    LABELS "integration"
    TIMEOUT 30
)

# render_device_smoke_test - exercises real COM/D3D11/D2D/DXGI device
# creation (HARDWARE->WARP fallback), which the plain unit-test binary
# deliberately avoids. No subprocess spawning needed, so gtest_main suffices
# (unlike startup_measure_test, which needs a custom main() to receive the
# built NeoMIFES.exe path as argv[1]).
add_executable(neomifes_render_device_smoke_test
    render_device_smoke_test.cpp
)

target_link_libraries(neomifes_render_device_smoke_test
    PRIVATE
        neomifes::compile_options
        neomifes::render
        GTest::gtest
        GTest::gtest_main
)

set_target_properties(neomifes_render_device_smoke_test PROPERTIES
    FOLDER "tests"
)

add_test(
    NAME    render_device_smoke
    COMMAND neomifes_render_device_smoke_test
)

set_tests_properties(render_device_smoke PROPERTIES
    LABELS "integration"
    TIMEOUT 30
)
