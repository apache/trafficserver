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

# This is a script intended to be passed to install(SCRIPT ...)
# This copies default config files to the destination without overwriting existing files.
# If the config file doesn't exist, the source config file is copied  without the '.default' extension
# If the config file already exists, the source config file is copied with the '.default' extension


file(GLOB CONFIG_FILES ${CONFIG_SOURCE_GLOBS})
file(MAKE_DIRECTORY "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CONFIG_DEST_PATH}")

foreach(CONFIG_FILE ${CONFIG_FILES})
  # remove the '.default' extension from the path
  cmake_path(GET CONFIG_FILE STEM LAST_ONLY CONFIG_FILE_NAME)
  set(DEST_FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${CONFIG_DEST_PATH}/${CONFIG_FILE_NAME}")
  if (EXISTS ${DEST_FILE})
    set(DEST_FILE "${DEST_FILE}.default")
  endif()
  message(STATUS "Installing config: ${DEST_FILE}")
  # Prefer copy_file but we need to support 3.20 which doesn't have this feature so fall back to
  # configure_file
  if(CMAKE_MINOR_VERSION GREATER 20)
    file(COPY_FILE ${CONFIG_FILE} ${DEST_FILE})
  else()
    configure_file(${CONFIG_FILE} ${DEST_FILE} COPYONLY)
  endif()
endforeach()
