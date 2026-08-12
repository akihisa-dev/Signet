cmake_minimum_required(VERSION 3.31)

if(NOT DEFINED SIGNET_BUNDLE OR NOT IS_DIRECTORY "${SIGNET_BUNDLE}")
  message(FATAL_ERROR "SIGNET_BUNDLE must name an existing .app directory")
endif()

if(NOT SIGNET_BUNDLE MATCHES "\\.app$")
  message(FATAL_ERROR "Expected an app bundle, got: ${SIGNET_BUNDLE}")
endif()

set(SIGNET_EXECUTABLE "${SIGNET_BUNDLE}/Contents/MacOS/Signet")
set(SIGNET_PLIST "${SIGNET_BUNDLE}/Contents/Info.plist")
set(SIGNET_LICENSES "${SIGNET_BUNDLE}/Contents/Resources/licenses")
set(SIGNET_MANIFEST "${SIGNET_LICENSES}/runtime/manifest.tsv")
set(SIGNET_CHECKSUM_MANIFEST "${SIGNET_LICENSES}/runtime/checksums.sha256")
set(SIGNET_EXPECTED_MINIMUM_OS "26.0")

if(NOT DEFINED SIGNET_EXPECTED_VERSION OR "${SIGNET_EXPECTED_VERSION}" STREQUAL "")
  message(FATAL_ERROR
    "SIGNET_EXPECTED_VERSION is required and must come from the CMake project version")
endif()

foreach(SIGNET_REQUIRED_PATH IN ITEMS
    "${SIGNET_EXECUTABLE}"
    "${SIGNET_PLIST}"
    "${SIGNET_BUNDLE}/Contents/PlugIns/platforms/libqcocoa.dylib"
    "${SIGNET_LICENSES}/Signet-LICENSE"
    "${SIGNET_LICENSES}/THIRD_PARTY_NOTICES.md"
    "${SIGNET_LICENSES}/runtime/CGAL-6.2/LICENSE"
    "${SIGNET_LICENSES}/runtime/gmp/COPYING"
    "${SIGNET_LICENSES}/runtime/mpfr/COPYING"
    "${SIGNET_LICENSES}/runtime/Qt-6.11.1/Qt-6.11.1-LICENSING-NOTICE.txt"
    "${SIGNET_LICENSES}/runtime/Qt-6.11.1/qt-6.11.1-sbom.spdx.json"
    "${SIGNET_MANIFEST}"
    "${SIGNET_CHECKSUM_MANIFEST}")
  if(NOT EXISTS "${SIGNET_REQUIRED_PATH}")
    message(FATAL_ERROR "Missing bundle path: ${SIGNET_REQUIRED_PATH}")
  endif()
endforeach()

function(signet_run output_var)
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
  if(NOT "${SIGNET_ERROR}" STREQUAL "")
    string(APPEND SIGNET_OUTPUT "\n${SIGNET_ERROR}")
  endif()
  set(${output_var} "${SIGNET_OUTPUT}" PARENT_SCOPE)
endfunction()

