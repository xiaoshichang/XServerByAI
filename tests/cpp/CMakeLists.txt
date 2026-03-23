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

target_link_libraries(xs_net_cluster_control_codec_tests PRIVATE net)

add_test(NAME xs_net_cluster_control_codec_tests COMMAND xs_net_cluster_control_codec_tests)

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

target_link_libraries(xs_node_gm_control_service_tests PRIVATE node ipc net)

add_test(NAME xs_node_gm_control_service_tests COMMAND xs_node_gm_control_service_tests)

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

function(xs_add_managed_runtime_asset asset_name project_path output_dir)
    get_filename_component(project_directory "${project_path}" DIRECTORY)

    file(GLOB_RECURSE ${asset_name}_SOURCES CONFIGURE_DEPENDS
        "${project_directory}/*.cs"
    )

    set(${asset_name}_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/managed/${output_dir}")
    file(TO_CMAKE_PATH "${${asset_name}_OUTPUT_DIR}" ${asset_name}_OUTPUT_DIR_CMAKE)
    set(${asset_name}_ASSEMBLY_PATH
        "${${asset_name}_OUTPUT_DIR_CMAKE}/XServer.Managed.GameLogic.dll")
    set(${asset_name}_RUNTIMECONFIG_PATH
        "${${asset_name}_OUTPUT_DIR_CMAKE}/XServer.Managed.GameLogic.runtimeconfig.json")

    add_custom_command(
        OUTPUT
            "${${asset_name}_ASSEMBLY_PATH}"
            "${${asset_name}_RUNTIMECONFIG_PATH}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/dotnet-cli-home"
        COMMAND
            ${CMAKE_COMMAND} -E env
                DOTNET_CLI_HOME=${CMAKE_BINARY_DIR}/dotnet-cli-home
                DOTNET_SKIP_FIRST_TIME_EXPERIENCE=1
                DOTNET_NOLOGO=1
                "${DOTNET_EXECUTABLE}"
                build
                "${project_path}"
                -c
                Debug
                --nologo
                -p:GenerateRuntimeConfigurationFiles=true
                "-p:OutDir=${${asset_name}_OUTPUT_DIR_CMAKE}/"
        DEPENDS
            "${CMAKE_SOURCE_DIR}/XServerByAI.Managed.sln"
            "${project_path}"
            ${${asset_name}_SOURCES}
        VERBATIM
    )

    add_custom_target(${asset_name}
        DEPENDS
            "${${asset_name}_ASSEMBLY_PATH}"
            "${${asset_name}_RUNTIMECONFIG_PATH}"
    )

    set(${asset_name}_ASSEMBLY_PATH "${${asset_name}_ASSEMBLY_PATH}" PARENT_SCOPE)
    set(${asset_name}_RUNTIMECONFIG_PATH "${${asset_name}_RUNTIMECONFIG_PATH}" PARENT_SCOPE)
endfunction()

xs_add_managed_runtime_asset(
    xs_managed_gamelogic_runtime_assets
    "${CMAKE_SOURCE_DIR}/src/managed/GameLogic/GameLogic.csproj"
    "GameLogic"
)

xs_add_managed_runtime_asset(
    xs_managed_interop_abi_mismatch_runtime_assets
    "${CMAKE_SOURCE_DIR}/src/managed/Tests/ManagedInteropAbiMismatch.Tests/ManagedInteropAbiMismatch.Tests.csproj"
    "InteropAbiMismatch"
)

xs_add_managed_runtime_asset(
    xs_managed_interop_missing_exports_runtime_assets
    "${CMAKE_SOURCE_DIR}/src/managed/Tests/ManagedInteropMissingExports.Tests/ManagedInteropMissingExports.Tests.csproj"
    "InteropMissingExports"
)

###############################
# xs_host_runtime_tests
###############################
add_executable(xs_host_runtime_tests
    host_runtime_tests.cpp
)

add_dependencies(
    xs_host_runtime_tests
    xs_managed_gamelogic_runtime_assets
    xs_managed_interop_abi_mismatch_runtime_assets
    xs_managed_interop_missing_exports_runtime_assets
)

target_link_libraries(xs_host_runtime_tests PRIVATE host)

target_compile_definitions(xs_host_runtime_tests PRIVATE
    XS_TEST_GAMELOGIC_ASSEMBLY_PATH="${xs_managed_gamelogic_runtime_assets_ASSEMBLY_PATH}"
    XS_TEST_GAMELOGIC_RUNTIMECONFIG_PATH="${xs_managed_gamelogic_runtime_assets_RUNTIMECONFIG_PATH}"
    XS_TEST_GAMELOGIC_ABI_MISMATCH_ASSEMBLY_PATH="${xs_managed_interop_abi_mismatch_runtime_assets_ASSEMBLY_PATH}"
    XS_TEST_GAMELOGIC_ABI_MISMATCH_RUNTIMECONFIG_PATH="${xs_managed_interop_abi_mismatch_runtime_assets_RUNTIMECONFIG_PATH}"
    XS_TEST_GAMELOGIC_MISSING_EXPORTS_ASSEMBLY_PATH="${xs_managed_interop_missing_exports_runtime_assets_ASSEMBLY_PATH}"
    XS_TEST_GAMELOGIC_MISSING_EXPORTS_RUNTIMECONFIG_PATH="${xs_managed_interop_missing_exports_runtime_assets_RUNTIMECONFIG_PATH}"
)

if (WIN32)
    add_custom_command(TARGET xs_host_runtime_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/3rd/dotnet_host/win-x64/nethost.dll"
            $<TARGET_FILE_DIR:xs_host_runtime_tests>
    )
endif()
add_test(NAME xs_host_runtime_tests COMMAND xs_host_runtime_tests)
