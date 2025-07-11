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

  wget https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
  tar zxvf v3.11.3.tar.gz
  cd json-3.11.3
  cmake -B build -DBUILD_SHARED_LIBS=OFF -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON
  cmake --build build --config Release --parallel --verbose
  sudo cmake --install build --prefix /usr/local/

protobuf:

::

  cd
  wget https://github.com/protocolbuffers/protobuf/archive/refs/tags/v3.21.12.tar.gz
  tar zxvf v3.21.12.tar.gz
  cd protobuf-3.21.12
  cmake -B build -Dprotobuf_BUILD_SHARED_LIBS=OFF -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  cmake --build build --config Release --parallel --verbose
  sudo cmake --install build --prefix /usr/local/

opentelemetry-cpp

::

  cd
  wget https://github.com/open-telemetry/opentelemetry-cpp/archive/refs/tags/v1.19.0.tar.gz
  tar zxvf v1.19.0.tar.gz
  cd opentelemetry-cpp-1.19.0
  cmake -B build -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DWITH_EXAMPLES=OFF -DWITH_OTLP_GRPC=OFF -DWITH_OTLP_HTTP=ON -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_CXX_STANDARD=17 -DCMAKE_CXX_STANDARD_REQUIRED=ON -DWITH_ABSEIL=OFF
  cmake --build build --config Release --parallel --verbose
  sudo cmake --install build --prefix /usr/local/

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

* ``-q=[queue size]`` (default: ``25``)

The size of the batch processor queue.

* ``-d=[delay]`` (default: ``3000``)

The time interval between two consecutive exports in milliseconds.

* ``-b=[batch size]`` (default: ``10``)

The maximum batch size of every export. Should be smaller than queue size.
