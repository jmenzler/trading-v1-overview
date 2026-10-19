cmake_minimum_required(VERSION 3.18)
project(quote_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(Python 3.12 COMPONENTS Interpreter Development.Module REQUIRED)
find_package(nanobind CONFIG REQUIRED)

nanobind_add_module(quote_cpp NB_STATIC NOMINSIZE python/bindings.cpp src/clmm_step.cpp src/clmm.cpp src/dlmm.cpp src/search.cpp)
target_include_directories(quote_cpp PRIVATE include)
# Numba doesn't fuse multiply-add; contracting would change the rounded bits.
target_compile_options(quote_cpp PRIVATE -ffp-contract=off -Wall -Wextra)

install(TARGETS quote_cpp LIBRARY DESTINATION .)
