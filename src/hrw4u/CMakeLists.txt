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

find_package(antlr4-runtime QUIET)

if(NOT antlr4-runtime_FOUND)
  message(STATUS "ANTLR4 runtime not found - hrw4u library will not be built")
  set(TS_HAS_CPP_HRW4U
      FALSE
      PARENT_SCOPE
  )
  return()
endif()

find_program(ANTLR4_EXECUTABLE antlr4 NAMES antlr4 antlr)

if(NOT ANTLR4_EXECUTABLE)
  message(STATUS "ANTLR4 tool not found - hrw4u library will not be built")
  set(TS_HAS_CPP_HRW4U
      FALSE
      PARENT_SCOPE
  )
  return()
endif()

# Verify tool version matches runtime (ANTLR4 C++ API breaks between minor versions).
if(ANTLR_VERSION)
  execute_process(
    COMMAND ${ANTLR4_EXECUTABLE}
    OUTPUT_VARIABLE _antlr4_tool_stdout
    ERROR_VARIABLE _antlr4_tool_stderr
    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_STRIP_TRAILING_WHITESPACE
    TIMEOUT 10
  )
  set(_antlr4_tool_output "${_antlr4_tool_stdout} ${_antlr4_tool_stderr}")
  string(REGEX MATCH "[Vv]ersion[ \t]+([0-9]+\\.[0-9]+\\.?[0-9]*)" _antlr4_version_match "${_antlr4_tool_output}")
  set(_antlr4_tool_version "${CMAKE_MATCH_1}")
  unset(_antlr4_tool_stdout)
  unset(_antlr4_tool_stderr)
  unset(_antlr4_tool_output)
  unset(_antlr4_version_match)

  if(_antlr4_tool_version)
    string(REGEX MATCH "^([0-9]+\\.[0-9]+)" _tool_major_minor "${_antlr4_tool_version}")
    string(REGEX MATCH "^([0-9]+\\.[0-9]+)" _runtime_major_minor "${ANTLR_VERSION}")
    if(NOT _tool_major_minor STREQUAL _runtime_major_minor)
      message(WARNING "ANTLR4 version mismatch: tool is ${_antlr4_tool_version}, runtime is ${ANTLR_VERSION}. "
                      "hrw4u will not be built. Install matching versions to enable hrw4u."
      )
      set(TS_HAS_CPP_HRW4U
          FALSE
          PARENT_SCOPE
      )
      return()
    endif()
    message(STATUS "ANTLR4 tool version ${_antlr4_tool_version} matches runtime ${ANTLR_VERSION}")
    unset(_tool_major_minor)
    unset(_runtime_major_minor)
  else()
    message(STATUS "Could not determine ANTLR4 tool version, proceeding with runtime ${ANTLR_VERSION}")
  endif()
  unset(_antlr4_tool_version)
endif()

set(TS_HAS_CPP_HRW4U
    TRUE
    PARENT_SCOPE
)
message(STATUS "Building hrw4u library with ANTLR4 support")

set(HRW4U_GRAMMAR_FILE "${PROJECT_SOURCE_DIR}/tools/hrw4u/grammar/hrw4u.g4")
set(HRW4U_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY ${HRW4U_GENERATED_DIR})

set(HRW4U_GENERATED_SOURCES ${HRW4U_GENERATED_DIR}/hrw4uLexer.cpp ${HRW4U_GENERATED_DIR}/hrw4uParser.cpp
                            ${HRW4U_GENERATED_DIR}/hrw4uBaseVisitor.cpp ${HRW4U_GENERATED_DIR}/hrw4uVisitor.cpp
)

set(HRW4U_GENERATED_HEADERS ${HRW4U_GENERATED_DIR}/hrw4uLexer.h ${HRW4U_GENERATED_DIR}/hrw4uParser.h
                            ${HRW4U_GENERATED_DIR}/hrw4uBaseVisitor.h ${HRW4U_GENERATED_DIR}/hrw4uVisitor.h
)

add_custom_command(
  OUTPUT ${HRW4U_GENERATED_SOURCES} ${HRW4U_GENERATED_HEADERS}
  COMMAND ${ANTLR4_EXECUTABLE} -Dlanguage=Cpp -visitor -no-listener -o ${HRW4U_GENERATED_DIR} ${HRW4U_GRAMMAR_FILE}
  DEPENDS ${HRW4U_GRAMMAR_FILE}
  COMMENT "Generating ANTLR4 C++ parser for hrw4u"
  VERBATIM
)

add_custom_target(hrw4u_generated DEPENDS ${HRW4U_GENERATED_SOURCES})

set_source_files_properties(${HRW4U_GENERATED_SOURCES} PROPERTIES COMPILE_FLAGS "-Wno-unused-parameter")

set(HRW4U_SOURCES Error.cc Types.cc Tables.cc Visitor.cc HRW4UVisitorImpl.cc)

add_library(hrw4u STATIC ${HRW4U_SOURCES} ${HRW4U_GENERATED_SOURCES})
add_library(ts::hrw4u ALIAS hrw4u)

# GCC errors on Clang-style designated initializers under -Werror.
target_compile_options(hrw4u PRIVATE -Wno-missing-field-initializers)
set_target_properties(hrw4u PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_dependencies(hrw4u hrw4u_generated)

target_include_directories(
  hrw4u
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include> $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${HRW4U_GENERATED_DIR} ${CMAKE_CURRENT_SOURCE_DIR}
)

# Prefer shared ANTLR4 runtime (distro static libs may lack -fPIC).
if(TARGET antlr4_shared)
  target_link_libraries(hrw4u PUBLIC antlr4_shared)
elseif(TARGET antlr4_static)
  target_link_libraries(hrw4u PUBLIC antlr4_static)
endif()

# Fallback for distro packages that don't set INTERFACE_INCLUDE_DIRECTORIES.
if(ANTLR4_INCLUDE_DIR)
  target_include_directories(hrw4u PUBLIC ${ANTLR4_INCLUDE_DIR})
endif()

set(HRW4U_PUBLIC_HEADERS
    ${PROJECT_SOURCE_DIR}/include/hrw4u/Error.h ${PROJECT_SOURCE_DIR}/include/hrw4u/Types.h
    ${PROJECT_SOURCE_DIR}/include/hrw4u/Tables.h ${PROJECT_SOURCE_DIR}/include/hrw4u/Visitor.h
    ${PROJECT_SOURCE_DIR}/include/hrw4u/HRW4UVisitor.h
)

set_target_properties(hrw4u PROPERTIES PUBLIC_HEADER "${HRW4U_PUBLIC_HEADERS}")

install(TARGETS hrw4u PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/hrw4u)

clang_tidy_check(hrw4u)
