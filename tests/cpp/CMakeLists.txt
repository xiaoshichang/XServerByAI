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

add_executable(xs_net_packet_tests
    packet_codec_tests.cpp
)

target_link_libraries(xs_net_packet_tests PRIVATE net)

add_test(NAME xs_net_packet_tests COMMAND xs_net_packet_tests)

add_executable(xs_net_message_dispatcher_tests
    message_dispatcher_tests.cpp
)

target_link_libraries(xs_net_message_dispatcher_tests PRIVATE net)

add_test(NAME xs_net_message_dispatcher_tests COMMAND xs_net_message_dispatcher_tests)

add_executable(xs_net_process_heartbeat_codec_tests
    process_heartbeat_codec_tests.cpp
)

target_link_libraries(xs_net_process_heartbeat_codec_tests PRIVATE net)

add_test(NAME xs_net_process_heartbeat_codec_tests COMMAND xs_net_process_heartbeat_codec_tests)

add_executable(xs_main_event_loop_tests
    main_event_loop_tests.cpp
)

target_link_libraries(xs_main_event_loop_tests PRIVATE core)

add_test(NAME xs_main_event_loop_tests COMMAND xs_main_event_loop_tests)

add_executable(xs_ipc_active_connector_tests
    ipc_active_connector_tests.cpp
)

target_link_libraries(xs_ipc_active_connector_tests PRIVATE ipc)

add_test(NAME xs_ipc_active_connector_tests COMMAND xs_ipc_active_connector_tests)

add_executable(xs_ipc_passive_listener_tests
    ipc_passive_listener_tests.cpp
)

target_link_libraries(xs_ipc_passive_listener_tests PRIVATE ipc)

add_test(NAME xs_ipc_passive_listener_tests COMMAND xs_ipc_passive_listener_tests)