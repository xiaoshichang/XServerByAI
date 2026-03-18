add_library(node_runtime STATIC
    NodeRuntime.cpp
    NodeRuntime.h
)

target_include_directories(node_runtime PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(node_runtime PUBLIC core gm gate game)

add_executable(xserver_node
    node_main.cpp
)

set_target_properties(xserver_node PROPERTIES
    OUTPUT_NAME "xserver-node"
)

target_link_libraries(xserver_node PRIVATE node_runtime)