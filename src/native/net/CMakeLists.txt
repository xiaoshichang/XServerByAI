add_library(net STATIC
    BinarySerialization.cpp
    BinarySerialization.h
    ByteOrder.h
    MessageDispatcher.cpp
    MessageDispatcher.h
    net.cpp
    PacketCodec.cpp
    PacketCodec.h
    PacketHeader.h
    ProcessHeartbeatCodec.cpp
    ProcessHeartbeatCodec.h
)

target_include_directories(net PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(net PUBLIC xs::thirdparty::asio)