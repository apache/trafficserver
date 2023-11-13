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

# ConfigureTransparentProxy.cmake
#
# The following variables should be set
#
#     ENABLE_TPROXY to one of AUTO, NO, or a number
#     TS_USE_POSIX_CAP to a boolean value
#
# This will define the following variables
#
#     TS_USE_TPROXY
#     TS_IP_TRANSPARENT
#

set(TS_IP_TRANSPARENT 0)

if(ENABLE_TPROXY STREQUAL "NO")
  set(TS_USE_TPROXY FALSE)
  return()
endif()

if(NOT TS_USE_POSIX_CAP)
  if(ENABLE_TPROXY STREQUAL "AUTO")
    set(TS_USE_TPROXY FALSE)
  else()
    message(FATAL_ERROR "ENABLE_TPROXY requires POSIX capabilities.")
  endif()
  return()
endif()

if(ENABLE_TPROXY STREQUAL "FORCE")
  set(TS_USE_TPROXY TRUE)
  set(TS_IP_TRANSPARENT 19)
  return()
endif()

if(ENABLE_TPROXY MATCHES "([0-9]+)")
  set(TS_USE_TPROXY TRUE)
  set(TS_IP_TRANSPARENT ${CMAKE_MATCH_1})
  return()
endif()

# If the read fails, it will print out a confusing error. This
# is to make it clear why the error is happening in that case.
message(STATUS "ENABLE_TPROXY enabled, looking for value in /usr/include/linux/in.h")
file(READ "/usr/include/linux/in.h" HEADER_CONTENTS)
if(HEADER_CONTENTS MATCHES "#define[ \t]+IP_TRANSPARENT[ \t]+([0-9]+)")
  set(TS_USE_TPROXY TRUE)
  set(TS_IP_TRANSPARENT ${CMAKE_MATCH_1})
else()
  if(ENABLE_TPROXY STREQUAL "AUTO")
    set(TS_USE_TPROXY FALSE)
  else()
    message(FATAL_ERROR "ENABLE_TPROXY on but IP_TRANSPARENT symbol not found")
  endif()
endif()

unset(HEADER_CONTENTS)
