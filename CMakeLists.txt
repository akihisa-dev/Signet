# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.31)

project(
  Signet
  VERSION 0.2.0
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

add_library(signet_core STATIC
  src/core/document.cpp
)
target_include_directories(signet_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
signet_set_project_warnings(signet_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(signet_document_tests tests/document_tests.cpp)
  target_link_libraries(signet_document_tests PRIVATE signet_core)
  signet_set_project_warnings(signet_document_tests)
  add_test(NAME document COMMAND signet_document_tests)

  if(SIGNET_ENABLE_SANITIZERS)
    foreach(target signet_core signet_document_tests)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
  endif()
endif()
