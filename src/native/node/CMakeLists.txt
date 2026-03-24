add_library(node STATIC
    ClientNetwork.cpp
    ClientNetwork.h
    GameNode.cpp
    GameNode.h
    GmControlHttpService.cpp
    GmControlHttpService.h
    GmInnerService.cpp
    GmInnerService.h
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
target_link_libraries(node PUBLIC core ipc net)

add_executable(xserver_node
    node_main.cpp
)

set_target_properties(xserver_node PROPERTIES
    OUTPUT_NAME "xserver-node"
)

target_link_libraries(xserver_node PRIVATE node)
