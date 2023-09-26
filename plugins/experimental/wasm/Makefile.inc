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

AM_CPPFLAGS += @YAMLCPP_INCLUDES@

experimental_wasm_wasm_la_CPPFLAGS = -I$(srcdir)/experimental/wasm/lib -I$(srcdir)/experimental/wasm $(AM_CPPFLAGS)

if HAS_WAMR
experimental_wasm_wasm_la_CPPFLAGS += -DWAMR
else
  if HAS_WASMTIME
  experimental_wasm_wasm_la_CPPFLAGS += -DWASMTIME
  endif
endif

if HAS_WASMEDGE
experimental_wasm_wasm_la_CPPFLAGS += -DWASMEDGE
endif

experimental_wasm_wasm_la_LDFLAGS = = $(AM_LDFLAGS)

experimental_wasm_wasm_la_LIBADD =

if HAS_WAMR
experimental_wasm_wasm_la_LIBADD += $(WAMR_LIBS)
else
  if HAS_WASMTIME
  experimental_wasm_wasm_la_LIBADD += $(WASMTIME_LIBS)
  endif
endif

if HAS_WASMEDGE
experimental_wasm_wasm_la_LIBADD += $(WASMEDGE_LIBS)
endif

pkglib_LTLIBRARIES += experimental/wasm/wasm.la

experimental_wasm_wasm_la_SOURCES = \
    experimental/wasm/lib/src/bytecode_util.cc \
    experimental/wasm/lib/include/proxy-wasm/bytecode_util.h \
    experimental/wasm/lib/src/context.cc \
    experimental/wasm/lib/include/proxy-wasm/context.h \
    experimental/wasm/lib/include/proxy-wasm/context_interface.h \
    experimental/wasm/lib/src/exports.cc \
    experimental/wasm/lib/include/proxy-wasm/exports.h \
    experimental/wasm/lib/include/proxy-wasm/limits.h \
    experimental/wasm/lib/include/proxy-wasm/proxy_wasm_common.h \
    experimental/wasm/lib/include/proxy-wasm/proxy_wasm_enums.h \
    experimental/wasm/lib/src/shared_data.cc \
    experimental/wasm/lib/include/proxy-wasm/shared_data.h \
    experimental/wasm/lib/src/shared_queue.cc \
    experimental/wasm/lib/include/proxy-wasm/shared_queue.h \
    experimental/wasm/lib/src/hash.cc \
    experimental/wasm/lib/src/hash.h \
    experimental/wasm/lib/src/signature_util.cc \
    experimental/wasm/lib/include/proxy-wasm/signature_util.h \
    experimental/wasm/lib/src/vm_id_handle.cc \
    experimental/wasm/lib/include/proxy-wasm/vm_id_handle.h \
    experimental/wasm/lib/src/pairs_util.cc \
    experimental/wasm/lib/include/proxy-wasm/pairs_util.h \
    experimental/wasm/lib/include/proxy-wasm/wasm_vm.h \
    experimental/wasm/lib/src/wasm.cc \
    experimental/wasm/lib/include/proxy-wasm/wasm.h \
    experimental/wasm/ats_wasm.cc \
    experimental/wasm/ats_wasm.h \
    experimental/wasm/ats_context.cc \
    experimental/wasm/ats_context.h \
    experimental/wasm/wasm_main.cc \
    experimental/wasm/lib/src/common/types.h \
    experimental/wasm/lib/include/proxy-wasm/word.h

if HAS_WASMEDGE
experimental_wasm_wasm_la_SOURCES += \
    experimental/wasm/lib/src/wasmedge/wasmedge.cc \
    experimetnal/wasm/lib/src/wasmedge/types.h \
    experimental/wasm/lib/include/proxy-wasm/wasmedge.h
endif

if HAS_WAMR
experimental_wasm_wasm_la_SOURCES += \
    experimental/wasm/lib/src/wamr/wamr.cc \
    experimetnal/wasm/lib/src/wamr/types.h \
    experimental/wasm/lib/include/proxy-wasm/wamr.h
else
  if HAS_WASMTIME
  experimental_wasm_wasm_la_SOURCES += \
      experimental/wasm/lib/src/wasmtime/wasmtime.cc \
      experimetnal/wasm/lib/src/wasmtime/types.h \
      experimental/wasm/lib/include/proxy-wasm/wasmtime.h
  endif
endif
