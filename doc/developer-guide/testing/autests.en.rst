.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied.  See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. _autest-writing:

Writing End-to-End Tests
************************

|TS| uses Catch2 for unit tests and pytest plus AuTest for end-to-end tests.
Catch2 tests reside next to the associated source code. End-to-end tests are
under ``tests/gold_tests/``. New replay-driven tests are collected directly by
pytest; tests that need bespoke process orchestration continue to use AuTest.

* For catch test framework documentation, see: https://github.com/catchorg/Catch2
* For AuTest framework documentation, see: https://autestsuite.bitbucket.io/index.html

This document focuses on end-to-end tests because Catch2 documentation is
available elsewhere.

File Structure and Naming
==========================

The end-to-end runners use distinct filename conventions:

- Tests are placed in appropriate subdirectories under
  ``tests/gold_tests/`` (e.g., ``cache/``, ``pluginTest/<plugin_name>``, ``tls/``,
  etc.)
- Replay-driven pytest files have a descriptive ``.test.yaml`` extension. The
  file is both the test registration and the Proxy Verifier replay.
- Bespoke AuTest files have a descriptive ``.test.py`` extension. Use these
  only when the direct replay runner cannot express the required client,
  server, or lifecycle.
- ``tests/gold_tests/autest-site`` is a special directory. AuTest, a general
  testing framework, is extended to add domain specific support, |TS| in this
  case, via ``.test.ext`` extension files. The files in here customize the
  command line arguments recognized by the ``autest`` command, the functions
  availabe to the ``Test`` and ``TestRun`` AuTest objects, specific ``Process``
  objects available to test, ``Skip`` conditions for individual tests, etc.

Running End-to-End Tests
========================

Configure the build with ``-DENABLE_AUTEST=ON``. The ``autest`` build target
runs both direct pytest replays and the remaining AuTests, preserving the
existing CI entry point:

.. code-block:: bash

   cmake --build build --target autest

Run only the pytest replay suite with:

.. code-block:: bash

   cmake --build build --target pytest-replay

Pass pytest selection or concurrency options at configure time. For example:

.. code-block:: bash

   cmake -B build -DENABLE_AUTEST=ON -DPYTEST_OPTIONS="-n 4 -k cache-control"
   cmake --build build --target pytest-replay

For a bespoke AuTest, use ``autest.sh`` directly:

.. code-block:: bash

   cmake --build build
   cmake --install build
   cd build/tests
   ./autest.sh --sandbox /tmp/sbcursor --clean=none -f <test_name_without_test_py_extension>

For example, to run ``cache-auth.test.py``:

.. code-block:: bash

   ./autest.sh --sandbox /tmp/sbcursor --clean=none -f cache-auth

To run tests in parallel, pass ``-j N`` where ``N`` is the number of worker
processes. Each worker gets an isolated port range to avoid conflicts:

.. code-block:: bash

   ./autest.sh -j 10 --sandbox /tmp/sbcursor -f cache-auth -f cache-control

Without ``-j``, tests run sequentially.

Recommended Approach: Direct Replay Tests
==========================================

New tests should normally be a single ``.test.yaml`` file. Pytest recognizes
that suffix through the ATS testkit plugin, constructs isolated DNS, Proxy
Verifier server, ATS, and Proxy Verifier client processes, and reports the file
as one pytest item. ATS readiness is based on the fully-initialized log message,
not merely an open port.

The YAML format is parseable without executing Python and keeps the test
topology, ATS configuration, traffic, and validation together. Tests requiring
ad-hoc clients or servers can still use the generic AuTest syntax.

The traffic portion of the ``.test.yaml`` files specifies Proxy Verifier HTTP
traffic behavior and follows the replay and verification syntax described
extensively in its project's README.md file here:
https://github.com/yahoo/proxy-verifier

Test File Structure
-------------------

Name the replay itself ``<scenario>.test.yaml``. No companion ``.test.py``
registration is needed. If a feature has multiple configurations, keep each
configuration in a separate file:

