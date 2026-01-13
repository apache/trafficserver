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

set(PLANTUML_DATE "1.2025.10")
set(PLANTUML_ARCHIVE ${CMAKE_CURRENT_BINARY_DIR}/plantuml-${PLANTUML_DATE}.tar.bz2)
set(PLANTUML_JAR ${CMAKE_CURRENT_BINARY_DIR}/plantuml-${PLANTUML_DATE}/plantuml.jar)

file(DOWNLOAD https://ci.trafficserver.apache.org/bintray/plantuml-${PLANTUML_DATE}.tar.bz2 ${PLANTUML_ARCHIVE}
     EXPECTED_HASH SHA1=248b535bb361119d154e3435dfe5382ddeca55d6
)
file(ARCHIVE_EXTRACT INPUT ${PLANTUML_ARCHIVE} PATTERNS *.jar)

configure_file(ext/local-config.cmake.in.py ext/local-config.py)
configure_file(ext/traffic-server.cmake.in.py ext/traffic-server.py)
configure_file(ext/extras/txnbox.cmake.in.py ext/extras/txnbox.py)
configure_file(conf.cmake.in.py conf.py @ONLY)
configure_file(manpages.cmake.in.py manpages.py)

# copy static directory to build
add_custom_target(
  generate_docs_setup
  COMMENT "Copy static files to build directory"
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/static ${CMAKE_CURRENT_BINARY_DIR}/static
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/_templates
          ${CMAKE_CURRENT_BINARY_DIR}/_templates
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
    COMMAND ${CMAKE_COMMAND} -E env GRAPHVIZ_DOT=${GRAPHVIZ_DOT} ${Java_JAVA_EXECUTABLE} -Djava.awt.headless=true
            -Dapple.awt.UIElement=true -jar ${PLANTUML_JAR} -o ${CMAKE_CURRENT_SOURCE_DIR}/uml/images -tsvg
            ${CMAKE_CURRENT_SOURCE_DIR}/${UML}
    DEPENDS ${UML}
    VERBATIM
  )
endforeach()
add_custom_target(generate_svg_from_uml DEPENDS ${SVG_FILES})

# Docs are built with python so we need a target to setup the virtual environment.
# Copy pyproject.toml to build directory and use uv to manage dependencies.
# uv creates .venv in the project directory by default.
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml ${CMAKE_CURRENT_BINARY_DIR}/pyproject.toml COPYONLY)
set(RUNUV ${UV} --project ${CMAKE_CURRENT_BINARY_DIR})
# Create a marker file to indicate uv setup is complete.
add_custom_command(
  OUTPUT .uv_installed
  COMMAND ${RUNUV} sync
  COMMAND ${CMAKE_COMMAND} -E touch .uv_installed
  COMMENT "Setup uv virtual environment in build directory (${CMAKE_CURRENT_BINARY_DIR}/.venv) and install packages"
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/pyproject.toml
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  VERBATIM
)

add_custom_target(
  generate_docs
  COMMAND ${CMAKE_COMMAND} -E env "JAVA_TOOL_OPTIONS=-Djava.awt.headless=true -Dapple.awt.UIElement=true" ${RUNUV} run
          python ${CMAKE_CURRENT_SOURCE_DIR}/checkvers.py --check-version
  COMMAND ${CMAKE_COMMAND} -E env "JAVA_TOOL_OPTIONS=-Djava.awt.headless=true -Dapple.awt.UIElement=true" ${RUNUV} run
          python -m sphinx -W -c ${CMAKE_CURRENT_BINARY_DIR} -b html ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_BINARY_DIR}/docbuild/html
  DEPENDS generate_docs_setup .uv_installed
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Sphinx PlantUML extension now generates diagrams automatically in build tree"
)
# Generate PDF documentation (Letter paper size - US standard)
add_custom_target(
  generate_pdf
  COMMAND ${CMAKE_COMMAND} -E env "JAVA_TOOL_OPTIONS=-Djava.awt.headless=true -Dapple.awt.UIElement=true" ${RUNUV} run
          python ${CMAKE_CURRENT_SOURCE_DIR}/checkvers.py --check-version
  COMMAND ${CMAKE_COMMAND} -E env "JAVA_TOOL_OPTIONS=-Djava.awt.headless=true -Dapple.awt.UIElement=true" ${RUNUV} run
          python -m sphinx -c ${CMAKE_CURRENT_BINARY_DIR} -b latex -t latex_paper ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_BINARY_DIR}/docbuild/latex
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}/docbuild/latex latexmk -pdf -interaction=nonstopmode -f
          ApacheTrafficServer.tex
  DEPENDS generate_docs_setup .uv_installed
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Building PDF documentation with Sphinx and LaTeX (Letter paper, PlantUML generates PNG automatically)"
)

# Generate PDF documentation (A4 paper size - International standard)
add_custom_target(
  generate_pdf_a4
  COMMAND ${CMAKE_COMMAND} -E env "JAVA_TOOL_OPTIONS=-Djava.awt.headless=true -Dapple.awt.UIElement=true" ${RUNUV} run
          python ${CMAKE_CURRENT_SOURCE_DIR}/checkvers.py --check-version
  COMMAND ${CMAKE_COMMAND} -E env "JAVA_TOOL_OPTIONS=-Djava.awt.headless=true -Dapple.awt.UIElement=true" ${RUNUV} run
          python -m sphinx -c ${CMAKE_CURRENT_BINARY_DIR} -b latex -t latex_a4 ${CMAKE_CURRENT_SOURCE_DIR}
          ${CMAKE_CURRENT_BINARY_DIR}/docbuild/latex-a4
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_CURRENT_BINARY_DIR}/docbuild/latex-a4 latexmk -pdf -interaction=nonstopmode
          -f ApacheTrafficServer.tex
  DEPENDS generate_docs_setup .uv_installed
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Building PDF documentation with Sphinx and LaTeX (A4 paper, PlantUML generates PNG automatically)"
)
