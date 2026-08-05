cmake_minimum_required(VERSION 3.28)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "In-source builds are not allowed. Please use a separate build/ directory.")
endif()

set(APP_NAME audio-gate)

project(${APP_NAME} VERSION 0.1 LANGUAGES C CXX)

# Enforce C++23 Standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTORCC ON)

# Add Dependencies (loads cmake/dependencies.cmake)
include(cmake/dependencies.cmake)

# Main Executable Target
add_executable(${APP_NAME})

# Declare C++23 Module Interfaces & Source Files
target_sources(${APP_NAME}
    PRIVATE
        src/application.cpp
        src/main.cpp
        src/virtual_audio_device.cpp
        src/pulse_client.cpp
        src/null_sink.cpp
        src/loopback.cpp
        src/remap_source.cpp
        include/tray_popup.h src/tray_popup.cpp
        include/tray_popup_fwd.h
        resources/resources.qrc
    PUBLIC
        FILE_SET CXX_MODULES TYPE CXX_MODULES FILES
            src/application.cppm
            src/virtual_audio_device.cppm
            src/pulse_client.cppm
            src/null_sink.cppm
            src/loopback.cppm
            src/remap_source.cppm
)

target_include_directories(${APP_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Link all dependencies imported from dependencies.cmake
target_link_libraries(${APP_NAME}
    PRIVATE
        ${APP_DEPENDENCIES}
)
