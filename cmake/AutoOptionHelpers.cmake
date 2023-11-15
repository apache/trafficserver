#######################
#
#  Licensed to the Apache Software Foundation (ASF) under one or more contributor license
#  agreements.  See the NOTICE file distributed with this work for additional information regarding
#  copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
#  (the "License"); you may not use this file except in compliance with the License.  You may obtain
#  a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software distributed under the License
#  is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
#  or implied. See the License for the specific language governing permissions and limitations under
#  the License.
#
#######################

include(CheckIncludeFile)

set(GLOBAL_AUTO_OPTION_VARS "")

function(_REGISTER_AUTO_OPTION _NAME _FEATURE_VAR _DESCRIPTION _DEFAULT)
  add_custom_target(${_NAME}_target)
  set_target_properties(${_NAME}_target PROPERTIES AUTO_OPTION_FEATURE_VAR ${_FEATURE_VAR})

  set(${_NAME}
      ${_DEFAULT}
      CACHE STRING "${_DESCRIPTION}"
  )
  set_property(CACHE ${_NAME} PROPERTY STRINGS AUTO ON OFF)

  set(LOCAL_AUTO_OPTION_VARS ${GLOBAL_AUTO_OPTION_VARS})
  list(APPEND LOCAL_AUTO_OPTION_VARS "${_NAME}")
  set(GLOBAL_AUTO_OPTION_VARS
      ${LOCAL_AUTO_OPTION_VARS}
      PARENT_SCOPE
  )
endfunction()

macro(_CHECK_PACKAGE_DEPENDS _OPTION_VAR _PACKAGE_DEPENDS _FEATURE_VAR)
  if(${${_OPTION_VAR}} STREQUAL AUTO)
    set(STRICTNESS)
  else()
    set(STRICTNESS REQUIRED)
  endif()

  foreach(PACKAGE_NAME ${_PACKAGE_DEPENDS})
    find_package(${PACKAGE_NAME} ${STRICTNESS})
    if(NOT ${PACKAGE_NAME}_FOUND)
      set(${_FEATURE_VAR} FALSE)
      break()
    endif()
  endforeach()
endmacro()

macro(_CHECK_HEADER_DEPENDS _OPTION_VAR _HEADER_DEPENDS _FEATURE_VAR)
  foreach(HEADER_NAME ${_HEADER_DEPENDS})
    check_include_file(${HEADER_NAME} HEADER_FOUND)
    if(NOT HEADER_FOUND)
      set(${_FEATURE_VAR} FALSE)
      break()
    endif()
  endforeach()
endmacro()

