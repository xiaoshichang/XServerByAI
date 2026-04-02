add_library(node STATIC
    ClientSession.cpp
    ClientSession.h
    ClientNetwork.cpp
    ClientNetwork.h
    GameNode.cpp
    GameNode.h
    GmControlHttpService.cpp
    GmControlHttpService.h
    GateNode.cpp
    GateNode.h
    GmNode.cpp
    GmNode.h
    InnerNetwork.cpp
    InnerNetwork.h
    NodeCommon.cpp
    NodeCommon.h
    NodeCreateHelper.cpp
    NodeCreateHelper.h
    InnerNetworkSessionManager.cpp
    InnerNetworkSessionManager.h
    ServerNode.cpp
    ServerNode.h
)

target_include_directories(node PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(node PUBLIC core host ipc net)

add_executable(xserver_node
    node_main.cpp
)

set_target_properties(xserver_node PROPERTIES
    OUTPUT_NAME "xserver-node"
)

target_link_libraries(xserver_node PRIVATE node)

if (WIN32)
    add_custom_command(TARGET xserver_node POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/3rd/dotnet_host/win-x64/nethost.dll"
            $<TARGET_FILE_DIR:xserver_node>
    )
endif()
