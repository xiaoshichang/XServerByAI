cmake_minimum_required(VERSION 3.31.5)

project(XServerByAI LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

option(XS_BUILD_TESTS "Build tests" OFF)
option(XS_BUILD_TOOLS "Build tools" OFF)

add_subdirectory(3rd)
add_subdirectory(src/native)
