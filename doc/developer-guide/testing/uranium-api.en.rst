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

.. _uranium-api:

Uranium Python API
******************

Request fixtures by name in the arguments of a ``test_*`` function. Do not
import and call fixture functions yourself. Import returned types and assertion
helpers from ``tools.uranium.services``.

.. literalinclude:: ../../../tests/uranium_tests/basic/test_basic.py
   :language: python
   :pyobject: test_traffic_server_starts

Prefer direct replay tests when Proxy Verifier can express the scenario.
Native tests use ordinary pytest functions for custom clients or runtime changes.
Share setup through helper functions or local ``@pytest.fixture`` functions;
a scenario class and a separate ``run()`` entry point are not required.

Fixtures
========

All fixtures are function-scoped except ``uranium_test_runtime``, which is
session-scoped. Configuration is staged until ``start()``: requesting a
fixture does not start ATS or a support process.

.. list-table::
   :header-rows: 1
   :widths: 20 25 55

   * - Fixture
     - Returned value
     - Responsibility
   * - ``ats``
     - :class:`~tools.uranium.services.ATS`
     - One unstarted ATS instance named ats.
   * - ``ats_factory``
     - :class:`~tools.uranium.services.ATSFactory`
     - Create multiple named ATS instances in one test.
   * - ``services``
     - :class:`~tools.uranium.services.ServiceFactory`
     - Create origins, DNS, verifier servers, and custom processes.
   * - ``curl``
     - :class:`~tools.uranium.services.Curl`
     - Run curl respecting the selected TCP/UDS transport.
   * - ``procedural_context``
     - :class:`~tools.uranium.services.ProceduralContext`
     - Own the test's paths, runtime, and sandbox.
   * - ``uranium_replay``
     - ``Callable[[Path], None]``
     - Execute a replay manifest and all variants inside a native test.
   * - ``uranium_test_runtime``
     - :class:`~tools.uranium.runtime.TestRuntime`
     - Share installed-tool paths, capabilities, and port allocation.

``ats`` depends on ``ats_factory``. Both factories and ``curl`` share the
test's ``procedural_context``, so they can safely be requested together.
Names passed to each factory must be unique within a test. Pass options such
as ``enable_tls=True`` or ``enable_cache=True`` to ``ats_factory.create()``.

Common ATS creation options are:

.. list-table::
   :header-rows: 1
   :widths: 25 15 60

   * - Option
     - Default
     - Effect
   * - ``enable_cache``
     - ``True``
     - Enable the isolated HTTP cache.
   * - ``enable_tls``
     - ``False``
     - Add a TLS listener and default test certificate.
   * - ``enable_quic``
     - ``False``
     - Add an HTTP/3 listener; normally used with TLS enabled.
   * - ``enable_proxy_protocol``
     - ``False``
     - Add dedicated PROXY protocol listeners.
   * - ``disable_log_rolling``
     - ``True``
     - Keep logs at predictable paths. Set false to test installed defaults;
       an explicit ``records.update()`` value takes precedence.
   * - ``server_args``
     - ``[]``
     - Additional argument strings passed to ``traffic_server``.

Configure records with ``ats.records.update({"proxy.config.http.cache.http": 0})``
and line-oriented files with ``ats.remap_config.add_line(...)``. Use the
``set_*_yaml()`` methods for structured configuration and ``write_config_file()``
for other files. Modify live files explicitly before requesting a reload;
changing a staged object alone does not reload a running server.

Start origins and DNS before ATS when the scenario requires them. Factories
stop their processes during teardown, even after an assertion fails. Unexpected
ATS exits, fatal diagnostics, and missing diagnostic output fail teardown.
Use ``ATS.wait()`` for intentional one-shot exits and
``ATS.expect_start_failure()`` for an expected startup failure.

A service's ``run()`` waits for a one-shot command; ``start()`` launches a
long-lived process. Check command results explicitly, or register stream
expectations before running the service. Negative log assertions should inspect
complete output after stopping the process, not an early positive-marker snapshot.

