add_library(core STATIC
    core.cpp
    Json.cpp
    Json.h
    Logging.cpp
    Logging.h
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(core PUBLIC xs::thirdparty::nlohmann_json xs::thirdparty::spdlog)
