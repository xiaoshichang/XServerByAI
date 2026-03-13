add_library(ipc STATIC
    ipc.cpp
)

target_include_directories(ipc PUBLIC ${CMAKE_CURRENT_LIST_DIR})