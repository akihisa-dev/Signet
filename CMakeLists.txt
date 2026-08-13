# SPDX-License-Identifier: AGPL-3.0-or-later

cmake_minimum_required(VERSION 3.31)

project(
  Signet
  VERSION 0.8.1
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
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Widgets LinguistTools Test)

qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES ja)

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

add_library(signet_ai STATIC
  src/ai/provider.h
  src/ai/codex_cli_provider.h
  src/ai/logo_construction_plan.cpp
  src/ai/plan_compiler.cpp
  src/ai/codex_cli_provider.cpp
)
target_include_directories(signet_ai PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(signet_ai PUBLIC signet_core Qt6::Core Qt6::Gui)
signet_set_project_warnings(signet_ai)

qt_add_executable(Signet MACOSX_BUNDLE
  src/main.cpp
  src/ui/canvas_view.cpp
  src/ui/editor_interaction.cpp
  src/ui/ai_generation_dialog.cpp
  src/ui/main_window.cpp
)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SignetVersion.h.in"
  "${CMAKE_CURRENT_BINARY_DIR}/signet_version.h"
  @ONLY
)
target_include_directories(Signet PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(Signet PRIVATE signet_ai signet_core Qt6::Core Qt6::Gui Qt6::Widgets)
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
  foreach(target signet_core signet_ai Signet)
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
  set_target_properties(signet_core signet_ai Signet PROPERTIES
    CXX_CLANG_TIDY "${SIGNET_CLANG_TIDY_ARGS}"
  )
endif()

# Qt's supported deployment API invokes macdeployqt on Apple app bundles.  Qt
# 6.11's macOS implementation does not forward EXCLUDE_PLUGINS to macdeployqt,
# so using it still scans optional plugins and fails on unavailable QtPdf,
# QtSvg, and QtVirtualKeyboard frameworks.  Stop plugin scanning at the tool
# boundary and install the plugins recorded by the runtime manifest explicitly.
set(SIGNET_REQUIRED_QT_PLUGIN_RELATIVE_PATHS
  "platforms/libqcocoa.dylib"
  "styles/libqmacstyle.dylib"
  "imageformats/libqgif.dylib"
  "imageformats/libqicns.dylib"
  "imageformats/libqico.dylib"
  "imageformats/libqjp2.dylib"
  "imageformats/libqjpeg.dylib"
  "imageformats/libqmacheif.dylib"
  "imageformats/libqmng.dylib"
  "imageformats/libqtga.dylib"
  "imageformats/libqtiff.dylib"
  "imageformats/libqwbmp.dylib"
  "imageformats/libqwebp.dylib"
)
set(SIGNET_QT_DEPLOY_PLUGIN_OPTIONS)
foreach(SIGNET_QT_PLUGIN_RELATIVE_PATH IN LISTS SIGNET_REQUIRED_QT_PLUGIN_RELATIVE_PATHS)
  list(APPEND SIGNET_QT_DEPLOY_PLUGIN_OPTIONS
    "-executable=Signet.app/Contents/PlugIns/${SIGNET_QT_PLUGIN_RELATIVE_PATH}")
endforeach()
qt_generate_deploy_app_script(
  TARGET Signet
  OUTPUT_SCRIPT signet_deploy_script
  NO_PLUGINS
  NO_TRANSLATIONS
  DEPLOY_TOOL_OPTIONS -no-codesign ${SIGNET_QT_DEPLOY_PLUGIN_OPTIONS}
)

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

  add_executable(signet_ai_plan_tests tests/ai_plan_tests.cpp)
  target_link_libraries(signet_ai_plan_tests PRIVATE signet_ai)
  target_include_directories(signet_ai_plan_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  signet_set_project_warnings(signet_ai_plan_tests)
  add_test(NAME ai_plan COMMAND signet_ai_plan_tests)

  add_executable(signet_ai_provider_tests tests/ai_provider_tests.cpp)
  target_link_libraries(signet_ai_provider_tests PRIVATE signet_ai Qt6::Core Qt6::Gui Qt6::Test)
  target_include_directories(signet_ai_provider_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  signet_set_project_warnings(signet_ai_provider_tests)
  add_test(NAME ai_provider COMMAND signet_ai_provider_tests)

  if(SIGNET_ENABLE_SANITIZERS)
    foreach(target signet_document_tests signet_geometry_tests signet_snap_tests
                  signet_alignment_tests signet_document_evaluator_tests signet_ai_plan_tests
                  signet_ai_provider_tests)
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
    src/ui/ai_generation_dialog.cpp
    src/ui/main_window.cpp
  )
  target_link_libraries(signet_ui_tests PRIVATE signet_ai signet_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
  target_include_directories(signet_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  signet_set_project_warnings(signet_ui_tests)
  add_test(NAME ui COMMAND signet_ui_tests)
  set_tests_properties(ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  add_executable(signet_ai_ui_tests
    tests/ai_ui_tests.cpp
    src/ui/canvas_view.cpp
    src/ui/editor_interaction.cpp
    src/ui/ai_generation_dialog.cpp
    src/ui/main_window.cpp
  )
  target_link_libraries(signet_ai_ui_tests PRIVATE signet_ai signet_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
  target_include_directories(signet_ai_ui_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  signet_set_project_warnings(signet_ai_ui_tests)
  add_test(NAME ai_ui COMMAND signet_ai_ui_tests)
  set_tests_properties(ai_ui PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
    foreach(target signet_ui_tests signet_ai_ui_tests signet_editor_interaction_tests)
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      target_link_options(${target} PRIVATE -fsanitize=address,undefined)
    endforeach()
  endif()
endif()

set(SIGNET_TRANSLATION_TARGETS Signet)
if(BUILD_TESTING)
  list(APPEND SIGNET_TRANSLATION_TARGETS signet_ui_tests)
endif()
qt_add_translations(TARGETS ${SIGNET_TRANSLATION_TARGETS}
  TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/translations/Signet_ja.ts"
  RESOURCE_PREFIX "/i18n"
)

set(SIGNET_BUNDLE_LICENSE_DESTINATION "Signet.app/Contents/Resources/licenses")

# Release installs are distribution candidates. The checked-in runtime tree is
# made from the exact upstream files recorded in licenses/runtime/manifest.tsv;
# keeping those bytes in the source tree makes install and verification use the
# same license inventory.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${SIGNET_BUNDLE_LICENSE_DESTINATION}"
    RENAME "Signet-LICENSE"
  )
  install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${SIGNET_BUNDLE_LICENSE_DESTINATION}"
    RENAME "THIRD_PARTY_NOTICES.md"
  )
  install(DIRECTORY
    "${CMAKE_CURRENT_SOURCE_DIR}/licenses/runtime/"
    DESTINATION "${SIGNET_BUNDLE_LICENSE_DESTINATION}/runtime"
    FILES_MATCHING
    PATTERN "*"
  )
endif()

install(TARGETS Signet BUNDLE DESTINATION .)

if(NOT TARGET Qt6::QCocoaIntegrationPlugin)
  message(FATAL_ERROR "Qt6::QCocoaIntegrationPlugin is required for macOS deployment")
endif()
get_target_property(SIGNET_QCOCOA_PLUGIN Qt6::QCocoaIntegrationPlugin
  IMPORTED_LOCATION_RELEASE)
if(NOT SIGNET_QCOCOA_PLUGIN OR NOT EXISTS "${SIGNET_QCOCOA_PLUGIN}")
  get_target_property(SIGNET_QCOCOA_PLUGIN Qt6::QCocoaIntegrationPlugin IMPORTED_LOCATION)
endif()
if(NOT SIGNET_QCOCOA_PLUGIN OR NOT EXISTS "${SIGNET_QCOCOA_PLUGIN}")
  message(FATAL_ERROR
    "Cannot locate the Qt6::QCocoaIntegrationPlugin deployment artifact")
endif()
get_filename_component(SIGNET_QT_PLUGIN_TYPE_DIR "${SIGNET_QCOCOA_PLUGIN}" DIRECTORY)
get_filename_component(SIGNET_QT_PLUGIN_DIR "${SIGNET_QT_PLUGIN_TYPE_DIR}" DIRECTORY)

set(SIGNET_REQUIRED_QT_PLUGINS)
set(SIGNET_REQUIRED_QT_PLUGIN_REALPATHS)
foreach(SIGNET_QT_PLUGIN_RELATIVE_PATH IN LISTS SIGNET_REQUIRED_QT_PLUGIN_RELATIVE_PATHS)
  list(APPEND SIGNET_REQUIRED_QT_PLUGINS
    "${SIGNET_QT_PLUGIN_DIR}/${SIGNET_QT_PLUGIN_RELATIVE_PATH}")
endforeach()
set(SIGNET_REQUIRED_QT_PLUGINS
  ${SIGNET_REQUIRED_QT_PLUGINS}
)
foreach(SIGNET_QT_PLUGIN IN LISTS SIGNET_REQUIRED_QT_PLUGINS)
  if(NOT EXISTS "${SIGNET_QT_PLUGIN}")
    message(FATAL_ERROR "Required Qt plugin is missing: ${SIGNET_QT_PLUGIN}")
  endif()
  get_filename_component(SIGNET_QT_PLUGIN_REALPATH "${SIGNET_QT_PLUGIN}" REALPATH)
  if(NOT EXISTS "${SIGNET_QT_PLUGIN_REALPATH}")
    message(FATAL_ERROR "Required Qt plugin realpath is missing: ${SIGNET_QT_PLUGIN}")
  endif()
  list(APPEND SIGNET_REQUIRED_QT_PLUGIN_REALPATHS "${SIGNET_QT_PLUGIN_REALPATH}")
  get_filename_component(SIGNET_QT_PLUGIN_TYPE_DIR "${SIGNET_QT_PLUGIN}" DIRECTORY)
  get_filename_component(SIGNET_QT_PLUGIN_TYPE "${SIGNET_QT_PLUGIN_TYPE_DIR}" NAME)
  install(FILES "${SIGNET_QT_PLUGIN_REALPATH}"
    DESTINATION "Signet.app/Contents/PlugIns/${SIGNET_QT_PLUGIN_TYPE}")
endforeach()

set(SIGNET_QT_RUNTIME_DIRECTORIES)
foreach(SIGNET_QT_PLUGIN IN LISTS SIGNET_REQUIRED_QT_PLUGINS)
  get_filename_component(SIGNET_QT_PLUGIN_REALPATH "${SIGNET_QT_PLUGIN}" REALPATH)
  get_filename_component(SIGNET_QT_PLUGIN_TYPE_DIR "${SIGNET_QT_PLUGIN_REALPATH}" DIRECTORY)
  get_filename_component(SIGNET_QT_PLUGIN_ROOT "${SIGNET_QT_PLUGIN_TYPE_DIR}" DIRECTORY)
  get_filename_component(SIGNET_QT_DATA_DIR "${SIGNET_QT_PLUGIN_ROOT}" DIRECTORY)
  get_filename_component(SIGNET_QT_SHARE_DIR "${SIGNET_QT_DATA_DIR}" DIRECTORY)
  get_filename_component(SIGNET_QT_PREFIX_DIR "${SIGNET_QT_SHARE_DIR}" DIRECTORY)
  list(APPEND SIGNET_QT_RUNTIME_DIRECTORIES "${SIGNET_QT_PREFIX_DIR}/lib")
endforeach()
list(REMOVE_DUPLICATES SIGNET_REQUIRED_QT_PLUGIN_REALPATHS)
list(REMOVE_DUPLICATES SIGNET_QT_RUNTIME_DIRECTORIES)
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deploy_macos.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/deploy_macos.cmake"
  @ONLY
)

install(SCRIPT "${signet_deploy_script}")
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/deploy_macos.cmake")
install(SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/fix_macos_bundle_paths.cmake")