function(signet_verify_build_versions macho_path load_commands require_exact_minimum)
  string(REPLACE "\n" ";" SIGNET_LOAD_COMMAND_LINES "${load_commands}")
  set(SIGNET_IN_BUILD_VERSION FALSE)
  set(SIGNET_BUILD_VERSION_COUNT 0)
  set(SIGNET_MAIN_MINIMUM_OS_VALID TRUE)
  foreach(SIGNET_LOAD_COMMAND_LINE IN LISTS SIGNET_LOAD_COMMAND_LINES)
    if(SIGNET_LOAD_COMMAND_LINE MATCHES "^[ \t]+cmd LC_BUILD_VERSION$")
      set(SIGNET_IN_BUILD_VERSION TRUE)
      set(SIGNET_BUILD_PLATFORM "")
      set(SIGNET_BUILD_MINIMUM_OS "")
    elseif(SIGNET_IN_BUILD_VERSION AND
           SIGNET_LOAD_COMMAND_LINE MATCHES "^[ \t]+platform[ \t]+([^ \t]+)")
      set(SIGNET_BUILD_PLATFORM "${CMAKE_MATCH_1}")
    elseif(SIGNET_IN_BUILD_VERSION AND
           SIGNET_LOAD_COMMAND_LINE MATCHES "^[ \t]+minos[ \t]+([^ \t]+)")
      set(SIGNET_BUILD_MINIMUM_OS "${CMAKE_MATCH_1}")
    elseif(SIGNET_IN_BUILD_VERSION AND
           SIGNET_LOAD_COMMAND_LINE MATCHES "^[ \t]+sdk[ \t]+")
      if(SIGNET_BUILD_PLATFORM STREQUAL "" OR SIGNET_BUILD_MINIMUM_OS STREQUAL "")
        message(FATAL_ERROR
          "Malformed LC_BUILD_VERSION in ${macho_path}: ${load_commands}")
      endif()
      math(EXPR SIGNET_BUILD_VERSION_COUNT "${SIGNET_BUILD_VERSION_COUNT} + 1")
      if(NOT SIGNET_BUILD_PLATFORM STREQUAL "1")
        message(FATAL_ERROR
          "Non-macOS LC_BUILD_VERSION platform in ${macho_path}: ${SIGNET_BUILD_PLATFORM}")
      endif()
      if(SIGNET_BUILD_MINIMUM_OS VERSION_GREATER "${SIGNET_EXPECTED_MINIMUM_OS}")
        message(FATAL_ERROR
          "Mach-O minOS exceeds app target in ${macho_path}: ${SIGNET_BUILD_MINIMUM_OS}")
      endif()
      if(require_exact_minimum AND
         NOT SIGNET_BUILD_MINIMUM_OS STREQUAL "${SIGNET_EXPECTED_MINIMUM_OS}")
        set(SIGNET_MAIN_MINIMUM_OS_VALID FALSE)
      endif()
      set(SIGNET_IN_BUILD_VERSION FALSE)
    endif()
  endforeach()
  if(SIGNET_IN_BUILD_VERSION)
    message(FATAL_ERROR "Truncated LC_BUILD_VERSION in ${macho_path}")
  endif()
  if(SIGNET_BUILD_VERSION_COUNT EQUAL 0)
    message(FATAL_ERROR "Missing LC_BUILD_VERSION in ${macho_path}")
  endif()
  if(require_exact_minimum AND NOT SIGNET_MAIN_MINIMUM_OS_VALID)
    message(FATAL_ERROR
      "Unexpected executable LC_BUILD_VERSION minOS in ${macho_path}; expected ${SIGNET_EXPECTED_MINIMUM_OS}")
  endif()
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

