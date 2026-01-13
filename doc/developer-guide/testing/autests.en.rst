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

Writing Autests
***************

|TS| has two types of developer tests: (1) Catch2 tests for unit tests and (2)
AuTest framework tests for end-to-end testing. The Catch tests reside next the
the associated source code being tested while AuTests are located in
``tests/gold_tests/`` with ``.test.py`` extensions.

* For catch test framework documentation, see: https://github.com/catchorg/Catch2
* For AuTest framework documentation, see: https://autestsuite.bitbucket.io/index.html

This document focuses on AuTest framework tests because they are rather unique
to the |TS| project while Catch2 documentation can be found elsewhere.

File Structure and Naming
==========================

Here is a summary of the file structure and naming conventions for AuTest tests:

- AuTest tests are placed in appropriate subdirectories under
  ``tests/gold_tests/`` (e.g., ``cache/``, ``pluginTest/<plugin_name>``, ``tls/``,
  etc.)
- AuTest test files have a descriptive name with a ``.test.py`` extension (e.g.,
  ``cache-auth.test.py``, ``stats_over_http.test.py``). When the tests are run,
  be aware that their names, sans the ``.test.py`` extension, are used to
  identify the test.
- The ``.test.py`` typically are thin and reference the associated
  ``replay.yaml`` file that describes the test via the ``Test.ATSReplayTest()``
  method.
- ``tests/gold_tests/autest-site`` is a special directory. AuTest, a general
  testing framework, is extended to add domain specific support, |TS| in this
  case, via ``.test.ext`` extension files. The files in here customize the
  command line arguments recognized by the ``autest`` command, the functions
  availabe to the ``Test`` and ``TestRun`` AuTest objects, specific ``Process``
  objects available to test, ``Skip`` conditions for individual tests, etc.

Running Autests
===============

If |TS| cmake build is configured via ``-DENABLE_AUTEST=ON``, tests can be run with:

.. code-block:: bash

   cmake --build build
   cmake --install build
   cd build/tests
   ./autest.sh --sandbox /tmp/sbcursor --clean=none -f <test_name_without_test_py_extension>

For example, to run ``cache-auth.test.py``:

.. code-block:: bash

   ./autest.sh --sandbox /tmp/sbcursor --clean=none -f cache-auth

Recommended Approach: ATSReplayTest
====================================

Currently, many tests are specified largely entirely using the generic AuTest
framework specific syntax via ``.test.py`` files. These use the generic AuTest
framework syntax, which is generically very capable, but not tuned to the
specific |TS| environment and not generally parseable by code editors and AI
tools.

AuTest itself has a solution for this via its extensibility mechanism. A
concerted effort is underway to make more full use of the AuTest extension
mechanism to simplify the test writing process specifically for |TS|.  At a high
level, the extension is called ``ATSReplayTest`` and it is used in a ``test.py``
file to reference an associated ``replay.yaml`` file which fully describes the
test.  The goal is that a large percentage, maybe 90%, of the tests can be
written using this approach while certain requests, perhaps requiring ad-hoc
clients and servers, will be written using the generic AuTest framework syntax.

The traffic portion of the ``replay.yaml`` files specify Proxy Verifier HTTP
traffic behavior and follow the replay and verification syntax described
extensively in its project's README.md file here:
https://github.com/yahoo/proxy-verifier

Simple Test File Structure
---------------------------

Here is an example of a test file using ``ATSReplayTest``:

.. code-block:: python

   '''
   Brief description of what the test validates
   '''
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

   Test.Summary = '''
   Brief description of test purpose
   '''

   Test.ATSReplayTest(replay_file="replay/my-test.replay.yaml")

For tests with multiple |TS| configuration scenarios, you can call
``Test.ATSReplayTest()`` multiple times, each with a different replay file
specifying different configurations of |TS|, dns, servers, clients, etc.

.. code-block:: python

   Test.ATSReplayTest(replay_file="replay/scenario1.replay.yaml")
   Test.ATSReplayTest(replay_file="replay/scenario2.replay.yaml")
   Test.ATSReplayTest(replay_file="replay/scenario3.replay.yaml")

Replay File Structure
----------------------

The replay file contains both the test configuration (in the ``autest`` YAML
node) and the traffic replay and verification specification (in the ``sessions``
YAML node). Here is an example:

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

   # Configuration section for autest integration
   autest:
     description: 'Test description for this scenario'

     # Optional (but typical) DNS configuration.
     dns:
       name: 'dns'
       # Other MakeDNServer parameters can be set, see microDNS.test.ext

     # Required: Server configuration.
     server:
       name: 'server'
       # Other AddVerifierServerProcess parameters can be set, see verifier_server.test.ext

     # Required: Client configuration.
     client:
       name: 'client'
       # Other AddVerifierClientProcess parameters can be set, see verifier_client.test.ext

     # Required: ATS configuration.
     ats:
       name: 'ts'

       # Optional: Enable cache (default is determined by process_config)
       # enable_cache: true

       # Other parameters can be set, see trafficserver.test.ext

       # Optional: ATS process configuration
       process_config:
         enable_cache: true
         # Other MakeATSProcess parameters can be set, see trafficserver.test.ext

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

       # Verify respone headers from ATS.
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

- **description** (required): A brief description of what this test scenario
  validates. This is helpful to document the intetion of the test for the human
  reading the file and is also helpful when tests fail as this description is included
  in failure output.
- **dns** (optional): DNS server configuration with ``name`` and optional
  ``process_config``. See the ``microDNS.test.ext`` file for more details. Including
  the DNS allows remap entries to contain hostnames rather than localhost IP
  addresses.
- **server** (required): Proxy Verifier Server configuration with ``name`` and
  optional ``process_config``. See the ``verifier_server.test.ext`` file for
  more details. This acts as the HTTP origin server that receives requests from
  |TS| as they are proxied from the client to the server. It also provides any
  request verification and generates the configured HTTP response.
- **client** (required): Proxy Verifier Client configuration with ``name`` and
  optional ``process_config``. See the ``verifier_client.test.ext`` file for
  more details. This acts as the HTTP client that requests content from the
  server via the |TS| proxy and validates the response.
- **ats** (required): |TS| configuration including:

  - **name**: ATS process name
  - **enable_tls**: Set to ``true`` for HTTPS testing
  - **process_config**: Parameters passed to ``MakeATSProcess`` (e.g., ``enable_cache``)
  - **records_config**: Dictionary of records.config settings
  - **remap_config**: List of remap rules (string or dict format)
  - **copy_to_config_dir**: List of files/directories to copy to ATS config directory
  - **log_validation**: Log validation rules for ``traffic_out`` and ``diags_log``

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