# auto_option(<feature_name>
#   [DESCRIPTION <description>]
#   [DEFAULT <default>]
#   [FEATURE_VAR <feature_var>]
#   [WITH_SUBDIRECTORY <name>]
#   [PACKAGE_DEPENDS <package_one> <package_two> ...]
#   [HEADER_DEPENDS <header_one> <header_two> ...]
# )
#
# This macro registers a new auto option and sets its corresponding feature
# variable based on the requirements. The option it creates will be named
# ENABLE_<feature_name>, and the default feature variable will be
# USE_<feature_name>.
#
# It is necessary to have separate variables for the option and the feature
# because the option may be AUTO, but the feature must be enabled or not.
# It is expected that the option will have one of the values ON, OFF, or AUTO,
# and the feature will have one of the values TRUE, or FALSE.
#
# Behavior of the option is as follows:
#  - The option is falsey: the feature variable will be FALSE.
#  - The option is AUTO: if all requirements are satisfied, the
#      feature variable will be TRUE, otherwise FALSE.
#  - The option is truthy: if all requirements are satisfied, the
#      feature variable will be TRUE, otherwise a fatal error is produced.
#
# DESCRIPTION is the description that will go with the cache entry for the
# option. If not provided, it will be empty.
#
# DEFAULT is the default value of the option. Permitted values are OFF, FALSE, 0
# ON, TRUE, 1, AUTO. If no default is provided, AUTO is the default.
#
# FEATURE_VAR is the variable that will represent whether the feature should be
# used, given the value of the option and whether the requirements for the
# feature are satisfied. By default, it is USE_<feature_name>.
#
# WITH_SUBDIRECTORY is an optional subdirectory that should be added if the
# feature is enabled.
#
# PACKAGE_DEPENDS is a list of packages that are required for the feature.
#
# HEADER_DEPENDS is a list of headers that are required for the feature.
#
macro(auto_option _FEATURE_NAME)
  cmake_parse_arguments(
    ARG "" "DESCRIPTION;DEFAULT;FEATURE_VAR;WITH_SUBDIRECTORY" "PACKAGE_DEPENDS;HEADER_DEPENDS" ${ARGN}
  )

  set(OPTION_VAR "ENABLE_${_FEATURE_NAME}")
  if(ARG_FEATURE_VAR)
    set(FEATURE_VAR ${ARG_FEATURE_VAR})
  else()
    set(FEATURE_VAR "USE_${_FEATURE_NAME}")
  endif()

  if(ARG_DEFAULT STREQUAL OFF)
    set(DEFAULT OFF)
  elseif(NOT ARG_DEFAULT)
    set(DEFAULT AUTO)
  elseif(ARG_DEFAULT MATCHES "(ON)|(AUTO)|(TRUE)|(1)")
    set(DEFAULT ${ARG_DEFAULT})
  else()
    message(FATAL_ERROR "Invalid auto_option default ${ARG_DEFAULT}")
  endif()

  _register_auto_option(${OPTION_VAR} ${FEATURE_VAR} "${ARG_DESCRIPTION}" "${DEFAULT}")

  if(${${OPTION_VAR}} MATCHES "(ON)|(AUTO)|(TRUE)|(1)")
    set(${FEATURE_VAR} TRUE)
    _check_package_depends(${OPTION_VAR} "${ARG_PACKAGE_DEPENDS}" ${FEATURE_VAR})
    _check_header_depends(${OPTION_VAR} "${ARG_HEADER_DEPENDS}" ${FEATURE_VAR})
  else()
    set(${FEATURE_VAR} FALSE)
  endif()

  if(ARG_WITH_SUBDIRECTORY AND ${${FEATURE_VAR}})
    add_subdirectory(${ARG_WITH_SUBDIRECTORY})
  endif()
endmacro()

# Prints a colorized summary of one auto option.
function(PRINT_AUTO_OPTION _NAME)
  string(ASCII 27 ESC)
  set(COLOR_RED "${ESC}[31m")
  set(COLOR_GREEN "${ESC}[32m")
  set(COLOR_RESET "${ESC}[m")

  set(OPTION_VALUE ${${_NAME}})

  get_target_property(FEATURE "${_NAME}_target" AUTO_OPTION_FEATURE_VAR)
  set(FEATURE_VALUE ${${FEATURE}})

  set(RESET ${COLOR_RESET})
  if(FEATURE_VALUE)
    # This accounts for both the ON and AUTO cases.
    if(OPTION_VALUE)
      set(COLOR ${COLOR_GREEN})
      set(HINT "(hint: add -D${_NAME}=OFF to disable)")
    else()
      message(WARNING "${FEATURE} truthy but ${_NAME} isn't!")
      set(COLOR ${COLOR_RED})
      # There is no sensible hint for this case - things are
      # on fire.
      set(HINT "")
    endif()
  else()
    # This should account only for the AUTO case.
    # If the option is set to ON but dependencies were
    # missing, then we should have errored out already.
    if(OPTION_VALUE)
      set(COLOR ${COLOR_RED})
      set(HINT "(hint: add -D${_NAME}=ON to require)")
    else()
      set(RESET "")
      set(HINT "(hint: add -D${_NAME}=ON to enable)")
    endif()
  endif()

  message(STATUS "${FEATURE}: ${COLOR}${FEATURE_VALUE}${RESET} (${OPTION_VALUE}) ${HINT}")
endfunction()

# Prints out a colorized summary of all auto options.
function(PRINT_AUTO_OPTIONS_SUMMARY)
  message(STATUS "")
  message(STATUS "-------- AUTO OPTIONS SUMMARY --------")
  foreach(OPTION_NAME ${GLOBAL_AUTO_OPTION_VARS})
    print_auto_option(${OPTION_NAME})
  endforeach()
  message(STATUS "")
endfunction()
