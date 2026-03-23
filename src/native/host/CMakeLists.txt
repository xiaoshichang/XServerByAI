add_library(host STATIC
    ManagedRuntimeHost.cpp
    ManagedRuntimeHost.h
    host.cpp
)

target_include_directories(host PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(host PUBLIC xs::thirdparty::nethost)
