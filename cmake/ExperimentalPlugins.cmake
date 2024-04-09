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

# These are all the auto options for experimental plugins. They are contained in a
# module instead of a subdirectory because auto_options added in a subdirectory are
# not visible to their parent directories.

# Note that this default only works on the first run before the options are cached.
# Once the options are cached they must be adjusted manually.
if(BUILD_EXPERIMENTAL_PLUGINS)
  set(_DEFAULT AUTO)
else()
  set(_DEFAULT OFF)
endif()

auto_option(
  MAXMIND_ACL
  FEATURE_VAR
  BUILD_MAXMIND_ACL
  WITH_SUBDIRECTORY
  plugins/experimental/maxmind_acl
  PACKAGE_DEPENDS
  maxminddb
  DEFAULT
  ${_DEFAULT}
)
auto_option(
  STEK_SHARE
  FEATURE_VAR
  BUILD_STEK_SHARE
  WITH_SUBDIRECTORY
  plugins/experimental/stek_share
  PACKAGE_DEPENDS
  nuraft
  DEFAULT
  ${_DEFAULT}
)
auto_option(
  URI_SIGNING
  FEATURE_VAR
  BUILD_URI_SIGNING
  WITH_SUBDIRECTORY
  plugins/experimental/uri_signing
  PACKAGE_DEPENDS
  cjose
  jansson
  DEFAULT
  ${_DEFAULT}
)
auto_option(
  OTEL_TRACER
  FEATURE_VAR
  BUILD_OTEL_TRACER
  WITH_SUBDIRECTORY
  plugins/experimental/otel_tracer
  PACKAGE_DEPENDS
  opentelemetry
  Protobuf
  CURL
  DEFAULT
  ${_DEFAULT}
)

unset(_DEFAULT)
