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


.. include:: ../../common.defs

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

* https://github.com/proxy-wasm/proxy-wasm-cpp-host/tree/c4d7bb0fda912e24c64daf2aa749ec54cec99412
* https://github.com/proxy-wasm/proxy-wasm-cpp-sdk/tree/fd0be8405db25de0264bdb78fae3a82668c03782

Proxy-Wasm in turn uses an underlying WebAssembly runtime to execute the WebAssembly module. (Currently only WAMR,
Wasmtime and WasmEdge are supported)

The plugin creates a root context when ATS starts and a new context will be created out of the root context for each
transaction. ATS plugin events will trigger the corresponding functions in the WebAssembly module to be executed through
the context. The context is also responsible to call the needed ATS functions when the WebAssembly module needs to do
so.

Compiling the Plugin
====================

* The plugin requires either WAMR, Wasmtime or WasmEdge.

* Please note that WAMR and Wasmtime have conflict with each other and the plugin can only be compiled with one of them. If both are installed, WAMR will take priority.

**Compile and install WAMR (CMake is required)**

::

  wget https://github.com/bytecodealliance/wasm-micro-runtime/archive/refs/tags/WAMR-2.1.1.tar.gz
  tar zxvf WAMR-2.1.1.tar.gz
  cd wasm-micro-runtime-WAMR-2.1.1
  cp core/iwasm/include/* /usr/local/include/
  cd product-mini/platforms/linux
  mkdir build
  cd build
  cmake .. -DWAMR_BUILD_INTERP=1 -DWAMR_BUILD_FAST_INTERP=1 -DWAMR_BUILD_JIT=0 -DWAMR_BUILD_AOT=0 -DWAMR_BUILD_SIMD=0 -DWAMR_BUILD_MULTI_MODULE=1 -DWAMR_BUILD_LIBC_WASI=0 -DWAMR_BUILD_TAIL_CALL=1 -DWAMR_DISABLE_HW_BOUND_CHECK=1 -DWAMR_BUILD_BULK_MEMORY=1 -DWAMR_BUILD_WASM_CACHE=0
  make
  sudo make install

**Compile and install Wasmtime (Rust is required)**

::

  git clone https://github.com/bytecodealliance/wasmtime.git
  cd wasmtime/
  git checkout release-24.0.0
  git submodule update --init
  cargo build
  cargo build --release --manifest-path crates/c-api/Cargo.toml
  sudo cp target/release/libwasmtime.so /usr/local/lib
  sudo mkdir -p /usr/local/include/crates/c-api/include/
  sudo cp crates/c-api/include/wasm.h /usr/local/include/crates/c-api/include/

**Install WasmEdge**

::

  wget https://github.com/WasmEdge/WasmEdge/archive/refs/tags/proxy-wasm/0.13.1.tar.gz
  tar zxvf 0.13.1.tar.gz
  cd WasmEdge-proxy-wasm-0.13.1/utils
  ./install.sh

* Copy contents from ~/.wasmedge/include to /usr/local/include
* Copy contents from ~/.wasmedge/lib to /usr/local/lib
* The installation script will make changes to your environment variables. You can comment those out for now before compiling the plugin.

**Configure ATS to compile with experimental plugins**

To make this plugin available, you must enable experimental plugins when
building |TS| by passing the ``-DBUILD_EXPERIMENTAL_PLUGINS=ON`` to the ``cmake`` command
when building.

::

   $ cmake ...  -DCMAKE_BUILD_TYPE="Debug" -DBUILD_EXPERIMENTAL_PLUGINS=ON
   $ cmake --build ...
   $ sudo cmake --install ...

Examples
========

Follow the C++, Rust and TinyGo examples in the examples directory. Instructions are included on how to compile and use
generated wasm modules with the plugin.

Runtime can be chosen by changing the ``runtime`` field inside the yaml configuration file for the plugin.
``ats.wasm.runtime.wamr`` is for WAMR. ``ats.wasm.runtime.wasmtime`` is Wasmtime while ``ats.wasm.runtime.wasmedge`` is for WasmEdge.

The plugin can also take more than one yaml file as arguments and can thus load more than one wasm modules.

TODO
====

* Currently only the WAMR, Wasmtime and WasmEdge runtime is supported. We should also support V8.

Limitations
===========

The plugin will not support the following functionality as specified in Proxy-Wasm specification

* Getting and setting trailer request and response header
* Getting and setting HTTP/2 frame meta data
* Support asynchronous request call in the start handler function of the plugin lifecycle
* Support on Grpc lifecycle handler functions
* Support for L4 lifecycle handler functions

