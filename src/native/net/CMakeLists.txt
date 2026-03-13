add_library(net STATIC
    net.cpp
)

target_include_directories(net PUBLIC ${CMAKE_CURRENT_LIST_DIR})