function(signet_verify_checksum_manifest checksum_file base_dir label)
  if(NOT EXISTS "${checksum_file}")
    message(FATAL_ERROR "Missing ${label} checksum manifest: ${checksum_file}")
  endif()

  string(REPEAT "[0-9A-Fa-f]" 64 SIGNET_SHA256_PATTERN)
  file(STRINGS "${checksum_file}" SIGNET_CHECKSUM_LINES)
  set(SIGNET_CHECKSUM_PATHS)
  foreach(SIGNET_CHECKSUM_LINE IN LISTS SIGNET_CHECKSUM_LINES)
    string(STRIP "${SIGNET_CHECKSUM_LINE}" SIGNET_CHECKSUM_LINE)
    if("${SIGNET_CHECKSUM_LINE}" STREQUAL "")
      continue()
    endif()
    if(NOT SIGNET_CHECKSUM_LINE MATCHES "^(${SIGNET_SHA256_PATTERN})[ \t]+(.+)$")
      message(FATAL_ERROR
        "Malformed ${label} checksum entry: ${SIGNET_CHECKSUM_LINE}")
    endif()
    set(SIGNET_EXPECTED_HASH "${CMAKE_MATCH_1}")
    set(SIGNET_CHECKSUM_PATH "${CMAKE_MATCH_2}")
    # manifest.tsv is bundle inventory metadata, not a license text.  Its
    # bundle-file paths may change when the actual Mach-O closure changes, so
    # keep it outside the license-content checksum contract.
    if(SIGNET_CHECKSUM_PATH STREQUAL "manifest.tsv")
      continue()
    endif()
    if(IS_ABSOLUTE "${SIGNET_CHECKSUM_PATH}"
       OR "${SIGNET_CHECKSUM_PATH}" MATCHES "(^|/)\\.\\.(/|$)")
      message(FATAL_ERROR
        "Unsafe ${label} checksum path: ${SIGNET_CHECKSUM_PATH}")
    endif()
    list(FIND SIGNET_CHECKSUM_PATHS "${SIGNET_CHECKSUM_PATH}" SIGNET_DUPLICATE_INDEX)
    if(NOT SIGNET_DUPLICATE_INDEX EQUAL -1)
      message(FATAL_ERROR
        "Duplicate ${label} checksum path: ${SIGNET_CHECKSUM_PATH}")
    endif()
    list(APPEND SIGNET_CHECKSUM_PATHS "${SIGNET_CHECKSUM_PATH}")

    set(SIGNET_CHECKSUM_TARGET "${base_dir}/${SIGNET_CHECKSUM_PATH}")
    if(NOT EXISTS "${SIGNET_CHECKSUM_TARGET}" OR IS_DIRECTORY "${SIGNET_CHECKSUM_TARGET}")
      message(FATAL_ERROR
        "Missing ${label} checksum target: ${SIGNET_CHECKSUM_TARGET}")
    endif()
    signet_run(SIGNET_ACTUAL_HASH_OUTPUT shasum -a 256 "${SIGNET_CHECKSUM_TARGET}")
    if(NOT SIGNET_ACTUAL_HASH_OUTPUT MATCHES "^(${SIGNET_SHA256_PATTERN})[ \t]+")
      message(FATAL_ERROR
        "Unexpected shasum output for ${SIGNET_CHECKSUM_TARGET}: ${SIGNET_ACTUAL_HASH_OUTPUT}")
    endif()
    set(SIGNET_ACTUAL_HASH "${CMAKE_MATCH_1}")
    string(TOLOWER "${SIGNET_EXPECTED_HASH}" SIGNET_EXPECTED_HASH_LOWER)
    string(TOLOWER "${SIGNET_ACTUAL_HASH}" SIGNET_ACTUAL_HASH_LOWER)
    if(NOT SIGNET_EXPECTED_HASH_LOWER STREQUAL SIGNET_ACTUAL_HASH_LOWER)
      message(FATAL_ERROR
        "Checksum mismatch for ${label} file: ${SIGNET_CHECKSUM_PATH}")
    endif()
  endforeach()

  file(GLOB_RECURSE SIGNET_CHECKSUM_FILES LIST_DIRECTORIES FALSE "${base_dir}/*")
  foreach(SIGNET_CHECKSUM_TARGET IN LISTS SIGNET_CHECKSUM_FILES)
    file(RELATIVE_PATH SIGNET_CHECKSUM_PATH "${base_dir}" "${SIGNET_CHECKSUM_TARGET}")
    if(SIGNET_CHECKSUM_PATH STREQUAL "checksums.sha256"
       OR SIGNET_CHECKSUM_PATH STREQUAL "manifest.tsv")
      continue()
    endif()
    list(FIND SIGNET_CHECKSUM_PATHS "${SIGNET_CHECKSUM_PATH}" SIGNET_CHECKSUM_INDEX)
    if(SIGNET_CHECKSUM_INDEX EQUAL -1)
      message(FATAL_ERROR
        "${label} file is not covered by checksums.sha256: ${SIGNET_CHECKSUM_PATH}")
    endif()
  endforeach()
  message(STATUS "Verified ${label} checksums: ${SIGNET_CHECKSUM_PATHS}")
endfunction()

