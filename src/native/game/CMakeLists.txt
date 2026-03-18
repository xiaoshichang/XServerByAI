add_library(game STATIC
    GameNodeRunner.cpp
    GameNodeRunner.h
)

target_include_directories(game PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${PROJECT_SOURCE_DIR}/src/native/node
)

target_link_libraries(game PUBLIC core)