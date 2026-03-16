add_library(core STATIC
    Config.cpp
    Config.h
    core.cpp
    Json.cpp
    Json.h
    Logging.cpp
    Logging.h
    TimeUtils.cpp
    TimeUtils.h
    Timer.cpp
    Timer.h
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(core PUBLIC xs::thirdparty::nlohmann_json xs::thirdparty::spdlog xs::thirdparty::asio)
