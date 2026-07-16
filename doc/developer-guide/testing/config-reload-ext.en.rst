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

.. _autest-config-reload:

Config Reload Test Extension
****************************

The ``config_reload.test.ext`` extension provides ``Test.AddConfigReload()`` to
replace the legacy pattern of fire-and-forget ``traffic_ctl config reload``
followed by log grepping with a deterministic, structured approach.

The extension is loaded automatically from
``tests/gold_tests/autest-site/config_reload.test.ext``.


Why Use This Extension
======================

The legacy reload test pattern is fragile:

.. code-block:: python

   # OLD: fire-and-forget + sleep + log grep
   tr = Test.AddTestRun("Reload config")
   p = tr.Processes.Process("reload-1")
   p.Command = 'traffic_ctl config reload; sleep 30'
   p.Env = ts.Env
   p.ReturnCode = Any(0, -2)
   p.Ready = When.FileContains(
       ts.Disk.diags_log.Name, "finished loading", 2)
   p.Timeout = 20
   tr.Processes.Default.StartBefore(p)
   tr.Processes.Default.Command = 'echo "waiting for reload"'
   tr.TimeOut = 25

Problems with this approach:

- Relies on exact log text that can change across versions.
- Uses ``sleep`` for synchronization, leading to slow and flaky tests.
- Does not validate *which* config handler ran.
- Does not detect reload failures.

The new pattern is a single call:

.. code-block:: python

   # NEW: deterministic, validates specific handlers
   tr = Test.AddConfigReload(ts, expect_tasks=["sni.yaml"],
                             description="Reload after sni.yaml touch")


How It Works
============

``AddConfigReload`` uses ``traffic_ctl config reload -m`` (monitor mode) to
trigger a reload and **block until it completes**. Monitor mode polls the server
for the reload status, so there is no sleeping or guessing. The default timeout
is **30 seconds** (configurable via the ``timeout`` parameter).

When ``expect_tasks`` or ``expect_absent_tasks`` is set, a second test run
queries the ``get_reload_config_status`` JSONRPC endpoint and validates the task
tree via ``CustomJSONRPCResponse``. This gives tests access to the full
structured result — including per-task status, subtasks, and descriptions —
without relying on the human-readable output of ``traffic_ctl``.

When neither ``expect_tasks`` nor ``expect_absent_tasks`` is set, only the exit
code is validated (no JSONRPC query). This is useful for reloads where you only
care that the reload succeeded (exit code 0).


Test.AddConfigReload
====================

Triggers a config reload, blocks until completion, and validates the result.

.. code-block:: python

   tr = Test.AddConfigReload(
       ts,                          # ATS process object
       expect="success",            # "success", "fail", "timeout", or "any"
       token=None,                  # custom token (auto-generated if None)
       data=None,                   # inline YAML or @file path
       force=False,                 # --force flag
       timeout="30s",               # monitor timeout
       initial_wait=1.0,            # seconds before first poll
       refresh_int=0.5,             # seconds between polls
       expect_tasks=None,           # list or dict of expected handler names
       expect_absent_tasks=None,    # list of handler names that must NOT appear
       description=None,            # test run description (recommended)
   )

Parameters
----------

``ts``
   The ATS process object (from ``Test.MakeATSProcess()``).

