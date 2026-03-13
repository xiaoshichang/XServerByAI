add_library(core STATIC
    core.cpp
)

target_include_directories(core PUBLIC ${CMAKE_CURRENT_LIST_DIR})