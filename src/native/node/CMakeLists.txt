add_library(node_runtime STATIC
    NodeGameRunner.cpp
    NodeGameRunner.h
    NodeGateRunner.cpp
    NodeGateRunner.h
    NodeGmRunner.cpp
    NodeGmRunner.h
    NodeRuntime.cpp
    NodeRuntime.h
)

target_include_directories(node_runtime PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(node_runtime PUBLIC core)

add_executable(xserver_node
    node_main.cpp
)

set_target_properties(xserver_node PROPERTIES
    OUTPUT_NAME "xserver-node"
)

target_link_libraries(xserver_node PRIVATE node_runtime)