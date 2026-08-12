cmake_minimum_required(VERSION 3.31)

set(SIGNET_BUNDLE "${CMAKE_INSTALL_PREFIX}/Signet.app")
if(DEFINED ENV{DESTDIR} AND NOT "$ENV{DESTDIR}" STREQUAL "")
  set(SIGNET_BUNDLE "$ENV{DESTDIR}${SIGNET_BUNDLE}")
endif()

if(NOT IS_DIRECTORY "${SIGNET_BUNDLE}")
  message(FATAL_ERROR "Cannot fix bundle paths; app not found: ${SIGNET_BUNDLE}")
endif()

# macdeployqt can copy optional plugins whose Qt module is not deployable from
# the Homebrew Qt layout.  Signet does not use these plugins; leaving them in
# the bundle would make the install look complete while retaining unresolved
# QtSvg, QtPdf, and QtVirtualKeyboard @rpath dependencies.
foreach(SIGNET_UNUSED_PLUGIN IN ITEMS
    "${SIGNET_BUNDLE}/Contents/PlugIns/iconengines/libqsvgicon.dylib"
    "${SIGNET_BUNDLE}/Contents/PlugIns/imageformats/libqpdf.dylib"
    "${SIGNET_BUNDLE}/Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib")
  if(EXISTS "${SIGNET_UNUSED_PLUGIN}")
    file(REMOVE "${SIGNET_UNUSED_PLUGIN}")
  endif()
endforeach()

function(signet_execute)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE SIGNET_RESULT
    OUTPUT_VARIABLE SIGNET_OUTPUT
    ERROR_VARIABLE SIGNET_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT SIGNET_RESULT EQUAL 0)
    message(FATAL_ERROR
      "Command failed (${SIGNET_RESULT}): ${ARGN}\n${SIGNET_OUTPUT}\n${SIGNET_ERROR}")
  endif()
endfunction()

function(signet_bundle_relative_path input_path output_var)
  if("${input_path}" MATCHES "/([^/]+\\.framework)/Versions/([^/]+)/([^/]+)$")
    set(framework "${CMAKE_MATCH_1}")
    set(version "${CMAKE_MATCH_2}")
    set(binary "${CMAKE_MATCH_3}")
    set(candidate
      "${SIGNET_BUNDLE}/Contents/Frameworks/${framework}/Versions/${version}/${binary}")
    if(EXISTS "${candidate}")
      set(${output_var}
        "@executable_path/../Frameworks/${framework}/Versions/${version}/${binary}"
        PARENT_SCOPE)
      return()
    endif()
  endif()

  get_filename_component(binary "${input_path}" NAME)
  set(candidate "${SIGNET_BUNDLE}/Contents/Frameworks/${binary}")
  if(EXISTS "${candidate}")
    set(${output_var} "@executable_path/../Frameworks/${binary}" PARENT_SCOPE)
    return()
  endif()

  set(${output_var} "" PARENT_SCOPE)
endfunction()