``expect``
   Expected outcome:

   - ``"success"`` — exit code 0 (all handlers succeeded)
   - ``"fail"`` — exit code 2 (one or more handlers failed)
   - ``"timeout"`` — exit code 75 (monitor timed out)
   - ``"any"`` — exit code 0 or 2 (don't care about outcome)

   Default: ``"success"``.

``token``
   A custom reload token string. If ``None``, an auto-generated token
   (``autest-reload-1``, ``autest-reload-2``, ...) is used. Tokens are unique
   per test file.

``data``
   Inline YAML content or a ``@file`` path to pass via ``--data``. When the
   value starts with ``@``, it is passed as-is (e.g. ``@/path/to/file.yaml``).
   Otherwise the string is shell-quoted and passed inline.

   .. note::

      The ``--data`` flag is accepted by ``traffic_ctl config reload`` but
      individual reload handlers do not yet consume inline data. This parameter
      is reserved for future use.

``force``
   If ``True``, adds the ``--force`` flag to start a new reload even when one
   is already in progress. See the ``traffic_ctl config reload`` documentation
   for details on force behavior.

``timeout``
   Duration string for the monitor timeout (e.g. ``"30s"``, ``"1m"``). This
   controls how long ``traffic_ctl config reload -m`` will poll before giving
   up. Default: ``"30s"``. Set to ``None`` to disable the timeout (not
   recommended).

``initial_wait``
   Seconds to wait before the first poll, giving the server time to schedule
   handlers. Default: ``1.0``.

``refresh_int``
   Seconds between status polls. Default: ``0.5``.

``expect_tasks``
   Expected handler/config names in the reload. Accepts two forms:

   - **List** — checks that each name appears somewhere in the task tree:

     .. code-block:: python

        expect_tasks=["ip_allow.yaml", "sni.yaml"]

   - **Dict** — checks presence *and* per-task status:

     .. code-block:: python

        expect_tasks={"sni.yaml": "fail", "SSLConfig": "success"}

   When not set (``None``), no JSONRPC validation is performed — only the exit
   code is checked.

``expect_absent_tasks``
   A list of handler/config names that must **not** appear in the reload task
   tree. Useful for verifying that touching an unrelated file did not trigger
   a specific handler.

``description``
   Description for the ``TestRun``. **Recommended** — always pass a description
   for readable test output. When omitted, an auto-generated description is
   used (e.g. ``"Reload config [autest-reload-1]"``).

Return Value
------------

Returns the reload ``TestRun`` object (the first test run). Callers can add
extra assertions or ``StillRunningAfter`` references:

.. code-block:: python

   tr = Test.AddConfigReload(ts, expect_tasks=["remap.config"],
                             description="Reload after remap.config edit")
   tr.StillRunningAfter = ts
   tr.StillRunningAfter = origin_server


.. note::

   Standalone record-triggered reloads (via ``traffic_ctl config set`` without
   an explicit ``config reload``) do not create tasks in the reload framework
   and cannot be verified with this extension.


Examples
========

Basic reload after touching a config file:

.. code-block:: python

   tr = Test.AddTestRun("Touch ip_allow.yaml")
   tr.Processes.Default.Command = f"touch {config_dir}/ip_allow.yaml"
   tr.Processes.Default.ReturnCode = 0
   tr.StillRunningAfter = ts

   tr = Test.AddConfigReload(ts, expect_tasks=["ip_allow.yaml"],
                             description="Reload after ip_allow.yaml touch")

Expecting a reload failure (e.g. broken sni.yaml):

.. code-block:: python

   tr = Test.AddConfigReload(ts, expect="fail", expect_tasks=["sni.yaml"],
                             description="Reload with broken sni.yaml")

Verifying a handler was NOT triggered:

.. code-block:: python

   tr = Test.AddConfigReload(ts, expect_absent_tasks=["ip_allow.yaml"],
                             description="Reload (should NOT trigger ip_allow)")

Per-task status validation:

.. code-block:: python

   tr = Test.AddConfigReload(
       ts,
       expect="fail",
       expect_tasks={"sni.yaml": "fail", "SSLConfig": "success"},
       description="Reload with mixed task outcomes",
   )

Reload with inline YAML data:

.. code-block:: python

   # NOTE: --data is accepted by traffic_ctl but individual reload handlers
   # do not yet consume inline data. Reserved for future use.
   tr = Test.AddConfigReload(
       ts,
       data="ip_allow:\n  - apply: in\n    ip_addrs: 0/0\n    action: allow",
       expect_tasks=["ip_allow.yaml"],
       description="Reload with inline ip_allow data",
   )
