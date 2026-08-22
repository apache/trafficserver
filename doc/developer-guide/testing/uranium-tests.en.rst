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

.. _uranium-tests:

Writing Uranium Tests
*********************

|TS| uses Catch2 for unit tests and pytest for Uranium tests. Catch2 tests
reside next to the associated source code. Uranium tests are under
``tests/uranium_tests/``. Direct Proxy Verifier replays and native Python
scenarios are both collected and executed by pytest.

* For catch test framework documentation, see: https://github.com/catchorg/Catch2
* For pytest documentation, see: https://docs.pytest.org/
* For Proxy Verifier documentation, see: https://github.com/yahoo/proxy-verifier

This document focuses on Uranium tests because Catch2 documentation is
available elsewhere.

Why "Uranium"?
===============

Uranium is enormously useful: as nuclear fuel, it powers homes with reliable,
low-carbon electricity. It also demands careful handling. The name is
deliberately tongue-in-cheek because these system tests have a similar dual
nature: they provide indispensable confidence in a complete |TS| deployment,
but can be so unstable as to be toxic when process lifecycles, timing, network
state, and artifacts are not carefully contained. The name is a reminder to
keep every test isolated, deterministic, and safe to run.

File Structure and Naming
=========================

The Uranium test runners use distinct filename conventions:

- Tests are placed in appropriate subdirectories under
  ``tests/uranium_tests/`` (e.g., ``cache/``, ``pluginTest/<plugin_name>``, ``tls/``,
  etc.)
- Replay-driven pytest files have a descriptive ``.test.yaml`` extension. The
  file is both the test registration and the Proxy Verifier replay.
- Native scenarios use pytest's ``test_*.py`` convention. Prefer a scenario
  class whose methods configure its server, ATS process, and client and whose
  ``run()`` method names the ordered steps. Use this form only when a direct
  replay cannot express the required client, server, or runtime mutation.

Running Uranium Tests
=====================

Configure the build with ``-DENABLE_URTEST=ON``. The ``urtest`` build target
runs the complete pytest-owned inventory:

.. code-block:: bash

   cmake --build build --target urtest

Run only the replay suite with:

.. code-block:: bash

   cmake --build build --target urtest-replay

Pass pytest selection or concurrency options at configure time. For example:

.. code-block:: bash

   cmake -B build -DENABLE_URTEST=ON -DURTEST_OPTIONS="-n 4 -k cache_control"
   cmake --build build --target urtest-replay

Use ``urtest.sh`` directly for the shortest single-test workflow:

.. code-block:: bash

   cmake --build build
   cmake --install build
   cd build/tests
   ./urtest.sh -q -k <pytest_expression>

For example, to run items whose names contain ``header_rewrite``:

.. code-block:: bash

   ./urtest.sh -q -k header_rewrite

The ``-k`` expression works for both ``.test.yaml`` and ``test_*.py`` items.
It is pytest's ordinary substring and boolean-expression selector.

To run tests in parallel, pass pytest-xdist's ``-n N`` option. Each worker gets
an isolated sandbox and dynamically allocated listeners:

.. code-block:: bash

   ./urtest.sh -n 10 --sandbox /tmp/sbcursor -k "header_rewrite or cache_control"

Without ``-n``, tests run sequentially. Tests listed in
``tests/serial_tests.txt`` acquire an exclusive lock and never overlap other
Uranium test items.

Manual Tests
============

Mark privileged, unusually slow, or intentionally diagnostic native tests with
``@pytest.mark.manual`` when they should remain available but must not execute
as part of the normal suite. For a direct replay test, set ``urtest.manual`` to
``true`` or a string explaining why it is manual. Pytest reports these tests as
skipped by default. Pass ``--run-manual`` and select the intended item to run
one explicitly:

.. code-block:: bash

   ./urtest.sh --run-manual -k ats_probe

The test should retain its complete implementation and perform ordinary
runtime capability checks after it is enabled. For example, a privileged test
can still call ``pytest.skip`` when bpftrace or the required Linux capability
is unavailable.

