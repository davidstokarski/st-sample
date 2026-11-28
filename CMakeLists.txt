cmake_minimum_required(VERSION 3.24)
project(STSample VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Universal binary")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------------------------------------------------------------------------
# ST Shell: use a sibling checkout (../st-shell) when present, otherwise fetch
# it from GitHub. Override with -DST_SHELL_DIR=/path/to/st-shell.
# ---------------------------------------------------------------------------
set(ST_SHELL_DIR "" CACHE PATH "Path to a local st-shell checkout")
set(ST_SHELL_TAG "main" CACHE STRING "st-shell git ref to fetch")

if(ST_SHELL_DIR STREQUAL "" AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/../st-shell/CMakeLists.txt")
    set(ST_SHELL_DIR "${CMAKE_CURRENT_LIST_DIR}/../st-shell")
endif()

if(NOT ST_SHELL_DIR STREQUAL "")
    message(STATUS "Using local st-shell at ${ST_SHELL_DIR}")
    add_subdirectory("${ST_SHELL_DIR}" "${CMAKE_BINARY_DIR}/_deps/st-shell-build")
else()
    include(FetchContent)
    FetchContent_Declare(st_shell
        GIT_REPOSITORY https://github.com/davidstokarski/st-shell.git
        GIT_TAG        ${ST_SHELL_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(st_shell)
endif()

file(GLOB_RECURSE STSAMPLE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/Source/*.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Source/*.h")

# A drum sampler: an instrument (MIDI in, stereo out), AU type "aumu".
st_add_plugin(STSample
    PRODUCT_NAME    "ST Sample"
    PLUGIN_CODE     Stsm
    VERSION         ${PROJECT_VERSION}
    BUNDLE_SUFFIX   stsample
    IS_SYNTH
    VST3_CATEGORIES Instrument Drum Sampler
    SOURCES         ${STSAMPLE_SOURCES})

target_include_directories(STSample PRIVATE "${CMAKE_CURRENT_LIST_DIR}/Source")

option(STSAMPLE_BUILD_TESTS "Build the whole-plugin test tool" OFF)
if(STSAMPLE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
