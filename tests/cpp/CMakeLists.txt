###############################
# xs_core_json_tests
###############################
add_executable(xs_core_json_tests
    json_tests.cpp
)

target_link_libraries(xs_core_json_tests PRIVATE core)

add_test(NAME xs_core_json_tests COMMAND xs_core_json_tests)

###############################
# xs_core_time_tests
###############################
add_executable(xs_core_time_tests
    time_tests.cpp
)

target_link_libraries(xs_core_time_tests PRIVATE core)

add_test(NAME xs_core_time_tests COMMAND xs_core_time_tests)

###############################
# xs_core_loop_tests
###############################
add_executable(xs_core_loop_tests
    core_loop_tests.cpp
)

target_link_libraries(xs_core_loop_tests PRIVATE core)

add_test(NAME xs_core_loop_tests COMMAND xs_core_loop_tests)

###############################
# xs_net_serialization_tests
###############################
add_executable(xs_net_serialization_tests
    net_serialization_tests.cpp
)

target_link_libraries(xs_net_serialization_tests PRIVATE net)

add_test(NAME xs_net_serialization_tests COMMAND xs_net_serialization_tests)

###############################
# xs_net_packet_tests
###############################
add_executable(xs_net_packet_tests
    packet_codec_tests.cpp
)

target_link_libraries(xs_net_packet_tests PRIVATE net)

add_test(NAME xs_net_packet_tests COMMAND xs_net_packet_tests)

###############################
# xs_net_message_dispatcher_tests
###############################
add_executable(xs_net_message_dispatcher_tests
    message_dispatcher_tests.cpp
)

target_link_libraries(xs_net_message_dispatcher_tests PRIVATE net)

add_test(NAME xs_net_message_dispatcher_tests COMMAND xs_net_message_dispatcher_tests)

###############################
# xs_net_heartbeat_codec_tests
###############################
add_executable(xs_net_heartbeat_codec_tests
    heartbeat_codec_tests.cpp
)

target_link_libraries(xs_net_heartbeat_codec_tests PRIVATE net)

add_test(NAME xs_net_heartbeat_codec_tests COMMAND xs_net_heartbeat_codec_tests)

###############################
# xs_net_register_codec_tests
###############################
add_executable(xs_net_register_codec_tests
    register_codec_tests.cpp
)

target_link_libraries(xs_net_register_codec_tests PRIVATE net)

add_test(NAME xs_net_register_codec_tests COMMAND xs_net_register_codec_tests)

###############################
# xs_net_cluster_control_codec_tests
###############################
add_executable(xs_net_cluster_control_codec_tests
    cluster_control_codec_tests.cpp
)

target_link_libraries(xs_net_inner_cluster_codec_tests PRIVATE net)

add_test(NAME xs_net_inner_cluster_codec_tests COMMAND xs_net_inner_cluster_codec_tests)

###############################
# xs_main_event_loop_tests
###############################
add_executable(xs_main_event_loop_tests
    main_event_loop_tests.cpp
)

target_link_libraries(xs_main_event_loop_tests PRIVATE core)

add_test(NAME xs_main_event_loop_tests COMMAND xs_main_event_loop_tests)

###############################
# xs_ipc_active_connector_tests
###############################
add_executable(xs_ipc_active_connector_tests
    ipc_active_connector_tests.cpp
)

target_link_libraries(xs_ipc_active_connector_tests PRIVATE ipc)

add_test(NAME xs_ipc_active_connector_tests COMMAND xs_ipc_active_connector_tests)

###############################
# xs_ipc_passive_listener_tests
###############################
add_executable(xs_ipc_passive_listener_tests
    ipc_passive_listener_tests.cpp
)

target_link_libraries(xs_ipc_passive_listener_tests PRIVATE ipc)

add_test(NAME xs_ipc_passive_listener_tests COMMAND xs_ipc_passive_listener_tests)

###############################
# xs_node_create_helper_tests
###############################
add_executable(xs_node_create_helper_tests
    node_create_helper_tests.cpp
)

target_link_libraries(xs_node_create_helper_tests PRIVATE node)

add_test(NAME xs_node_create_helper_tests COMMAND xs_node_create_helper_tests)

###############################
# xs_node_gm_node_tests
###############################
add_executable(xs_node_gm_node_tests
    node_gm_node_tests.cpp
)

target_link_libraries(xs_node_gm_node_tests PRIVATE node ipc)

add_test(NAME xs_node_gm_node_tests COMMAND xs_node_gm_node_tests)

###############################
# xs_node_gm_control_service_tests
###############################
add_executable(xs_node_gm_control_service_tests
    node_gm_control_service_tests.cpp
)

target_link_libraries(xs_node_gate_node_tests PRIVATE node)

add_test(NAME xs_node_gate_node_tests COMMAND xs_node_gate_node_tests)


###############################
# xs_node_gate_gm_session_tests
###############################
add_executable(xs_node_gate_gm_session_tests
    node_gate_gm_session_tests.cpp
)