Container Execution
===================

The source-tree ``tests/urtest.sh`` command defaults to the official
``ci.trafficserver.apache.org/ats/fedora:44`` image. It configures an
incremental build under ``build-urtest-container`` and then runs pytest. Pass
``--run-in-docker`` to force this behavior or ``--no-run-in-docker`` to use the
current environment. A short ``/tmp`` sandbox avoids Unix socket path limits;
afterward, regular files are copied to ``build-urtest-container/sandbox`` on
the host.

The default changes to direct execution only when both conditions are true:

- A Docker, Podman, containerd, or Kubernetes marker indicates that the
  process is containerized.
- ``/etc/os-release`` identifies Fedora 44.

Thus Jenkins, which already launches the Fedora 44 image, does not attempt
nested Docker. The container launch matches CI with an init process, host
networking, and the ``SYS_PTRACE`` capability. It does not require
``--privileged`` and does not mount the host Docker socket.

Recommended Approach: Direct Replay Tests
==========================================

New tests should normally be a single ``.test.yaml`` file. Pytest recognizes
that suffix through the Uranium testkit plugin, constructs isolated DNS, Proxy
Verifier server, ATS, and Proxy Verifier client processes, and reports the file
as one pytest item. ATS readiness is based on the fully-initialized log message,
not merely an open port.

The YAML format is parseable without executing Python and keeps the test
topology, ATS configuration, traffic, and validation together. Use a native
pytest scenario when the test requires an ad-hoc client or server, multiple ATS
instances, or ordered runtime mutations that the replay lifecycle cannot
express.

The traffic portion of each ``.test.yaml`` file specifies Proxy Verifier HTTP
traffic behavior and follows the replay and verification syntax described
extensively in its project's README.md file here:
https://github.com/yahoo/proxy-verifier

Test File Structure
-------------------

Name the replay itself ``<scenario>.test.yaml``. No companion ``.test.py``
registration is needed. If a feature has multiple configurations, use named
``urtest.variants`` when they share traffic and topology, or separate files
when that is clearer:

.. code-block:: text

   replay/scenario1.test.yaml
   replay/scenario2.test.yaml
   replay/scenario3.test.yaml

Each file is an independent pytest item and therefore has its own result,
sandbox, ports, and failure artifacts. Keep multiple sessions in one file when
they intentionally share ATS state, such as a cache prime followed by a cache
hit.

Native Pytest Scenarios
-----------------------

Use a native ``test_*.py`` module when the test needs behavior outside the
direct replay lifecycle, such as a custom curl or OpenSSL invocation, a raw
socket client, several ATS instances, or a configuration reload between
requests. The fixtures in ``tests/tools/uranium`` own process cleanup, allocate
ports, and isolate each item's files.

Organize a procedural test around a scenario class. Give configuration and
test phases descriptive method names, and make ``run()`` the obvious entry
point:

