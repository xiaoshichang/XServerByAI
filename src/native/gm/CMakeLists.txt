add_executable(gm
    gm_main.cpp
)

target_link_libraries(gm PRIVATE core net ipc host)