target_link_libraries(xs_node_gate_gm_session_tests PRIVATE node)

add_test(NAME xs_node_gate_gm_session_tests COMMAND xs_node_gate_gm_session_tests)


###############################
# xs_node_inner_network_tests
###############################
add_executable(xs_node_inner_network_tests
    node_inner_network_tests.cpp
)

target_link_libraries(xs_node_inner_network_tests PRIVATE node)

add_test(NAME xs_node_inner_network_tests COMMAND xs_node_inner_network_tests)


###############################
# xs_node_gm_inner_service_tests
###############################
add_executable(xs_node_gm_inner_service_tests
    node_gm_inner_service_tests.cpp
)

target_link_libraries(xs_node_gm_inner_service_tests PRIVATE node ipc net)

add_test(NAME xs_node_gm_inner_service_tests COMMAND xs_node_gm_inner_service_tests)

###############################
# xs_node_process_registry_tests
###############################
add_executable(xs_node_process_registry_tests
    node_process_registry_tests.cpp
)

target_link_libraries(xs_node_process_registry_tests PRIVATE node)

add_test(NAME xs_node_process_registry_tests COMMAND xs_node_process_registry_tests)

###############################
# Managed runtime test assets
###############################
find_program(DOTNET_EXECUTABLE dotnet REQUIRED)

file(GLOB_RECURSE XS_MANAGED_RUNTIME_TEST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/managed/Foundation/*.cs"
    "${CMAKE_SOURCE_DIR}/src/managed/GameLogic/*.cs"
)

set(XS_MANAGED_RUNTIME_TEST_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/managed/GameLogic")
file(TO_CMAKE_PATH "${XS_MANAGED_RUNTIME_TEST_OUTPUT_DIR}" XS_MANAGED_RUNTIME_TEST_OUTPUT_DIR_CMAKE)
set(XS_MANAGED_RUNTIME_TEST_ASSEMBLY_PATH
    "${XS_MANAGED_RUNTIME_TEST_OUTPUT_DIR_CMAKE}/XServer.Managed.GameLogic.dll")
set(XS_MANAGED_RUNTIME_TEST_RUNTIMECONFIG_PATH
    "${XS_MANAGED_RUNTIME_TEST_OUTPUT_DIR_CMAKE}/XServer.Managed.GameLogic.runtimeconfig.json")

add_custom_command(
    OUTPUT
        "${XS_MANAGED_RUNTIME_TEST_ASSEMBLY_PATH}"
        "${XS_MANAGED_RUNTIME_TEST_RUNTIMECONFIG_PATH}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dotnet-cli-home"
    COMMAND
        ${CMAKE_COMMAND} -E env
            DOTNET_CLI_HOME=${CMAKE_BINARY_DIR}/dotnet-cli-home
            DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
            DOTNET_NOLOGO=1
            "${DOTNET_EXECUTABLE}"
            build
            "${CMAKE_SOURCE_DIR}/src/managed/GameLogic/GameLogic.csproj"
            -c
            Debug
            --nologo
            -p:GenerateRuntimeConfigurationFiles=true
            "-p:OutDir=${XS_MANAGED_RUNTIME_TEST_OUTPUT_DIR_CMAKE}/"
    DEPENDS
        "${CMAKE_SOURCE_DIR}/XServerByAI.Managed.sln"
        "${CMAKE_SOURCE_DIR}/src/managed/Foundation/Foundation.csproj"
        "${CMAKE_SOURCE_DIR}/src/managed/GameLogic/GameLogic.csproj"
        ${XS_MANAGED_RUNTIME_TEST_SOURCES}
    VERBATIM
)

add_custom_target(xs_managed_gamelogic_runtime_assets
    DEPENDS
        "${XS_MANAGED_RUNTIME_TEST_ASSEMBLY_PATH}"
        "${XS_MANAGED_RUNTIME_TEST_RUNTIMECONFIG_PATH}"
)

###############################
# xs_host_runtime_tests
###############################
add_executable(xs_host_runtime_tests
    host_runtime_tests.cpp
)

add_dependencies(xs_host_runtime_tests xs_managed_gamelogic_runtime_assets)

target_link_libraries(xs_host_runtime_tests PRIVATE host)

target_compile_definitions(xs_host_runtime_tests PRIVATE
    XS_TEST_GAMELOGIC_ASSEMBLY_PATH=\"${XS_MANAGED_RUNTIME_TEST_ASSEMBLY_PATH}\"
    XS_TEST_GAMELOGIC_RUNTIMECONFIG_PATH=\"${XS_MANAGED_RUNTIME_TEST_RUNTIMECONFIG_PATH}\"
)

if (WIN32)
    add_custom_command(TARGET xs_host_runtime_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/3rd/dotnet_host/win-x64/nethost.dll"
            $<TARGET_FILE_DIR:xs_host_runtime_tests>
    )
endif()
add_test(NAME xs_host_runtime_tests COMMAND xs_host_runtime_tests)

