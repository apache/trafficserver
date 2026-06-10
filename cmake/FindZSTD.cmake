find_path(
  ZSTD_INCLUDE_DIR
  NAMES zstd.h
  DOC "zstd include directory"
)
mark_as_advanced(ZSTD_INCLUDE_DIR)
find_library(
  ZSTD_LIBRARY
  NAMES zstd libzstd
  DOC "zstd library"
)
mark_as_advanced(ZSTD_LIBRARY)

if(ZSTD_INCLUDE_DIR)
  file(STRINGS "${ZSTD_INCLUDE_DIR}/zstd.h" _zstd_version_lines REGEX "#define[ \t]+ZSTD_VERSION_(MAJOR|MINOR|RELEASE)")
  string(REGEX REPLACE ".*ZSTD_VERSION_MAJOR *\([0-9]*\).*" "\\1" _zstd_version_major "${_zstd_version_lines}")
  string(REGEX REPLACE ".*ZSTD_VERSION_MINOR *\([0-9]*\).*" "\\1" _zstd_version_minor "${_zstd_version_lines}")
  string(REGEX REPLACE ".*ZSTD_VERSION_RELEASE *\([0-9]*\).*" "\\1" _zstd_version_release "${_zstd_version_lines}")
  set(ZSTD_VERSION "${_zstd_version_major}.${_zstd_version_minor}.${_zstd_version_release}")
  unset(_zstd_version_major)
  unset(_zstd_version_minor)
  unset(_zstd_version_release)
  unset(_zstd_version_lines)
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  ZSTD
  REQUIRED_VARS ZSTD_LIBRARY ZSTD_INCLUDE_DIR
  VERSION_VAR ZSTD_VERSION
)

if(ZSTD_FOUND)
  set(ZSTD_INCLUDE_DIRS "${ZSTD_INCLUDE_DIR}")
  set(ZSTD_LIBRARIES "${ZSTD_LIBRARY}")

  if(NOT TARGET zstd::zstd)
    add_library(zstd::zstd UNKNOWN IMPORTED)
    set_target_properties(
      zstd::zstd PROPERTIES IMPORTED_LOCATION "${ZSTD_LIBRARY}" INTERFACE_INCLUDE_DIRECTORIES "${ZSTD_INCLUDE_DIR}"
    )
  endif()
endif()
