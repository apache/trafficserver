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

# This script downloads and extracts proxy-verifier on first execution of autests.
# It is called by autest targets and autest.sh script.
#
# Required variables:
#   PROXY_VERIFIER_VERSION
#   PROXY_VERIFIER_HASH
#   ATS_SOURCE_DIR (for locating .git directory)
#   HOST_SYSTEM_NAME
#   HOST_SYSTEM_PROCESSOR

if(NOT PROXY_VERIFIER_VERSION)
  message(FATAL_ERROR "PROXY_VERIFIER_VERSION Required")
endif()

if(NOT PROXY_VERIFIER_HASH)
  message(FATAL_ERROR "PROXY_VERIFIER_HASH Required")
endif()

if(NOT ATS_SOURCE_DIR)
  message(FATAL_ERROR "ATS_SOURCE_DIR Required")
endif()

if(NOT HOST_SYSTEM_NAME)
  message(FATAL_ERROR "HOST_SYSTEM_NAME Required")
endif()

if(NOT HOST_SYSTEM_PROCESSOR)
  message(FATAL_ERROR "HOST_SYSTEM_PROCESSOR Required")
endif()

# Check that .git directory exists.
if(NOT EXISTS "${ATS_SOURCE_DIR}/.git")
  message(
    FATAL_ERROR "ENABLE_AUTEST requires a .git directory (developer-only feature). ${ATS_SOURCE_DIR}/.git not found."
  )
endif()

# Determine platform subdirectory.
if(HOST_SYSTEM_NAME STREQUAL "Linux")
  if(HOST_SYSTEM_PROCESSOR STREQUAL "x86_64"
     OR HOST_SYSTEM_PROCESSOR STREQUAL "amd64"
     OR HOST_SYSTEM_PROCESSOR STREQUAL "i686"
  )
    set(PV_SUBDIR "linux-amd64")
  elseif(HOST_SYSTEM_PROCESSOR STREQUAL "arm64" OR HOST_SYSTEM_PROCESSOR STREQUAL "aarch64")
    set(PV_SUBDIR "linux-arm64")
  else()
    message(FATAL_ERROR "Unknown processor ${HOST_SYSTEM_PROCESSOR}")
  endif()
elseif(HOST_SYSTEM_NAME STREQUAL "Darwin")
  if(HOST_SYSTEM_PROCESSOR STREQUAL "x86_64"
     OR HOST_SYSTEM_PROCESSOR STREQUAL "amd64"
     OR HOST_SYSTEM_PROCESSOR STREQUAL "i686"
  )
    set(PV_SUBDIR "darwin-amd64")
  elseif(HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
    set(PV_SUBDIR "darwin-arm64")
  else()
    message(FATAL_ERROR "Unknown processor ${HOST_SYSTEM_PROCESSOR}")
  endif()
else()
  message(FATAL_ERROR "Host ${HOST_SYSTEM_NAME} does not support running proxy verifier")
endif()

# Set paths.
set(PV_INSTALL_DIR "${ATS_SOURCE_DIR}/.git/proxy-verifier-${PROXY_VERIFIER_VERSION}")
set(PV_ARCHIVE "${ATS_SOURCE_DIR}/.git/proxy-verifier/proxy-verifier.tar.gz")
set(PROXY_VERIFIER_PATH "${PV_INSTALL_DIR}/${PV_SUBDIR}")

# Check if proxy-verifier is already installed.
if(EXISTS "${PROXY_VERIFIER_PATH}/verifier-client" AND EXISTS "${PROXY_VERIFIER_PATH}/verifier-server")
  message(STATUS "proxy-verifier ${PROXY_VERIFIER_VERSION} already present in ${PROXY_VERIFIER_PATH}")
else()
  message(STATUS "Downloading proxy-verifier ${PROXY_VERIFIER_VERSION}...")

  # Create directory for archive.
  file(MAKE_DIRECTORY "${ATS_SOURCE_DIR}/.git/proxy-verifier")

  # Download proxy-verifier.
  file(
    DOWNLOAD https://ci.trafficserver.apache.org/bintray/proxy-verifier-${PROXY_VERIFIER_VERSION}.tar.gz ${PV_ARCHIVE}
    EXPECTED_HASH ${PROXY_VERIFIER_HASH}
    SHOW_PROGRESS
  )

  # Extract to .git directory.
  message(STATUS "Extracting proxy-verifier to ${PV_INSTALL_DIR}...")
  file(ARCHIVE_EXTRACT INPUT ${PV_ARCHIVE} DESTINATION "${ATS_SOURCE_DIR}/.git")

  # Verify extraction succeeded.
  if(NOT EXISTS "${PROXY_VERIFIER_PATH}/verifier-client" OR NOT EXISTS "${PROXY_VERIFIER_PATH}/verifier-server")
    message(FATAL_ERROR "proxy-verifier extraction failed - binaries not found in ${PROXY_VERIFIER_PATH}")
  endif()

  message(STATUS "proxy-verifier ${PROXY_VERIFIER_VERSION} installed successfully in ${PROXY_VERIFIER_PATH}")
endif()