.. code-block:: text

   replay/scenario1.test.yaml
   replay/scenario2.test.yaml
   replay/scenario3.test.yaml

Each file is an independent pytest item and therefore has its own result,
sandbox, ports, and failure artifacts. Keep multiple sessions in one file when
they intentionally share ATS state, such as a cache prime followed by a cache
hit.

Replay File Structure
----------------------

The replay file contains both the test configuration (in the ``autest`` YAML
node) and the traffic replay and verification specification (in the ``sessions``
YAML node). The ``autest`` key retains its name for compatibility with existing
replay files. Here is an example:

.. code-block:: yaml

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

   meta:
     version: "1.0"

   # Configuration section for ATS pytest integration
   autest:
     description: 'Test description for this scenario'

     # Optional (but typical) DNS configuration.
     dns:
       name: 'dns'

     # Required: Server configuration.
     server:
       name: 'server'

     # Required: Client configuration.
     client:
       name: 'client'
       # Optional one-shot client timeout in seconds.
       timeout: 60

     # Required: ATS configuration.
     ats:
       name: 'ts'

       # Optional: ATS process configuration
       process_config:
         enable_cache: true
         enable_tls: false
         enable_quic: false

       # ATS records.config settings
       records_config:
         proxy.config.diags.debug.enabled: 1
         proxy.config.diags.debug.tags: 'http|cache'
         proxy.config.http.cache.http: 1

       # Remap configuration (list format)
       remap_config:
         # Option 1: String format.
         - "map http://test.com/ http://backend.test.com:8080/"

         # Option 2: Dict format with automatic port substitution
         # Note: Using hostnames like backend.example.com requires DNS configuration
         - from: "http://example.com/"
           to: "http://backend.example.com:{SERVER_HTTP_PORT}/"
           # Optional plugins
           plugins:
             - name: "conf_remap.so"
               args:
                 - "proxy.config.http.cache.required_headers=0"

       # Optional: Copy test-specific files/directories to ATS config directory
       copy_to_config_dir:
         - "my-plugin-config.txt"
         - "cert-directory/"

       # Optional: Log (traffic.out or diags.log) validation
       log_validation:
         traffic_out:
           contains:
             - expression: "Expected log message in traffic.out"
               description: "Verify this appears in traffic.out"
           excludes:
             - expression: "Unwanted log message"
               description: "Verify this does NOT appear in traffic.out"
         diags_log:
           contains:
             - expression: "Expected log message in diags.log"
               description: "Verify this appears in diags.log"
           excludes:
             - expression: "Unwanted message in diags.log"
               description: "Verify this does NOT appear in diags.log"

       # Optional: Verify ATS metric values after traffic completes.
       metric_checks:
         - metric: "proxy.process.http.200_responses"
           value: 1

     # Optional runtime capability gates.
     requires:
       ats_features: [TS_HAS_CRIPTS]
       plugins: [example.so]

   # Traffic specification using Proxy Verifier format
   # client-request and server-response generate request and response traffic
   #   toward the ATS proxy.
   # proxy-request and proxy-response verify the content of the request and response
   #   after proxying through ATS.
   sessions:
   - transactions:

     # First transaction: populate cache

     # Send a request to ATS.
     - client-request:
         method: GET
         url: /path
         version: '1.1'
         headers:
           fields:
           - [Host, example.com]
           - [uuid, transaction-1]

       # Verify request headers from ATS.
       proxy-request:
         headers:
           fields:
           - [X-Added-Header, { value: some_field_value, as: equal }]

       # Send a response to ATS.
       server-response:
         status: 200
         reason: OK
         headers:
           fields:
           - [Content-Type, text/plain]
           - [Content-Length, "4"]
           - [Cache-Control, "max-age=300"]

       # Verify response headers from ATS.
       proxy-response:
         status: 200
         headers:
           fields:
           - [Content-Length, { value: 4, as: equal }]

     # Second transaction: verify cache hit with delay
     - client-request:
         # Add delay for cache IO to complete
         delay: 100ms

         method: GET
         url: /path
         version: '1.1'
         headers:
           fields:
           - [Host, example.com]
           - [uuid, transaction-2]

       # Server should not receive this request (cache hit)
       server-response:
         status: 404
         reason: Not Found

       # Expect cached 200 response
       proxy-response:
         status: 200
         headers:
           fields:
           - [Content-Length, { value: 4, as: equal }]