``uranium_replay(path)`` accepts a ``pathlib.Path`` to a manifest. Each call
and variant uses a distinct child of the native test's sandbox, preserving
sibling services and earlier results. Replay-only scenarios should normally
use direct ``*.test.yaml`` collection.

Paths, cleanup, and parallel execution
--------------------------------------

Sandboxes use ``<sandbox-root>/<test-name>/``. A rerun clears the previous
directory for that test. Failures retain artifacts; passing tests retain them
only with ``--keep-sandboxes``. A session lock prevents concurrent invocations
from sharing a root; give simultaneous runs different ``--sandbox`` paths.

Use ``services.allocate_port()`` for custom listeners instead of fixed ports.
Ownership and allocation work across pytest-xdist workers. Tests that cannot
overlap others are listed in ``tests/serial_tests.txt``.

Copy helpers resolve relative paths against the test's source directory.
Use ``procedural_context.run_directory`` for generated inputs. ATS exposes
``config_directory``, ``log_directory``, and ``storage_directory``.
Unix-socket paths can live outside the sandbox to satisfy OS path limits.

Clients and expectations
------------------------

``curl.get(ats, ...)`` and ``curl.run_for(ats, arguments, ...)`` honor
``--curl-uds``. ``run_for`` accepts one shell-style argument string, parsed
without executing a shell. Use ``curl.run`` when choosing the endpoint and
transport yourself. ``ATS.run`` takes an argument vector; ``ATS.run_shell``
explicitly executes a shell script. Timeout values are in seconds.

``CommandResult`` exposes ``returncode``, ``stdout``, ``stderr``, and combined
``output``. Managed service streams support ``contains()``, ``excludes()``,
and ``matches_gold()``. Register these before running the service; do not use
``+=`` or replace the stream expectation object.

API reference
=============

These signatures and docstrings are generated from the implementation by
Sphinx autodoc. No ATS installation or running services are needed to build
the reference.

Traffic Server and configuration
--------------------------------

.. autoclass:: tools.uranium.services.ATSFactory
   :members:

.. autoclass:: tools.uranium.services.ATS
   :members:

.. autoclass:: tools.uranium.services.RecordsConfig
   :members:

.. autoclass:: tools.uranium.services.ConfigFile
   :members:

Clients and support services
----------------------------

Origin, DNS, HTTP-bin, and verifier services share the lifecycle and output
expectation methods documented on ``ProcessService`` below. ``services.origin``
accepts microserver options such as ``ssl``, ``both``, ``lookup_key``, and
``delay`` (seconds). ``services.dns`` accepts ``port`` and a default response.
Use ``services.process`` for a custom executable, with ``ready_port`` when
TCP listener readiness is sufficient; otherwise wait for a protocol response
or a specific output marker before sending test traffic.

.. autoclass:: tools.uranium.services.Curl
   :members:

.. autoclass:: tools.uranium.services.ServiceFactory
   :members:

.. autoclass:: tools.uranium.services.ProcessService
   :members:

.. autoclass:: tools.uranium.services.OriginServer
   :members:

.. autoclass:: tools.uranium.services.DNSServer
   :members:

.. autoclass:: tools.uranium.services.HttpBinServer
   :members:

.. autoclass:: tools.uranium.services.VerifierServer
   :members:

Results and context
-------------------

.. autoclass:: tools.uranium.services.CommandResult
   :members:

.. autoclass:: tools.uranium.services.ProceduralContext
   :members:

.. autoclass:: tools.uranium.runtime.TestRuntime
   :members:

.. autoclass:: tools.uranium.expectations.StreamExpectations
   :members:

.. autoclass:: tools.uranium.expectations.StreamExpectation
   :members:

.. autoclass:: tools.uranium.process.ManagedProcess
   :members:

Assertion helpers
-----------------

.. autofunction:: tools.uranium.services.assert_matches_gold

.. autofunction:: tools.uranium.services.wait_for_file_lines

.. autofunction:: tools.uranium.services.wait_for_metric

.. autofunction:: tools.uranium.services.send_tcp
