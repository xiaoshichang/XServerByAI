add_library(ipc STATIC
    ipc.cpp
    ZmqActiveConnector.cpp
    ZmqActiveConnector.h
    ZmqContext.cpp
    ZmqContext.h
    ZmqPassiveListener.cpp
    ZmqPassiveListener.h
)

target_include_directories(ipc PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(ipc PUBLIC xs::thirdparty::zeromq xs::thirdparty::asio)