.. code-block:: python

   from tools.uranium.services import ATS, ATSFactory, Curl, OriginServer, ServiceFactory


   class ExampleScenario:
       def __init__(self, ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
           self._origin = self.configure_origin(services)
           self._ats = self.configure_ats(ats_factory)
           self._curl = curl

       @staticmethod
       def configure_origin(services: ServiceFactory) -> OriginServer:
           origin = services.origin("origin")
           origin.add_response(
               {"headers": "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n"},
               {"headers": "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n"},
           )
           return origin

       def configure_ats(self, ats_factory: ATSFactory) -> ATS:
           ats = ats_factory.create("ats", enable_cache=False)
           ats.remap_config.add_line(f"map / http://127.0.0.1:{self._origin.port}")
           return ats

       def run(self) -> None:
           self._origin.start()
           self._ats.start()
           result = self._curl.get(self._ats, headers={"Host": "example.com"})
           assert result.returncode == 0, result.output


   def test_example(ats_factory: ATSFactory, services: ServiceFactory, curl: Curl) -> None:
       ExampleScenario(ats_factory, services, curl).run()

Use ``ServiceFactory.verifier_server()`` and
``ServiceFactory.verifier_client()`` when a procedural scenario still benefits
from Proxy Verifier traffic. Launch Python helpers with ``sys.executable`` so
they use the same environment as pytest. Avoid fixed ports and global temporary
paths; allocated listeners and the fixture run directory keep ``-n`` runs
independent.

Declare expectations for a managed service's standard streams with explicit
methods. Regular-expression expectations require an explanation, which is
included in failure output. Gold paths are relative to the test module:

.. code-block:: python

   server.stdout.contains(
       "Ready with 3 transactions",
       "The server should parse all three transactions.",
   )
   server.stderr.excludes(
       "Violation:",
       "The server should not report verification errors.",
   )
   client.stdout.matches_gold("gold/client.gold")
   client.expect_return_codes(0, 1)

``stdout`` and ``stderr`` are read-only expectation objects; do not assign to
them or use ``+=``. Use ``reset()`` to discard declarations intentionally.
When a scenario must inspect output while a process is running, read
``stdout_text``, ``stderr_text``, or their combined ``output`` property.

``tools.uranium.services`` is the stable scenario-facing import facade. The
service implementations are split by responsibility under
``tests/tools/uranium/services`` so harness internals can depend directly on
ATS, curl, origin, DNS, HTTPBin, Proxy Verifier, or generic process support
without reintroducing a monolithic module.

``Curl.run()`` and ``Curl.run_for()`` take one shell-style argument string, and
``Curl.get()`` accepts the same syntax in its ``options`` parameter. Quote
values containing whitespace just as in a shell command. Uranium parses the
string with ``shlex.split`` and executes curl directly; it does not invoke a
shell. For example:

.. code-block:: python

   result = self._curl.run_for(
       self._ats,
       f"--verbose --header 'Host: example.com' http://127.0.0.1:{self._ats.http_port}/",
       timeout=10,
   )

Document every function parameter with a ``:param name:`` field. State units
for values such as timeouts rather than relying on the default value to imply
them.

Replay File Structure
----------------------

The replay file contains both the Uranium test configuration (in the ``urtest``
YAML node) and the traffic replay and verification specification (in the
``sessions`` YAML node). Here is an example:

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

   # Configuration for the Uranium test runner
   urtest:
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

       # Optional environment variables for the ATS process.
       environment:
         ATS_TEST_HOOK: "1"

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

The ``urtest`` Configuration Section
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The ``urtest`` section configures the test environment:

- **description** or **summary** (one required): A brief description of what
  this test scenario validates. This documents the intention for human readers
  and is included in test reports and failure output.
- **replay** (optional): Path, relative to a collected manifest, to an existing
  Proxy Verifier traffic file. Omit this when the collected file contains the
  traffic sessions itself.
- **variants** (optional): Named metadata overlays collected as independent
  pytest items. Nested ATS, client, and server mappings merge with the manifest
  defaults.
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
  - **hosting_config**: List of hosting.config rules
  - **environment**: Dictionary of environment variables for the ATS process
  - **copy_to_config_dir**: List of files/directories to copy to ATS config directory
  - **log_validation**: Log validation rules for ``traffic_out`` and ``diags_log``
  - **metric_checks**: List of metric name/value pairs to verify after traffic completes
  - **file_checks**: Paths whose presence, absence, or text content is verified after traffic completes

String values in ATS metadata may use ``{SERVER_HTTP_PORT}``,
``{SERVER_HTTPS_PORT}``, ``{ATS_HTTP_PORT}``, ``{ATS_HTTPS_PORT}``,
``{ATS_ROOT}``, ``{CONFIG_DIR}``, ``{LOG_DIR}``, ``{RUNTIME_DIR}``, and
``{STORAGE_DIR}`` placeholders. Uranium replaces them after allocating the
test sandbox and listeners.

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
