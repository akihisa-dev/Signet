# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.31)

project(
  Signet
  VERSION 0.1.1
  DESCRIPTION "A geometric emblem editor"
  LANGUAGES CXX
)

if(NOT APPLE)
  message(FATAL_ERROR "Signet currently supports macOS only")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Supported macOS architecture")
set(CMAKE_OSX_DEPLOYMENT_TARGET "26.0" CACHE STRING "Minimum supported macOS version")

option(SIGNET_ENABLE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(SIGNET_ENABLE_CLANG_TIDY "Run clang-tidy while compiling" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SignetWarnings)

# The first bootstrap commit establishes a buildable project target. Product
# sources and tests are added in dependency order by subsequent commits.
add_library(signet_core INTERFACE)
target_include_directories(signet_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/src")

include(CTest)
