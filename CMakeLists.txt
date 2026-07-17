# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# Disable format warning-as-error: int32_t is long int on xtensa-esp-elf
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-error=format=")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-error=format=")
# Add this line to disable the specific warning
add_compile_options(-Wno-missing-field-initializers)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)

set(PROJECT_VER "2.3.0")
project(xiaozhi)

# Disable format warning-as-error: int32_t is long int on this toolchain
idf_build_set_property(COMPILE_OPTIONS "-Wno-error=format=" APPEND)
