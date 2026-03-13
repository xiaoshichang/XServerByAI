add_library(host STATIC
    host.cpp
)

target_include_directories(host PUBLIC ${CMAKE_CURRENT_LIST_DIR})