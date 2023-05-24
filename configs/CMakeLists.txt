# Makefile.am for config
#
#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

file(GLOB CONFIG_FILES *.default ${CMAKE_BINARY_DIR}/configs/*.default)

foreach(CONFIG_FILE ${CONFIG_FILES})
  cmake_path(GET CONFIG_FILE STEM LAST_ONLY CONFIG_FILE_NAME)
  install(FILES ${CONFIG_FILE} DESTINATION etc/trafficserver RENAME ${CONFIG_FILE_NAME})
endforeach()


file(GLOB BODY_FACTORY_FILES body_factory/default/*)
list(FILTER BODY_FACTORY_FILES EXCLUDE REGEX Makefile.*)

foreach(BODY_FACTORY_FILE ${BODY_FACTORY_FILES})
  install(FILES ${BODY_FACTORY_FILE} DESTINATION etc/trafficserver/body_factory/default)
endforeach()
