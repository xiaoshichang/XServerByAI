add_executable(xserver_node
    node_main.cpp
)

set_target_properties(xserver_node PROPERTIES
    OUTPUT_NAME "xserver-node"
)

target_link_libraries(xserver_node PRIVATE core net ipc host)
