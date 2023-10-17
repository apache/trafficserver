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

set(PLANTUML_DATE "1.2018.1")
set(PLANTUML_ARCHIVE ${CMAKE_CURRENT_BINARY_DIR}/plantuml-${PLANTUML_DATE}.tar.bz2)
set(PLANTUML_JAR ${CMAKE_CURRENT_BINARY_DIR}/plantuml-${PLANTUML_DATE}/plantuml.jar)

file(DOWNLOAD https://ci.trafficserver.apache.org/bintray/plantuml-${PLANTUML_DATE}.tar.bz2 ${PLANTUML_ARCHIVE}
     EXPECTED_HASH SHA1=4dbf218641a777007f9bc72ca8017a41a23e1081
)
file(ARCHIVE_EXTRACT INPUT ${PLANTUML_ARCHIVE} PATTERNS *.jar)

configure_file(ext/local-config.cmake.in.py ext/local-config.py)
configure_file(ext/traffic-server.cmake.in.py ext/traffic-server.py)
configure_file(conf.cmake.in.py conf.py)
configure_file(manpages.cmake.in.py manpages.py)

# Docs are built with python so we need a target to setup pipenv
set(RUNPIPENV PIPENV_PIPFILE=${CMAKE_CURRENT_SOURCE_DIR}/Pipfile ${PipEnv})
add_custom_command(
  OUTPUT Pipfile.lock
  COMMAND ${RUNPIPENV} install
  COMMENT "Setup pipenv"
  DEPENDS Pipfile
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_target(
  generate_docs
  COMMAND ${RUNPIPENV} run python ${CMAKE_CURRENT_SOURCE_DIR}/checkvers.py --check-version
  COMMAND ${RUNPIPENV} run python -m sphinx -c ${CMAKE_CURRENT_BINARY_DIR} -b html ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_BINARY_DIR}/docbuild/html
  DEPENDS Pipfile.lock
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
#add_custom_command(
#    TARGET generate_docs
#    POST_BUILD
#    COMMAND ${RUNPIPENV} --rm
#    COMMENT "Cleaning up pipenv"
#)
