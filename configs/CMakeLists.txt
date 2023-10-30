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

# This is odd but seems to be the idiomatic way to pass arguments to install scripts
install(
  CODE "set(CONFIG_SOURCE_GLOBS \"${CMAKE_BINARY_DIR}/configs/*.default\" \"${CMAKE_SOURCE_DIR}/configs/*.default\")"
)
install(CODE "set(CONFIG_DEST_PATH \"${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_SYSCONFDIR}\")")

install(SCRIPT ${CMAKE_SOURCE_DIR}/cmake/install_configs.cmake)

file(GLOB BODY_FACTORY_FILES body_factory/default/*)
list(FILTER BODY_FACTORY_FILES EXCLUDE REGEX Makefile.*)

foreach(BODY_FACTORY_FILE ${BODY_FACTORY_FILES})
  install(FILES ${BODY_FACTORY_FILE} DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/body_factory/default)
endforeach()
