cmake_minimum_required(VERSION 3.10)

###
# Project definition
###
project(isotp)

set(APP_NAME isotp_example)
set(APP_LIB_NAME isotp)

###
# Get all include directories
###
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/example
)

set(CMAKE_BUILD_TYPE Debug)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src)

option(TESTS "Compile the tests" ON)
if(TESTS)
    add_subdirectory(tests)
endif(TESTS)

option(LINUX_SOCKET_EXAMPLE "Example implementation for linux" OFF)
if(LINUX_SOCKET_EXAMPLE)
    add_executable(${APP_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/example/linux_socket.c)
    target_link_libraries(${APP_NAME} ${APP_LIB_NAME})
endif(LINUX_SOCKET_EXAMPLE)