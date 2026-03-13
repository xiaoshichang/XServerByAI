add_executable(gate
    gate_main.cpp
)

target_link_libraries(gate PRIVATE core net ipc host)