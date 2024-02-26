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

# Findopentelemetry.cmake
#
# This will define the following variables
#
#     opentelemetry_FOUND
#     opentelemetry_LIBRARY
#     opentelemetry_INCLUDE_DIRS
#
# and the following imported targets
#
#     opentelemetry::opentelemetry
#

#opentelemetry has a lot of libraries
set(OTEL_LIBS
    opentelemetry_exporter_ostream_span
    opentelemetry_exporter_otlp_http
    opentelemetry_exporter_otlp_http_client
    opentelemetry_http_client_curl
    opentelemetry_metrics
    opentelemetry_otlp_recordable
    opentelemetry_proto
    opentelemetry_resources
    opentelemetry_trace
    opentelemetry_version
    opentelemetry_common
)

find_path(opentelemetry_INCLUDE_DIR NAMES opentelemetry/version.h)

foreach(OTLIB ${OTEL_LIBS})
  set(OTLIB_NAME ${OTLIB}_LIBRARY)
  find_library(${OTLIB_NAME} NAMES ${OTLIB})
  list(APPEND OTEL_LIBRARIES ${OTLIB_NAME})
endforeach()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(opentelemetry REQUIRED_VARS opentelemetry_INCLUDE_DIR ${OTEL_LIBRARIES})

if(opentelemetry_FOUND)
  mark_as_advanced(opentelemetry_FOUND ${OTEL_LIBRARIES})
  set(opentelemetry_INCLUDE_DIRS "${opentelemetry_INCLUDE_DIR}")

  foreach(OTELLIB ${OTEL_LIBRARIES})
    list(APPEND opentelemetry_LIBRARIES ${${OTELLIB}})
  endforeach()
  message(STATUS "Opentelemetry found: ${opentelemetry_LIBRARIES}")
  message(STATUS "Opentelemetry include: ${opentelemetry_INCLUDE_DIRS}")

  if(NOT TARGET opentelemetry::opentelemetry)
    add_library(opentelemetry::opentelemetry INTERFACE IMPORTED)
    target_include_directories(opentelemetry::opentelemetry INTERFACE ${opentelemetry_INCLUDE_DIRS})
    target_link_libraries(opentelemetry::opentelemetry INTERFACE ${opentelemetry_LIBRARIES})
  endif()
endif()
