cmake_minimum_required(VERSION 3.20)

project(vofa2 VERSION 0.1.0 LANGUAGES CXX)

# 应用版本号：默认取 project() 的版本，CI 每次构建用 -DVOFA_VERSION=x.y.z 覆盖，
# 从而让 exe 资源、界面标题、安装包和 Release 资产名保持同一个版本。
set(VOFA_VERSION "${PROJECT_VERSION}" CACHE STRING "Application version (x.y.z)")
if(NOT VOFA_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(WARNING "VOFA_VERSION='${VOFA_VERSION}' 不是 x.y.z 形式，回退到 ${PROJECT_VERSION}")
    set(VOFA_VERSION "${PROJECT_VERSION}")
endif()
message(STATUS "VOFA2 version: ${VOFA_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(VOFA_BUILD_TESTS "Build unit tests" ON)
option(VOFA_BUILD_BENCH "Build micro-benchmarks" ON)
option(VOFA_BUILD_FUZZERS "Build libFuzzer targets (Clang only)" OFF)
option(VOFA_BUILD_APP "Build the Qt application" OFF)

if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(src/core)
add_subdirectory(src/protocol)
add_subdirectory(src/commands)
add_subdirectory(src/data)

if(VOFA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(VOFA_BUILD_BENCH)
    add_subdirectory(tests/bench)
endif()

if(VOFA_BUILD_FUZZERS)
    add_subdirectory(tests/fuzz)
endif()

if(VOFA_BUILD_APP)
    find_package(Qt6 REQUIRED COMPONENTS
        Core
        Gui
        Qml
        Quick
        QuickControls2
        Network
        SerialPort
        Test
    )
    qt_standard_project_setup()

    add_subdirectory(src/remote)
    add_subdirectory(src/transport)
    add_subdirectory(src/app)

    if(VOFA_BUILD_TESTS)
        add_subdirectory(tests/qt)
    endif()
endif()
