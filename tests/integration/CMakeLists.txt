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