if(DEFINED SIGNET_SOURCE_DIR)
  signet_verify_checksum_manifest(
    "${SIGNET_SOURCE_DIR}/licenses/runtime/checksums.sha256"
    "${SIGNET_SOURCE_DIR}/licenses/runtime"
    "source runtime license")
endif()

file(STRINGS "${SIGNET_MANIFEST}" SIGNET_MANIFEST_LINES)
set(SIGNET_MANIFEST_MACHO_PATHS)
set(SIGNET_MANIFEST_ROW_COUNT 0)
foreach(SIGNET_MANIFEST_LINE IN LISTS SIGNET_MANIFEST_LINES)
  string(STRIP "${SIGNET_MANIFEST_LINE}" SIGNET_MANIFEST_LINE)
  if("${SIGNET_MANIFEST_LINE}" STREQUAL "" OR "${SIGNET_MANIFEST_LINE}" MATCHES "^#")
    continue()
  endif()
  string(REPLACE "|" ";" SIGNET_MANIFEST_FIELDS "${SIGNET_MANIFEST_LINE}")
  list(LENGTH SIGNET_MANIFEST_FIELDS SIGNET_FIELD_COUNT)
  if(NOT SIGNET_FIELD_COUNT EQUAL 6)
    message(FATAL_ERROR
      "Expected six manifest fields, got ${SIGNET_FIELD_COUNT}: ${SIGNET_MANIFEST_LINE}")
  endif()
  list(GET SIGNET_MANIFEST_FIELDS 0 SIGNET_COMPONENT)
  list(GET SIGNET_MANIFEST_FIELDS 3 SIGNET_BUNDLE_FIELD)
  list(GET SIGNET_MANIFEST_FIELDS 4 SIGNET_LICENSE_FIELD)
  if("${SIGNET_COMPONENT}" STREQUAL "" OR "${SIGNET_BUNDLE_FIELD}" STREQUAL ""
     OR "${SIGNET_LICENSE_FIELD}" STREQUAL "")
    message(FATAL_ERROR "Incomplete manifest row: ${SIGNET_MANIFEST_LINE}")
  endif()

  string(REPLACE "," ";" SIGNET_ROW_BUNDLE_PATHS "${SIGNET_BUNDLE_FIELD}")
  foreach(SIGNET_RELATIVE_PATH IN LISTS SIGNET_ROW_BUNDLE_PATHS)
    string(STRIP "${SIGNET_RELATIVE_PATH}" SIGNET_RELATIVE_PATH)
    if(IS_ABSOLUTE "${SIGNET_RELATIVE_PATH}"
       OR "${SIGNET_RELATIVE_PATH}" MATCHES "(^|/)\\.\\.(/|$)")
      message(FATAL_ERROR
        "Unsafe bundle path in manifest: ${SIGNET_RELATIVE_PATH}")
    endif()
    list(FIND SIGNET_MANIFEST_MACHO_PATHS "${SIGNET_RELATIVE_PATH}" SIGNET_DUPLICATE_INDEX)
    if(NOT SIGNET_DUPLICATE_INDEX EQUAL -1)
      message(FATAL_ERROR
        "Mach-O path appears in multiple manifest rows: ${SIGNET_RELATIVE_PATH}")
    endif()
    set(SIGNET_MANIFEST_TARGET "${SIGNET_BUNDLE}/${SIGNET_RELATIVE_PATH}")
    if(NOT EXISTS "${SIGNET_MANIFEST_TARGET}" OR IS_DIRECTORY "${SIGNET_MANIFEST_TARGET}")
      message(FATAL_ERROR
        "Manifest bundle path does not exist: ${SIGNET_RELATIVE_PATH}")
    endif()
    signet_run(SIGNET_MANIFEST_FILE_TYPE file "${SIGNET_MANIFEST_TARGET}")
    if(NOT SIGNET_MANIFEST_FILE_TYPE MATCHES "Mach-O")
      message(FATAL_ERROR
        "Manifest path is not Mach-O: ${SIGNET_RELATIVE_PATH}\n${SIGNET_MANIFEST_FILE_TYPE}")
    endif()
    list(APPEND SIGNET_MANIFEST_MACHO_PATHS "${SIGNET_RELATIVE_PATH}")
  endforeach()

  string(REPLACE "," ";" SIGNET_ROW_LICENSE_PATHS "${SIGNET_LICENSE_FIELD}")
  foreach(SIGNET_LICENSE_RELATIVE_PATH IN LISTS SIGNET_ROW_LICENSE_PATHS)
    string(STRIP "${SIGNET_LICENSE_RELATIVE_PATH}" SIGNET_LICENSE_RELATIVE_PATH)
    if(IS_ABSOLUTE "${SIGNET_LICENSE_RELATIVE_PATH}"
       OR "${SIGNET_LICENSE_RELATIVE_PATH}" MATCHES "(^|/)\\.\\.(/|$)")
      message(FATAL_ERROR
        "Unsafe license path in manifest: ${SIGNET_LICENSE_RELATIVE_PATH}")
    endif()
    set(SIGNET_LICENSE_TARGET "${SIGNET_LICENSES}/${SIGNET_LICENSE_RELATIVE_PATH}")
    if(NOT EXISTS "${SIGNET_LICENSE_TARGET}" OR IS_DIRECTORY "${SIGNET_LICENSE_TARGET}")
      message(FATAL_ERROR
        "Manifest license path does not exist for ${SIGNET_COMPONENT}: ${SIGNET_LICENSE_RELATIVE_PATH}")
    endif()
    file(SIZE "${SIGNET_LICENSE_TARGET}" SIGNET_LICENSE_SIZE)
    if(SIGNET_LICENSE_SIZE LESS 1)
      message(FATAL_ERROR
        "Manifest license file is empty for ${SIGNET_COMPONENT}: ${SIGNET_LICENSE_RELATIVE_PATH}")
    endif()
  endforeach()
  math(EXPR SIGNET_MANIFEST_ROW_COUNT "${SIGNET_MANIFEST_ROW_COUNT} + 1")