function(signet_expand_loader_path input_path macho_path output_var)
  get_filename_component(SIGNET_LOADER_DIR "${macho_path}" DIRECTORY)
  set(SIGNET_EXPANDED_PATH "${input_path}")
  string(REPLACE "@loader_path" "${SIGNET_LOADER_DIR}" SIGNET_EXPANDED_PATH
    "${SIGNET_EXPANDED_PATH}")
  string(REPLACE "@executable_path" "${SIGNET_BUNDLE}/Contents/MacOS" SIGNET_EXPANDED_PATH
    "${SIGNET_EXPANDED_PATH}")
  get_filename_component(SIGNET_EXPANDED_PATH "${SIGNET_EXPANDED_PATH}" ABSOLUTE)
  set(${output_var} "${SIGNET_EXPANDED_PATH}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE SIGNET_BUNDLE_FILES LIST_DIRECTORIES FALSE "${SIGNET_BUNDLE}/*")
list(REMOVE_DUPLICATES SIGNET_BUNDLE_FILES)

foreach(SIGNET_FILE IN LISTS SIGNET_BUNDLE_FILES)
  if(IS_SYMLINK "${SIGNET_FILE}")
    continue()
  endif()
  execute_process(
    COMMAND file "${SIGNET_FILE}"
    RESULT_VARIABLE SIGNET_FILE_RESULT
    OUTPUT_VARIABLE SIGNET_FILE_TYPE
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(NOT SIGNET_FILE_RESULT EQUAL 0 OR NOT SIGNET_FILE_TYPE MATCHES "Mach-O")
    continue()
  endif()

  execute_process(
    COMMAND otool -l "${SIGNET_FILE}"
    RESULT_VARIABLE SIGNET_LOAD_COMMAND_RESULT
    OUTPUT_VARIABLE SIGNET_LOAD_COMMANDS
    ERROR_VARIABLE SIGNET_LOAD_COMMAND_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT SIGNET_LOAD_COMMAND_RESULT EQUAL 0)
    message(FATAL_ERROR
      "otool -l failed for ${SIGNET_FILE}: ${SIGNET_LOAD_COMMAND_ERROR}")
  endif()

  string(REPLACE "\n" ";" SIGNET_LOAD_COMMAND_LINES "${SIGNET_LOAD_COMMANDS}")
  foreach(SIGNET_LOAD_COMMAND_LINE IN LISTS SIGNET_LOAD_COMMAND_LINES)
    if(NOT SIGNET_LOAD_COMMAND_LINE MATCHES "^[ \t]+path[ \t]+([^ \t]+)")
      continue()
    endif()
    set(SIGNET_RPATH "${CMAKE_MATCH_1}")
    if(SIGNET_RPATH MATCHES "^/System/|^/usr/lib/")
      continue()
    endif()
    if(SIGNET_RPATH MATCHES "^/")
      signet_execute(install_name_tool -delete_rpath "${SIGNET_RPATH}" "${SIGNET_FILE}")
    endif()
    if(SIGNET_RPATH MATCHES "^/")
      continue()
    endif()
  endforeach()

  execute_process(
    COMMAND otool -L "${SIGNET_FILE}"
    RESULT_VARIABLE SIGNET_OTOOL_RESULT
    OUTPUT_VARIABLE SIGNET_DEPENDENCIES
    ERROR_VARIABLE SIGNET_OTOOL_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT SIGNET_OTOOL_RESULT EQUAL 0)
    message(FATAL_ERROR
      "otool failed for ${SIGNET_FILE}: ${SIGNET_OTOOL_ERROR}")
  endif()

  string(REPLACE "\n" ";" SIGNET_DEPENDENCY_LINES "${SIGNET_DEPENDENCIES}")
  foreach(SIGNET_DEPENDENCY_LINE IN LISTS SIGNET_DEPENDENCY_LINES)
    if(NOT SIGNET_DEPENDENCY_LINE MATCHES "^[ \t]+([^ \t]+)[ \t]+\\(")
      continue()
    endif()
    set(SIGNET_DEPENDENCY "${CMAKE_MATCH_1}")
    if(SIGNET_DEPENDENCY MATCHES "^@rpath/")
      string(REGEX REPLACE "^@rpath/" "/" SIGNET_RPATH_AS_PATH "${SIGNET_DEPENDENCY}")
      signet_bundle_relative_path("${SIGNET_RPATH_AS_PATH}" SIGNET_RELATIVE_DEPENDENCY)
      if("${SIGNET_RELATIVE_DEPENDENCY}" STREQUAL "")
        message(FATAL_ERROR
          "Bundle-relative dependency is not bundled: ${SIGNET_DEPENDENCY}\n"
          "Referenced by: ${SIGNET_FILE}")
      endif()
      signet_execute(install_name_tool -change "${SIGNET_DEPENDENCY}"
        "${SIGNET_RELATIVE_DEPENDENCY}" "${SIGNET_FILE}")
      continue()
    endif()
    if(NOT SIGNET_DEPENDENCY MATCHES "^/")
      continue()
    endif()
    if(SIGNET_DEPENDENCY MATCHES "^/System/|^/usr/lib/")
      continue()
    endif()

    signet_bundle_relative_path("${SIGNET_DEPENDENCY}" SIGNET_RELATIVE_DEPENDENCY)
    if("${SIGNET_RELATIVE_DEPENDENCY}" STREQUAL "")
      message(FATAL_ERROR
        "Non-system absolute dependency is not bundled: ${SIGNET_DEPENDENCY}\n"
        "Referenced by: ${SIGNET_FILE}")
    endif()
    signet_execute(install_name_tool -change "${SIGNET_DEPENDENCY}"
      "${SIGNET_RELATIVE_DEPENDENCY}" "${SIGNET_FILE}")
  endforeach()

  execute_process(
    COMMAND otool -l "${SIGNET_FILE}"
    RESULT_VARIABLE SIGNET_POST_LOAD_COMMAND_RESULT
    OUTPUT_VARIABLE SIGNET_POST_LOAD_COMMANDS
    ERROR_VARIABLE SIGNET_POST_LOAD_COMMAND_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(NOT SIGNET_POST_LOAD_COMMAND_RESULT EQUAL 0)
    message(FATAL_ERROR
      "otool -l failed for ${SIGNET_FILE}: ${SIGNET_POST_LOAD_COMMAND_ERROR}")
  endif()
  string(REPLACE "\n" ";" SIGNET_POST_LOAD_COMMAND_LINES "${SIGNET_POST_LOAD_COMMANDS}")
  foreach(SIGNET_POST_LOAD_COMMAND_LINE IN LISTS SIGNET_POST_LOAD_COMMAND_LINES)
    if(NOT SIGNET_POST_LOAD_COMMAND_LINE MATCHES "^[ \t]+path[ \t]+([^ \t]+)")
      continue()
    endif()
    set(SIGNET_POST_RPATH "${CMAKE_MATCH_1}")
    if(SIGNET_POST_RPATH MATCHES "^/System/|^/usr/lib/")
      continue()
    endif()
    if(SIGNET_POST_RPATH MATCHES "^/")
      signet_execute(install_name_tool -delete_rpath "${SIGNET_POST_RPATH}" "${SIGNET_FILE}")
      continue()
    endif()
    signet_expand_loader_path("${SIGNET_POST_RPATH}" "${SIGNET_FILE}" SIGNET_EXPANDED_POST_RPATH)
    if(NOT IS_DIRECTORY "${SIGNET_EXPANDED_POST_RPATH}")
      signet_execute(install_name_tool -delete_rpath "${SIGNET_POST_RPATH}" "${SIGNET_FILE}")
    endif()
  endforeach()

  execute_process(
    COMMAND otool -D "${SIGNET_FILE}"
    RESULT_VARIABLE SIGNET_ID_RESULT
    OUTPUT_VARIABLE SIGNET_ID_OUTPUT
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  if(SIGNET_ID_RESULT EQUAL 0)
    string(REPLACE "\n" ";" SIGNET_ID_LINES "${SIGNET_ID_OUTPUT}")
    list(LENGTH SIGNET_ID_LINES SIGNET_ID_LINE_COUNT)
    math(EXPR SIGNET_ID_LAST_LINE "${SIGNET_ID_LINE_COUNT} - 1")
    list(GET SIGNET_ID_LINES ${SIGNET_ID_LAST_LINE} SIGNET_ID)
    string(REGEX REPLACE ":$" "" SIGNET_ID "${SIGNET_ID}")
    if(NOT SIGNET_ID STREQUAL "${SIGNET_FILE}" AND
       SIGNET_ID MATCHES "^/" AND
       NOT SIGNET_ID MATCHES "^/System/|^/usr/lib/")
      signet_bundle_relative_path("${SIGNET_ID}" SIGNET_RELATIVE_ID)
      if("${SIGNET_RELATIVE_ID}" STREQUAL "")
        message(FATAL_ERROR
          "Bundled Mach-O has an unresolvable absolute install name: ${SIGNET_ID}\n"
          "File: ${SIGNET_FILE}")
      endif()
      signet_execute(install_name_tool -id "${SIGNET_RELATIVE_ID}" "${SIGNET_FILE}")
    endif()
  endif()
endforeach()
