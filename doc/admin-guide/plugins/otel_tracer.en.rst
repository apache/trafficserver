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

.. _admin-plugins-otel-tracer:


OpenTelemetry Tracer Plugin
***************************

Description
===========

This plugin allows ATS to participate in OpenTelemetry distributed tracing system.
The plugin attaches B3 headers to the transaction and sends trace information to an OTLP HTTP endpoint, typically provided through a OpenTelemetry collector.

See the documentation on OpenTelemetry at https://opentelemetry.io/docs/.

How it Works
============

This plugin checks incoming requests for the B3 headers.
If they are present, the plugin will participate in the same trace and generate a new span ID.
If the header is not present, the plugin will generate new trace ID and span ID.
These information will be added as request headers for transaction going upstream.
Also together with transaction information such as incoming URL, method, response code, etc, it will be sent to a OTLP HTTP endpoint.

Sample B3 headers:

::

  X-B3-Sampled: 1
  X-B3-SpanId: 2e56605382810dc9
  X-B3-TraceId: b9e48850a375247c2c416174648844f4

Compiling the Plugin
====================

To compile this plugin, we need nlohmann-json, protobuf and opentelemetry-cpp

nlohmann-json:

::

  cd
  wget https://github.com/nlohmann/json/archive/refs/tags/v3.9.1.tar.gz
  tar zxvf v3.9.1.tar.gz
  cd json-3.9.1
  mkdir build
  cd build
  cmake .. -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON
  make
  make install

protobuf:

::

  cd
  wget https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.19.4.tar.gz
  tar zxvf v3.19.4.tar.gz
  cd protobuf-3.19.4
  ./autogen.sh
  ./configure --enable-shared=no --enable-static=yes CXXFLAGS="-std=c++17 -fPIC" CFLAGS="-fPIC"
  make
  make install

opentelemetry-cpp

::

  cd
  wget https://github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v1.3.0.tar.gz
  tar zxvf v1.3.0.tar.gz
  cd opentelemetry-cpp-1.3.0
  mkdir build
  cd build
  cmake .. -DBUILD_TESTING=OFF -DWITH_EXAMPLES=OFF -DWITH_JAEGER=OFF -DWITH_OTLP=ON -DWITH_OTLP_GRPC=OFF -DWITH_OTLP_HTTP=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON
  cmake --build . --target all
  cmake --install . --config Debug --prefix /usr/local/

Installation
============

The `OpenTelemetry Tracer` plugin is a :term:`global plugin`.  Enable it by adding ``otel_tracer.so`` to your :file:`plugin.config`.

Configuration
=============

The plugin supports the following options:

* ``-u=[OTLP HTTP url endpoint]`` (default: ``http://localhost:4317/v1/traces``)

This is the OTLP HTTP url endpoint, typically provided by a OpenTelemetry collector.

* ``-s=[service name]`` (default: ``otel_tracer``)

This is the service name that will be sent as part of the information to the OTLP HTTP url endpoint.

* ``-r=[sampling rate]`` (default: ``1.0``)

The value can be between 0.0 to 1.0. It controls the sampling rate of the trace information.
