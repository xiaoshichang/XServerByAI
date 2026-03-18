add_library(gate STATIC
    GateNodeRunner.cpp
    GateNodeRunner.h
)

target_include_directories(gate PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${PROJECT_SOURCE_DIR}/src/native/node
)

target_link_libraries(gate PUBLIC core)