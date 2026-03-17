add_executable(xs_core_json_tests
    json_tests.cpp
)

target_link_libraries(xs_core_json_tests PRIVATE core)

add_test(NAME xs_core_json_tests COMMAND xs_core_json_tests)

add_executable(xs_core_time_tests
    time_tests.cpp
)

target_link_libraries(xs_core_time_tests PRIVATE core)

add_test(NAME xs_core_time_tests COMMAND xs_core_time_tests)

add_executable(xs_core_loop_tests
    core_loop_tests.cpp
)

target_link_libraries(xs_core_loop_tests PRIVATE core)

add_test(NAME xs_core_loop_tests COMMAND xs_core_loop_tests)

add_executable(xs_net_serialization_tests
    net_serialization_tests.cpp
)

target_link_libraries(xs_net_serialization_tests PRIVATE net)

add_test(NAME xs_net_serialization_tests COMMAND xs_net_serialization_tests)