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
configure_file(ext/extras/txnbox.cmake.in.py ext/extras/txnbox.py)
configure_file(conf.cmake.in.py conf.py)
configure_file(manpages.cmake.in.py manpages.py)

# copy static directory to build
add_custom_target(
  generate_docs_setup
  COMMENT "Copy static files to build directory"
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/static ${CMAKE_CURRENT_BINARY_DIR}/static
)

# generate svg files from uml files
set(UML_FILES
    uml/JsonRPCManager.uml
    uml/TLS-Bridge-Messages.uml
    uml/TLS-Bridge-Plugin.uml
    uml/hdr-heap-class.plantuml
    uml/hdr-heap-str-alloc.plantuml
    uml/host-resolve.plantuml
    uml/l4-basic-sequence.uml
    uml/l4-example-cdn-layout.uml
    uml/l4-pre-warming-overview.uml
    uml/l4-sni-routing-seq.uml
    uml/l4-tcp-routing.uml
    uml/traffic_ctl-class-diagram.uml
    uml/url_rewrite.plantuml
    uml/extras/config-data.plantuml
    uml/extras/txn_box_config_schema.plantuml
)
# unfortunately, sphinx can't look else for files than its source directory
# so these files must be create in the source tree
foreach(UML ${UML_FILES})
  cmake_path(GET UML STEM uml_name)
  list(APPEND SVG_FILES ${CMAKE_CURRENT_SOURCE_DIR}/uml/images/${uml_name}.svg)
  add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/uml/images/${uml_name}.svg
    COMMAND ${Java_JAVA_EXECUTABLE} -jar ${PLANTUML_JAR} -o ${CMAKE_CURRENT_SOURCE_DIR}/uml/images -tsvg
            ${CMAKE_CURRENT_SOURCE_DIR}/${UML}
    DEPENDS ${UML}
    VERBATIM
  )
endforeach()
add_custom_target(generate_svg_from_uml DEPENDS ${SVG_FILES})

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
  COMMAND ${RUNPIPENV} run python -m sphinx -vvv -c ${CMAKE_CURRENT_BINARY_DIR} -b html ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_BINARY_DIR}/docbuild/html
  DEPENDS generate_docs_setup generate_svg_from_uml Pipfile.lock
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

#add_custom_command(
#    TARGET generate_docs
#    POST_BUILD
#    COMMAND ${RUNPIPENV} --rm
#    COMMENT "Cleaning up pipenv"
#)