Replay File Components
-----------------------

autest Configuration Section
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``autest`` section configures the test environment:

- **description** or **summary** (one required): A brief description of what
  this test scenario validates. This documents the intention for human readers
  and is included in test reports and failure output.
- **dns** (optional): DNS server configuration with ``name`` and optional
  records. Including DNS allows remap entries to contain hostnames rather than
  localhost IP addresses.
- **server** (required): Proxy Verifier Server configuration with ``name`` and
  optional ``process_config``. This acts as the HTTP origin server that receives
  requests from |TS|, verifies them, and generates configured responses.
- **client** (required): Proxy Verifier Client configuration with ``name`` and
  optional ``process_config`` and ``timeout``. This requests content through
  |TS| and validates the response.
- **requires** (optional): Runtime gates such as ATS build features, installed
  plugins, or minimum OpenSSL and Proxy Verifier versions. An unmet requirement
  skips the item.
- **ats** (required): |TS| configuration including:

  - **name**: ATS process name
  - **process_config**: ATS lifecycle options such as ``enable_cache``,
    ``enable_tls``, ``enable_quic``, and ``enable_cripts``
  - **records_config**: Dictionary of records.config settings
  - **remap_config**: List of remap rules (string or dict format)
  - **cache_config**: List of cache.config rules
  - **copy_to_config_dir**: List of files/directories to copy to ATS config directory
  - **log_validation**: Log validation rules for ``traffic_out`` and ``diags_log``
  - **metric_checks**: List of metric name/value pairs to verify after traffic completes

Log Validation
~~~~~~~~~~~~~~

The ``log_validation`` section allows you to verify the contents of
``traffic.out`` and ``diags.log`` after the test completes.

.. code-block:: yaml

   log_validation:
     traffic_out:
       contains:
         - expression: "cache hit"
           description: "Verify cache hit occurred"
       excludes:
         - expression: "cache miss"
           description: "Should not be a cache miss"
     diags_log:
       contains:
         - expression: "Plugin initialized"
           description: "Verify plugin loaded"

Metric Verification
~~~~~~~~~~~~~~~~~~~~

The ``metric_checks`` section allows you to verify |TS| metric values after all
traffic in the test has completed. Each entry specifies a metric name and its
expected value. After traffic completes, the pytest replay runner uses
``traffic_ctl metric get`` while ATS remains running to verify each metric.

.. code-block:: yaml

   metric_checks:
     - metric: "proxy.process.http.429_responses"
       value: 1
     - metric: "proxy.process.http.200_responses"
       value: 2
       delay: 5

Sessions and Transactions
~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``sessions`` section uses the Proxy Verifier format to specify HTTP traffic
and validation. The below provides a brief overview. For details, see the Proxy
Verifier documentation: https://github.com/yahoo/proxy-verifier

Key points:

- The client uses ``client-request`` to generate an HTTP requests to the |TS|
  proxy. This must contain a ``uuid`` header value to uniquely identify the
  transaction which is later used by the server.
- The server uses ``proxy-request`` to verify the contents of the proxied
  request |TS| sent to it.
- The server uses ``server-response`` to specify the HTTP response to send to
  the client. The server uses the ``uuid`` header value as the key to look up
  which transaction applies to the received request.
- The client uses the ``proxy-response`` to verify the contents of the response
  from the |TS| proxy.
- Use ``delay`` in ``client-request`` to wait between requests (e.g., for cache IO).
- Verficiation in ``proxy-*`` nodes uses ``{ value: X, as: <directive> }``
  syntax to check header values.
- Status codes can be verified with the ``status`` field in ``proxy-response``.
