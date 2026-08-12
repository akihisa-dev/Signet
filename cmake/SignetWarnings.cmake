# SPDX-License-Identifier: AGPL-3.0-or-later

function(signet_set_project_warnings target)
  target_compile_options(${target} PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wnon-virtual-dtor
  )
endfunction()
