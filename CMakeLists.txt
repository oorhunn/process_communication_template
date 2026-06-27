cmake_minimum_required(VERSION 3.31)


project(health_check_template)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(ROOT_PATH "${CMAKE_CURRENT_SOURCE_DIR}")
set(SRC_PATH "${ROOT_PATH}/src")
set(LIB_PATH "${ROOT_PATH}/lib")
set(COMMON_PATH "${ROOT_PATH}/common")
set(UTILITY_PATH "${CMAKE_CURRENT_LIST_DIR}/utility")


list(APPEND CMAKE_MODULE_PATH "${ROOT_PATH}")
include(process_communication/process_communication)

add_subdirectory(examples)


enable_testing()
add_subdirectory(tests)