endforeach()
if(SIGNET_MANIFEST_ROW_COUNT EQUAL 0)
  message(FATAL_ERROR "Runtime license manifest has no component rows")
endif()

signet_verify_checksum_manifest(
  "${SIGNET_CHECKSUM_MANIFEST}"
  "${SIGNET_LICENSES}/runtime"
  "bundle runtime license")

signet_run(SIGNET_SHORT_VERSION plutil -extract CFBundleShortVersionString raw -o - "${SIGNET_PLIST}")
if(NOT SIGNET_SHORT_VERSION STREQUAL "${SIGNET_EXPECTED_VERSION}")
  message(FATAL_ERROR
    "Unexpected CFBundleShortVersionString: ${SIGNET_SHORT_VERSION}; expected ${SIGNET_EXPECTED_VERSION}")
endif()
signet_run(SIGNET_BUNDLE_VERSION plutil -extract CFBundleVersion raw -o - "${SIGNET_PLIST}")
if(NOT SIGNET_BUNDLE_VERSION STREQUAL "${SIGNET_EXPECTED_VERSION}")
  message(FATAL_ERROR
    "Unexpected CFBundleVersion: ${SIGNET_BUNDLE_VERSION}; expected ${SIGNET_EXPECTED_VERSION}")
endif()
signet_run(SIGNET_MINIMUM_OS plutil -extract LSMinimumSystemVersion raw -o - "${SIGNET_PLIST}")
if(NOT SIGNET_MINIMUM_OS STREQUAL "${SIGNET_EXPECTED_MINIMUM_OS}")
  message(FATAL_ERROR "Unexpected LSMinimumSystemVersion: ${SIGNET_MINIMUM_OS}")
endif()

