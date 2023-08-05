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

set(GLOBAL_AUTO_OPTION_VARS "")

function(_REGISTER_AUTO_OPTION _NAME _FEATURE_VAR _DESCRIPTION)
  add_custom_target(${_NAME}_target)
  set_target_properties(${_NAME}_target
    PROPERTIES
      AUTO_OPTION_FEATURE_VAR ${_FEATURE_VAR}
  )

  set(${_NAME} AUTO CACHE STRING ${_DESCRIPTION})
  set_property(CACHE ${_NAME} PROPERTY STRINGS AUTO ON OFF)

  set(LOCAL_AUTO_OPTION_VARS ${GLOBAL_AUTO_OPTION_VARS})
  list(APPEND LOCAL_AUTO_OPTION_VARS "${_NAME}")
  set(GLOBAL_AUTO_OPTION_VARS ${LOCAL_AUTO_OPTION_VARS} PARENT_SCOPE)
endfunction()

# Add a new auto feature that uses find_package.
# Creates an option ENABLE_<PACKAGE_NAME>.
macro(AUTO_FEATURE_PACKAGE _PACKAGE_NAME _FEATURE_VAR _DESCRIPTION)
  set(OPTION_VAR ENABLE_${_PACKAGE_NAME})

  _register_auto_option(${OPTION_VAR} ${_FEATURE_VAR} ${_DESCRIPTION})

  if(${OPTION_VAR} STREQUAL AUTO)
    find_package(${_PACKAGE_NAME} QUIET)
  elseif(${OPTION_VAR})
    find_package(${_PACKAGE_NAME} REQUIRED)
  endif()

  # This is for consistency so all feature vars are TRUE or FALSE.
  if(${_PACKAGE_NAME}_FOUND)
    set(${_FEATURE_VAR} TRUE)
  else()
    set(${_FEATURE_VAR} FALSE)
  endif()

  unset(OPTION_VAR)
  unset(FEATURE_VAR)
endmacro()

macro(AUTO_OFF_FEATURE_PACKAGE _PACKAGE_NAME _FEATURE_VAR _DESCRIPTION)
  # Need to set our cache string before the default one gets set.
  set(ENABLE_${_PACKAGE_NAME} OFF CACHE STRING ${_DESCRIPTION})
  auto_feature_package(${_PACKAGE_NAME} ${_FEATURE_VAR} ${_DESCRIPTION})
endmacro()

# Prints a colorized summary of one auto option.
function(PRINT_AUTO_OPTION _NAME)
  string(ASCII 27 ESC)
  set(COLOR_RED   "${ESC}[31m")
  set(COLOR_GREEN "${ESC}[32m")
  set(COLOR_RESET "${ESC}[m")

  set(OPTION_VALUE ${${_NAME}})

  get_target_property(FEATURE "${_NAME}_target" AUTO_OPTION_FEATURE_VAR)
  set(FEATURE_VALUE ${${FEATURE}})

  set(RESET ${COLOR_RESET})
  if(FEATURE_VALUE)
    if(OPTION_VALUE)
      set(COLOR ${COLOR_GREEN})
    else()
      message(WARNING "${FEATURE} truthy but ${_NAME} isn't!")
      set(COLOR ${COLOR_RED})
    endif()
  else()
    if(OPTION_VALUE)
      set(COLOR ${COLOR_RED})
    else()
      set(RESET "")
    endif()
  endif()

  message(STATUS "${FEATURE}: ${COLOR}${FEATURE_VALUE}${RESET} (${OPTION_VALUE})")
endfunction()

# Prints out a colorized summary of all auto options.
function(PRINT_AUTO_OPTIONS_SUMMARY)
  message(STATUS "")
  message(STATUS "-------- AUTO OPTIONS SUMMARY")
  foreach(OPTION_NAME ${GLOBAL_AUTO_OPTION_VARS})
    print_auto_option(${OPTION_NAME})
  endforeach()
  message(STATUS "")
endfunction()
