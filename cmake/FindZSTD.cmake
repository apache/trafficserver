#=========================================================================
#
# Derived from the Visualization Toolkit (VTK), CMake/FindLZ4.cmake:
# https://gitlab.kitware.com/vtk/vtk
#
# Copyright (c) 1993-2015 Ken Martin, Will Schroeder, Bill Lorensen
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#  * Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
#  * Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
#  * Neither name of Ken Martin, Will Schroeder, or Bill Lorensen nor the names
#    of any contributors may be used to endorse or promote products derived
#    from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#=========================================================================

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
