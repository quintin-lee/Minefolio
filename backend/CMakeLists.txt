cmake_minimum_required(VERSION 3.16)
project(minefolio C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    csilk
    GIT_REPOSITORY https://github.com/quintin-lee/csilk.git
    GIT_TAG        v0.5.0
)

FetchContent_MakeAvailable(csilk)

# Find libpq for PostgreSQL support
find_package(PkgConfig QUIET)
pkg_check_modules(PQ libpq)

# Minefolio sources (recursive glob for all subdirectories)
file(GLOB_RECURSE SOURCES "src/*.c")
add_executable(minefolio ${SOURCES})

target_include_directories(minefolio PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(minefolio PRIVATE _GNU_SOURCE)

target_link_libraries(minefolio PRIVATE csilk crypto ${PQ_LIBRARIES})

if(PQ_INCLUDE_DIRS)
    target_include_directories(minefolio PRIVATE ${PQ_INCLUDE_DIRS})
endif()

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
