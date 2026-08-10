cmake_minimum_required(VERSION 3.16)
project(minefolio C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

# csilk as subdirectory
set(CSILK_SOURCE_DIR "/data/home/quintin/workspace/source/c/csilk")
list(APPEND CMAKE_MODULE_PATH "${CSILK_SOURCE_DIR}/cmake")
include(csilk)
add_subdirectory("${CSILK_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/csilk")

# Minefolio sources
file(GLOB SOURCES "src/*.c" "src/common/*.c")
add_executable(minefolio ${SOURCES})

target_include_directories(minefolio PRIVATE
    "${CSILK_SOURCE_DIR}/include"
    "${CSILK_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(minefolio PRIVATE csilk)

# Copy config and sql on build
add_custom_command(TARGET minefolio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/config"
        "${CMAKE_BINARY_DIR}/config"
)
add_custom_command(TARGET minefolio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/sql"
        "${CMAKE_BINARY_DIR}/sql"
)
