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

auto_option(ACCESS_CONTROL FEATURE_VAR BUILD_ACCESS_CONTROL DEFAULT ${_DEFAULT})
auto_option(BLOCK_ERRORS FEATURE_VAR BUILD_BLOCK_ERRORS DEFAULT ${_DEFAULT})
auto_option(CACHE_FILL FEATURE_VAR BUILD_CACHE_FILL DEFAULT ${_DEFAULT})
auto_option(CERT_REPORTING_TOOL FEATURE_VAR BUILD_CERT_REPORTING_TOOL DEFAULT ${_DEFAULT})
auto_option(COOKIE_REMAP FEATURE_VAR BUILD_COOKIE_REMAP DEFAULT ${_DEFAULT})
auto_option(CUSTOM_REDIRECT FEATURE_VAR BUILD_CUSTOM_REDIRECT DEFAULT ${_DEFAULT})
auto_option(FQ_PACING FEATURE_VAR BUILD_FQ_PACING DEFAULT ${_DEFAULT})
auto_option(GEOIP_ACL FEATURE_VAR BUILD_GEOIP_ACL DEFAULT ${_DEFAULT})
auto_option(HEADER_FREQ FEATURE_VAR BUILD_HEADER_FREQ DEFAULT ${_DEFAULT})
auto_option(HOOK_TRACE FEATURE_VAR BUILD_HOOK_TRACE DEFAULT ${_DEFAULT})
auto_option(HTTP_STATS FEATURE_VAR BUILD_HTTP_STATS DEFAULT ${_DEFAULT})
auto_option(ICAP FEATURE_VAR BUILD_ICAP DEFAULT ${_DEFAULT})
auto_option(INLINER FEATURE_VAR BUILD_INLINER DEFAULT ${_DEFAULT})
auto_option(JA4_FINGERPRINT FEATURE_VAR BUILD_JA4_FINGERPRINT DEFAULT ${_DEFAULT})
auto_option(
  MAGICK
  FEATURE_VAR
  BUILD_MAGICK
  PACKAGE_DEPENDS
  ImageMagick
  COMPONENTS
  Magick++
  MagickWand
  MagickCore
  DEFAULT
  ${_DEFAULT}
)
auto_option(
  MAXMIND_ACL
  FEATURE_VAR
  BUILD_MAXMIND_ACL
  PACKAGE_DEPENDS
  maxminddb
  DEFAULT
  ${_DEFAULT}
)
auto_option(MEMCACHE FEATURE_VAR BUILD_MEMCACHE DEFAULT ${_DEFAULT})
auto_option(MEMORY_PROFILE FEATURE_VAR BUILD_MEMORY_PROFILE DEFAULT ${_DEFAULT})
auto_option(MONEY_TRACE FEATURE_VAR BUILD_MONEY_TRACE DEFAULT ${_DEFAULT})
auto_option(MP4 FEATURE_VAR BUILD_MP4 DEFAULT ${_DEFAULT})
auto_option(
  OTEL_TRACER
  FEATURE_VAR
  BUILD_OTEL_TRACER
  PACKAGE_DEPENDS
  opentelemetry
  Protobuf
  CURL
  DEFAULT
  ${_DEFAULT}
)
auto_option(RATE_LIMIT FEATURE_VAR BUILD_RATE_LIMIT DEFAULT ${_DEFAULT})
auto_option(REDO_CACHE_LOOKUP FEATURE_VAR BUILD_REDO_CACHE_LOOKUP DEFAULT ${_DEFAULT})
auto_option(SSLHEADERS FEATURE_VAR BUILD_SSLHEADERS DEFAULT ${_DEFAULT})
auto_option(STALE_RESPONSE FEATURE_VAR BUILD_STALE_RESPONSE DEFAULT ${_DEFAULT})
auto_option(
  STEK_SHARE
  FEATURE_VAR
  BUILD_STEK_SHARE
  PACKAGE_DEPENDS
  nuraft
  DEFAULT
  ${_DEFAULT}
)
auto_option(STREAM_EDITOR FEATURE_VAR BUILD_STREAM_EDITOR DEFAULT ${_DEFAULT})
auto_option(SYSTEM_STATS FEATURE_VAR BUILD_SYSTEM_STATS DEFAULT ${_DEFAULT})
auto_option(TLS_BRIDGE FEATURE_VAR BUILD_TLS_BRIDGE DEFAULT ${_DEFAULT})
auto_option(TXN_BOX FEATURE_VAR BUILD_TXN_BOX DEFAULT ${_DEFAULT})
auto_option(
  URI_SIGNING
  FEATURE_VAR
  BUILD_URI_SIGNING
  PACKAGE_DEPENDS
  cjose
  jansson
  DEFAULT
  ${_DEFAULT}
)
auto_option(URL_SIG FEATURE_VAR BUILD_URL_SIG DEFAULT ${_DEFAULT})
auto_option(
  WASM
  FEATURE_VAR
  BUILD_WASM
  PACKAGE_DEPENDS
  wamr
  DEFAULT
  ${_DEFAULT}
)

unset(_DEFAULT)