file(GLOB_RECURSE SIGNET_BUNDLE_FILES LIST_DIRECTORIES FALSE "${SIGNET_BUNDLE}/*")
list(APPEND SIGNET_BUNDLE_FILES "${SIGNET_EXECUTABLE}")
list(REMOVE_DUPLICATES SIGNET_BUNDLE_FILES)
set(SIGNET_MACHO_FILES)
foreach(SIGNET_FILE IN LISTS SIGNET_BUNDLE_FILES)
  if(IS_SYMLINK "${SIGNET_FILE}")
    continue()
  endif()
  signet_run(SIGNET_FILE_TYPE file "${SIGNET_FILE}")
  if(SIGNET_FILE_TYPE MATCHES "Mach-O")
    list(APPEND SIGNET_MACHO_FILES "${SIGNET_FILE}")
    if(NOT SIGNET_FILE_TYPE MATCHES "arm64")
      message(FATAL_ERROR "Non-arm64 Mach-O in bundle: ${SIGNET_FILE}\n${SIGNET_FILE_TYPE}")
    endif()
    if(SIGNET_FILE_TYPE MATCHES "x86_64|i386|arm64e")
      message(FATAL_ERROR "Bundle contains a non-arm64 architecture: ${SIGNET_FILE}\n${SIGNET_FILE_TYPE}")
    endif()
    signet_run(SIGNET_DEPENDENCIES otool -L "${SIGNET_FILE}")
    if(SIGNET_DEPENDENCIES MATCHES "/opt/homebrew|/opt/local|/usr/local")
      message(FATAL_ERROR "External package-manager dependency in ${SIGNET_FILE}\n${SIGNET_DEPENDENCIES}")
    endif()
    if(DEFINED SIGNET_SOURCE_DIR AND SIGNET_DEPENDENCIES MATCHES "${SIGNET_SOURCE_DIR}")
      message(FATAL_ERROR "Source-tree dependency in ${SIGNET_FILE}\n${SIGNET_DEPENDENCIES}")
    endif()
    if(DEFINED SIGNET_BUILD_DIR AND SIGNET_DEPENDENCIES MATCHES "${SIGNET_BUILD_DIR}")
      message(FATAL_ERROR "Build-tree dependency in ${SIGNET_FILE}\n${SIGNET_DEPENDENCIES}")
    endif()

    signet_run(SIGNET_LOAD_COMMANDS otool -l "${SIGNET_FILE}")
    if("${SIGNET_FILE}" STREQUAL "${SIGNET_EXECUTABLE}")
      signet_verify_build_versions("${SIGNET_FILE}" "${SIGNET_LOAD_COMMANDS}" TRUE)
    else()
      signet_verify_build_versions("${SIGNET_FILE}" "${SIGNET_LOAD_COMMANDS}" FALSE)
    endif()
    string(REPLACE "\n" ";" SIGNET_LOAD_COMMAND_LINES "${SIGNET_LOAD_COMMANDS}")
    set(SIGNET_RPATHS)
    foreach(SIGNET_LOAD_COMMAND_LINE IN LISTS SIGNET_LOAD_COMMAND_LINES)
      if(SIGNET_LOAD_COMMAND_LINE MATCHES "^[ \t]+path[ \t]+([^ \t]+)")
        set(SIGNET_RPATH "${CMAKE_MATCH_1}")
        list(APPEND SIGNET_RPATHS "${SIGNET_RPATH}")
        if(SIGNET_RPATH MATCHES "^/System/|^/usr/lib/")
          continue()
        endif()
        if(SIGNET_RPATH MATCHES "^/")
          message(FATAL_ERROR "External absolute RPATH in ${SIGNET_FILE}: ${SIGNET_RPATH}")
        endif()
        signet_expand_loader_path("${SIGNET_RPATH}" "${SIGNET_FILE}" SIGNET_EXPANDED_RPATH)
        if(NOT IS_DIRECTORY "${SIGNET_EXPANDED_RPATH}")
          message(FATAL_ERROR
            "Bundle-relative RPATH does not exist in ${SIGNET_FILE}: ${SIGNET_RPATH}")
        endif()
      endif()
    endforeach()

    string(REPLACE "\n" ";" SIGNET_DEPENDENCY_LINES "${SIGNET_DEPENDENCIES}")
    foreach(SIGNET_DEPENDENCY_LINE IN LISTS SIGNET_DEPENDENCY_LINES)
      if(NOT SIGNET_DEPENDENCY_LINE MATCHES "^[ \t]+([^ \t]+)[ \t]+\\(")
        continue()
      endif()
      set(SIGNET_DEPENDENCY "${CMAKE_MATCH_1}")
      if(SIGNET_DEPENDENCY MATCHES "^@rpath/")
        string(REGEX REPLACE "^@rpath/" "" SIGNET_RPATH_DEPENDENCY "${SIGNET_DEPENDENCY}")
        set(SIGNET_RESOLVED_RPATH_DEPENDENCY FALSE)
        foreach(SIGNET_RPATH IN LISTS SIGNET_RPATHS)
          signet_expand_loader_path("${SIGNET_RPATH}" "${SIGNET_FILE}" SIGNET_EXPANDED_RPATH)
          if(EXISTS "${SIGNET_EXPANDED_RPATH}/${SIGNET_RPATH_DEPENDENCY}")
            set(SIGNET_RESOLVED_RPATH_DEPENDENCY TRUE)
          endif()
        endforeach()
        if(NOT SIGNET_RESOLVED_RPATH_DEPENDENCY)
          message(FATAL_ERROR
            "Bundle-relative dependency is not present: ${SIGNET_DEPENDENCY}\n"
            "Referenced by: ${SIGNET_FILE}")
        endif()
      elseif(SIGNET_DEPENDENCY MATCHES "^@loader_path/|^@executable_path/")
        signet_expand_loader_path("${SIGNET_DEPENDENCY}" "${SIGNET_FILE}" SIGNET_EXPANDED_DEPENDENCY)
        if(NOT EXISTS "${SIGNET_EXPANDED_DEPENDENCY}")
          message(FATAL_ERROR
            "Bundle-relative dependency does not exist: ${SIGNET_DEPENDENCY}\n"
            "Referenced by: ${SIGNET_FILE}")
        endif()
      elseif(SIGNET_DEPENDENCY MATCHES "^/")
        if(NOT SIGNET_DEPENDENCY MATCHES "^/System/|^/usr/lib/")
          message(FATAL_ERROR
            "External absolute dependency in ${SIGNET_FILE}: ${SIGNET_DEPENDENCY}")
        endif()
      endif()
    endforeach()
  endif()
