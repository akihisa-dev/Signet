# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.31)

project(
  Signet
  VERSION 0.4.0
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

find_package(CGAL 6.2 REQUIRED)

add_library(signet_core STATIC
  src/core/document.cpp
  src/geometry/arrangement_model.cpp
  src/geometry/alignment.cpp
  src/geometry/document_evaluator.cpp
  src/geometry/snap.cpp
)
target_include_directories(signet_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(signet_core PUBLIC CGAL::CGAL)
signet_set_project_warnings(signet_core)

include(CTest)
if(BUILD_TESTING)
  add_executable(signet_document_tests tests/document_tests.cpp)
  target_link_libraries(signet_document_tests PRIVATE signet_core)
  signet_set_project_warnings(signet_document_tests)
  add_test(NAME document COMMAND signet_document_tests)

  add_executable(signet_geometry_tests tests/arrangement_tests.cpp)
  target_link_libraries(signet_geometry_tests PRIVATE signet_core)
  signet_set_project_warnings(signet_geometry_tests)
  add_test(NAME geometry COMMAND signet_geometry_tests)

  add_executable(signet_snap_tests tests/snap_tests.cpp)
  target_link_libraries(signet_snap_tests PRIVATE signet_core)
  signet_set_project_warnings(signet_snap_tests)
  add_test(NAME snap COMMAND signet_snap_tests)

  add_executable(signet_alignment_tests tests/alignment_tests.cpp)
  target_link_libraries(signet_alignment_tests PRIVATE signet_core)
  signet_set_project_warnings(signet_alignment_tests)
  add_test(NAME alignment COMMAND signet_alignment_tests)

  add_executable(signet_document_evaluator_tests tests/document_evaluator_tests.cpp)
  target_link_libraries(signet_document_evaluator_tests PRIVATE signet_core)
  signet_set_project_warnings(signet_document_evaluator_tests)
  add_test(NAME document_evaluator COMMAND signet_document_evaluator_tests)

  if(SIGNET_ENABLE_SANITIZERS)
    foreach(target signet_core signet_document_tests signet_geometry_tests signet_snap_tests
                  signet_alignment_tests signet_document_evaluator_tests)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
  endif()
endif()
