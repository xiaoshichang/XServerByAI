add_library(net STATIC
    BinarySerialization.cpp
    BinarySerialization.h
    ByteOrder.h
    MessageDispatcher.cpp
    MessageDispatcher.h
    message/HeartbeatCodec.cpp
    message/HeartbeatCodec.h
    message/PacketCodec.cpp
    message/PacketCodec.h
    net.cpp
    PacketHeader.h
)

target_include_directories(net PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(net PUBLIC xs::thirdparty::asio)