cmake_minimum_required(VERSION 3.20)
project(ferro CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Without this CMake passes no -O flag at all, so latency numbers would come
# from an unoptimized build. RelWithDebInfo keeps symbols for profiling.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_subdirectory(cpp_core)
