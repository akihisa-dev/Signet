# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.31)

project(
  Signet
  VERSION 0.5.0
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
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets Test)

qt_standard_project_setup()

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

qt_add_executable(Signet MACOSX_BUNDLE
  src/main.cpp
  src/ui/canvas_view.cpp
  src/ui/editor_interaction.cpp
  src/ui/main_window.cpp
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignetVersion.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/signet_version.h"
  @ONLY
)
target_include_directories(Signet PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(Signet PRIVATE signet_core Qt6::Core Qt6::Gui Qt6::Widgets)
signet_set_project_warnings(Signet)

set_target_properties(Signet PROPERTIES
  MACOSX_BUNDLE_BUNDLE_NAME "Signet"
  MACOSX_BUNDLE_GUI_IDENTIFIER "dev.akihisa.signet"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  MACOSX_BUNDLE_COPYRIGHT "Copyright Signet contributors"
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
  MACOSX_DEPLOYMENT_TARGET "26.0"
)

if(SIGNET_ENABLE_SANITIZERS)
  foreach(target signet_core Signet)
    target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(${target} PRIVATE -fsanitize=address,undefined)
  endforeach()
endif()

if(SIGNET_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
  set(SIGNET_CLANG_TIDY_ARGS
    "${CLANG_TIDY_EXECUTABLE}"
    "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/.clang-tidy"
  )
  set_target_properties(signet_core Signet PROPERTIES
    CXX_CLANG_TIDY "${SIGNET_CLANG_TIDY_ARGS}"
  )
endif()

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
    foreach(target signet_document_tests signet_geometry_tests signet_snap_tests
                  signet_alignment_tests signet_document_evaluator_tests)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
  endif()
endif()

if(BUILD_TESTING)
  add_executable(signet_ui_tests
    tests/ui_tests.cpp
    src/ui/canvas_view.cpp
    src/ui/editor_interaction.cpp
    src/ui/main_window.cpp
  )
  target_link_libraries(signet_ui_tests PRIVATE signet_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
  target_include_directories(signet_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  signet_set_project_warnings(signet_ui_tests)
  add_test(NAME ui COMMAND signet_ui_tests)
  set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(signet_editor_interaction_tests
    tests/editor_interaction_tests.cpp
    src/ui/editor_interaction.cpp
  )
  target_link_libraries(signet_editor_interaction_tests PRIVATE signet_core)
  target_include_directories(signet_editor_interaction_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
  signet_set_project_warnings(signet_editor_interaction_tests)
  add_test(NAME editor_interaction COMMAND signet_editor_interaction_tests)

  if(SIGNET_ENABLE_SANITIZERS)
    foreach(target signet_ui_tests signet_editor_interaction_tests)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
  endif()
endif()

install(TARGETS Signet BUNDLE DESTINATION .)
