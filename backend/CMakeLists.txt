cmake_minimum_required(VERSION 3.16)
project(minefolio C)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    csilk
    SOURCE_DIR     "${CMAKE_CURRENT_SOURCE_DIR}/../deps/csilk"
    GIT_REPOSITORY https://github.com/quintin-lee/csilk.git
    GIT_TAG        master
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
target_compile_options(minefolio PRIVATE -Wall -Wextra -Wno-unused-parameter)

find_package(CURL REQUIRED)

target_link_libraries(minefolio PRIVATE csilk crypto CURL::libcurl ${PQ_LIBRARIES})

if(PQ_INCLUDE_DIRS)
    target_include_directories(minefolio PRIVATE ${PQ_INCLUDE_DIRS})
endif()

# ---------------------------------------------------------------------------
# Pre-build auto-format: keep src/**/*.{c,h} conformant to .clang-format
# Requires clang-format >= 14 (.clang-format uses AlignTrailingComments Kind:)
# Third-party / generated code (build/, deps/, _deps/, vendor/, third_party/)
# is explicitly excluded so the hook never touches external libraries.
# ---------------------------------------------------------------------------
find_program(CLANG_FORMAT_EXE
    NAMES clang-format clang-format-18 clang-format-17 clang-format-16
          clang-format-15 clang-format-14
)

if(CLANG_FORMAT_EXE)
    file(GLOB_RECURSE FORMAT_SOURCES "src/*.c" "src/*.h")
    # Hard guarantee: never format third-party or generated code
    list(FILTER FORMAT_SOURCES EXCLUDE REGEX "(/|^)(build|deps|_deps|vendor|third[_-]?party)/")
    if(FORMAT_SOURCES)
        set(FORMAT_STAMP "${CMAKE_BINARY_DIR}/clang-format.stamp")
        add_custom_command(
            OUTPUT "${FORMAT_STAMP}"
            COMMAND ${CMAKE_COMMAND}
                -DCLANG_FORMAT="${CLANG_FORMAT_EXE}"
                "-DSOURCES=${FORMAT_SOURCES}"
                -DSTAMP="${FORMAT_STAMP}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/FormatSources.cmake"
            DEPENDS ${FORMAT_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/.clang-format"
            COMMENT "Checking source formatting"
            VERBATIM
        )
        add_custom_target(format_sources ALL DEPENDS "${FORMAT_STAMP}")
        add_dependencies(minefolio format_sources)
    else()
        message(STATUS "no sources to format - skipping pre-build auto-format")
    endif()
else()
    message(STATUS "clang-format not found - skipping pre-build auto-format")
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
if(DEFINED csilk_SOURCE_DIR)
    add_custom_command(TARGET minefolio POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${csilk_SOURCE_DIR}/share"
            "${CMAKE_BINARY_DIR}/share"
    )
endif()
