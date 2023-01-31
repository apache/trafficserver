.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. _admin-plugins-wasm:


Wasm Plugin
***********

Description
===========

This plugins allows WebAssembly/Wasm modules compiled by Proxy-Wasm SDKs to be used in ATS.

See the documentation on specification for Proxy-Wasm at https://github.com/proxy-wasm/spec

C++ SDK is at https://github.com/proxy-wasm/proxy-wasm-cpp-sdk

Rust SDK is at https://github.com/proxy-wasm/proxy-wasm-rust-sdk

How it Works
============

The plugin uses the library and header files from the Proxy-Wasm project.

* https://github.com/proxy-wasm/proxy-wasm-cpp-host/tree/72ce32f7b11f9190edf874028255e1309e41690f
* https://github.com/proxy-wasm/proxy-wasm-cpp-sdk/tree/fd0be8405db25de0264bdb78fae3a82668c03782

Proxy-Wasm in turn uses an underlying WebAssembly runtime to execute the WebAssembly module. (Currently only WAMR is supported)

The plugin creates a root context when ATS starts and a new context will be created out of the root context for each
transaction. ATS plugin events will trigger the corresponding functions in the WebAssembly module to be executed through
the context. The context is also responsible to call the needed ATS functions when the WebAssembly module needs to do
so.

Compiling the Plugin
====================

* WAMR runtime needs CMake
* Compile and install WAMR

::

  wget https://github.com/bytecodealliance/wasm-micro-runtime/archive/c3d66f916ef8093e5c8cacf3329ed968f807cf58.tar.gz
  tar zxvf c3d66f916ef8093e5c8cacf3329ed968f807cf58.tar.gz
  cd wasm-micro-runtime-c3d66f916ef8093e5c8cacf3329ed968f807cf58
  cp core/iwasm/include/* /usr/local/include/
  cd product-mini/platforms/linux
  mkdir build
  cd build
  cmake .. -DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=1 -DWAMR_BUILD_JIT=0 -DWAMR_BUILD_AOT=0 -DWAMR_BUILD_SIMD=0 -DWAMR_BUILD_MULTI_MODULE=1 -DWAMR_BUILD_LIBC_WASI=0 -DWAMR_BUILD_TAIL_CALL=1 -DWAMR_DISABLE_HW_BOUND_CHECK=1 -DWAMR_BUILD_BULK_MEMORY=1 -DWAMR_BUILD_WASM_CACHE=0
  make
  sudo make install

* Configure ATS to compile with experimental plugins

::

  autoreconf -f -i
  ./configure --enable-debug=yes --enable-experimental-plugins=yes
  make
  sudo make install

Examples
========

Follow the C++, Rust and TinyGo examples in the examples directory. Instructions are included on how to compile and use
generated wasm modules with the plugin.

TODO
====

* Currently only the WAMR runtime is supported. We should also support V8, Wasmtime, and WasmEdge.
* Need to support functionality for retrieving and setting request/response body
* Need to support functionality for making async request call
* Need to support L4 lifecycle handler functions
* Support loading more than one Wasm module

Limitations
===========

The plugin will not support the following functionality as specified in Proxy-Wasm specification

* Getting and setting trailer request and response header
* Getting and setting HTTP/2 frame meta data
* Support on Grpc lifecycle handler functions

