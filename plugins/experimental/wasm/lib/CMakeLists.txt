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

set(CC_FILES
    src/bytecode_util.cc
    src/context.cc
    src/exports.cc
    src/shared_data.cc
    src/shared_queue.cc
    src/hash.cc
    src/signature_util.cc
    src/vm_id_handle.cc
    src/pairs_util.cc
    src/wasm.cc
)
if(wamr_FOUND)
  list(APPEND CC_FILES src/wamr/wamr.cc)
elseif(wasmtime_FOUND)
  list(APPEND CC_FILES src/wasmtime/wasmtime.cc)
endif()
if(wasmedge_FOUND)
  list(APPEND CC_FILES src/wasmedge/wasmedge.cc)
endif()

add_library(wasmlib STATIC ${CC_FILES})
target_compile_options(wasmlib PUBLIC -Wno-unused-parameter)
if(wamr_FOUND)
  target_compile_options(wasmlib PRIVATE -Wno-missing-field-initializers)
  target_link_libraries(wasmlib PUBLIC wamr::wamr)
elseif(wasmtime_FOUND)
  target_link_libraries(wasmlib PUBLIC wasmtime::wasmtime)
endif()
if(wasmedge_FOUND)
  target_link_libraries(wasmlib PUBLIC wasmedge::wasmedge)
endif()

target_include_directories(wasmlib PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
set_target_properties(wasmlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
