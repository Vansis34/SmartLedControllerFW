# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# PlatformIO passes its own component directories to CMake. Append the shared
# Espressif example component instead of overwriting that list, otherwise the
# application component selected through `src_dir = main` may be lost.
list(APPEND EXTRA_COMPONENT_DIRS
    $ENV{IDF_PATH}/examples/common_components/protocol_examples_common)

# if(${IDF_TARGET} STREQUAL "linux")
#     list(APPEND EXTRA_COMPONENT_DIRS "$ENV{IDF_PATH}/tools/mocks/freertos/"
#                     "$ENV{IDF_PATH}/examples/protocols/linux_stubs/esp_stubs")
#     set(COMPONENTS main)
# endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(LED-controller-FW)
