add_library(gm STATIC
    GmNodeRunner.cpp
    GmNodeRunner.h
)

target_include_directories(gm PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}
    ${PROJECT_SOURCE_DIR}/src/native/node
)

target_link_libraries(gm PUBLIC core)