add_executable(xs_core_json_tests
    json_tests.cpp
)

target_link_libraries(xs_core_json_tests PRIVATE core)

add_test(NAME xs_core_json_tests COMMAND xs_core_json_tests)
