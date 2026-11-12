set(TTP_BUILD_VERSION "" CACHE STRING "Beijing yyyy.MM.dd[pN] build version")
find_program(TTP_POWERSHELL NAMES powershell pwsh REQUIRED)
set(_version_args)
if(NOT TTP_BUILD_VERSION STREQUAL "")
  list(APPEND _version_args -Version "${TTP_BUILD_VERSION}")
endif()
add_custom_target(${TTP_PLUGIN_ID}_version
  COMMAND "${TTP_POWERSHELL}" -NoProfile -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/cmake/write_version.ps1"
    -Template "${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc.in"
    -OutputPath "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc" ${_version_args}
  BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/generated/version.rc" VERBATIM)