endforeach()

if(NOT SIGNET_MACHO_FILES)
  message(FATAL_ERROR "No Mach-O files found in bundle")
endif()

foreach(SIGNET_FILE IN LISTS SIGNET_MACHO_FILES)
  file(RELATIVE_PATH SIGNET_RELATIVE_MACHO_PATH "${SIGNET_BUNDLE}" "${SIGNET_FILE}")
  list(FIND SIGNET_MANIFEST_MACHO_PATHS "${SIGNET_RELATIVE_MACHO_PATH}" SIGNET_MANIFEST_INDEX)
  if(SIGNET_MANIFEST_INDEX EQUAL -1)
    message(FATAL_ERROR
      "Bundled Mach-O is missing from runtime manifest: ${SIGNET_RELATIVE_MACHO_PATH}")
  endif()
endforeach()

list(LENGTH SIGNET_MACHO_FILES SIGNET_MACHO_COUNT)
list(LENGTH SIGNET_MANIFEST_MACHO_PATHS SIGNET_MANIFEST_MACHO_COUNT)
if(NOT SIGNET_MACHO_COUNT EQUAL SIGNET_MANIFEST_MACHO_COUNT)
  message(FATAL_ERROR
    "Runtime manifest Mach-O count (${SIGNET_MANIFEST_MACHO_COUNT}) does not match bundle (${SIGNET_MACHO_COUNT})")
endif()

message(STATUS "Verified ${SIGNET_BUNDLE}")
message(STATUS "Mach-O files checked: ${SIGNET_MACHO_COUNT}")
message(STATUS "Manifest rows checked: ${SIGNET_MANIFEST_ROW_COUNT}")
