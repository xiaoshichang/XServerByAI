find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_test(
    NAME xs_python_cluster_ctl_tests
    COMMAND
        "${Python3_EXECUTABLE}"
        -m
        unittest
        discover
        -s
        "${CMAKE_CURRENT_SOURCE_DIR}"
        -p
        "test_*.py"
)

set_tests_properties(
    xs_python_cluster_ctl_tests
    PROPERTIES
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
