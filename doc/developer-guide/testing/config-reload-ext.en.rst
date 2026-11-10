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

.. _uranium-config-reload:

Testing Configuration Reloads
*****************************

Configuration reload tests are native Uranium pytest scenarios (see
:ref:`uranium-tests`). Uranium has no dedicated reload directive; a test drives
the reload through the ``ATS`` service and validates the structured result the
server reports for the reload token.

Avoid the fire-and-forget pattern of running ``traffic_ctl config reload``,
sleeping, and grepping ``diags.log`` for a message. It relies on log text that
can change, adds fixed delays, cannot tell which handler ran, and does not
detect a failed reload.


Triggering a Reload
===================

``ATS.traffic_ctl()`` runs :program:`traffic_ctl` against the test's run root
and returns a ``CommandResult``. Pass a token so the reload can be queried
afterward, and use monitor mode (``-m``) so the command blocks until the
reload reaches a terminal state:

.. code-block:: python

   from tools.uranium.services import ATS


   def test_reload_ip_allow(ats: ATS) -> None:
       """Reload ip_allow.yaml after it changes.

       :param ats: Isolated Traffic Server instance.
       """
       ats.start()
       (ats.config_directory / "ip_allow.yaml").touch()
       result = ats.traffic_ctl("config", "reload", "-m", "-t", "ip-allow-touch", "-T", "30s")
       assert result.returncode == 0, result.output

With ``-m``, the exit code reflects the reload outcome: ``0`` for success,
``2`` when the reload failed, and ``75`` when the ``-T`` timeout expired. If
another reload is already in progress, ``-m`` attaches to it and reports its
outcome. See :option:`traffic_ctl config reload` for
the full list and for the ``-w`` (initial wait), ``-r`` (refresh interval),
``-T`` (timeout), ``--force``, ``--data`` and ``--directive`` options.

``ATS.rpc()`` sends one JSON-RPC request through :program:`traffic_ctl`. Use it
with ``admin_config_reload`` when a test needs to control the request payload
directly, for example to submit inline configuration or to check the
``errors`` array of the response.


Validating the Result
=====================

Check which handlers ran, and how each one finished, through the
:ref:`get_reload_config_status` JSON-RPC method rather than the
human-readable output of :program:`traffic_ctl`:

.. code-block:: python

   import json

   request = {
       "jsonrpc": "2.0",
       "id": "1",
       "method": "get_reload_config_status",
       "params": {"token": "ip-allow-touch"},
   }
   response = json.loads(ats.rpc(request).stdout)
   root = response["result"]["tasks"][0]
   assert root["status"] == "success", response

The root task's ``sub_tasks`` hold one entry per handler, each with its own
``description``, ``filename`` and ``status``. Walk that tree to require that a
handler ran with an expected status, or that a handler which should not have
been triggered is absent.

When a reload is started without ``-m``, poll
:ref:`get_reload_config_status` until the root task reaches ``success``,
``fail`` or ``timeout`` before asserting on it. Waiting only until a child task
succeeds can return before the reload as a whole has finished.

Record-triggered reloads (``traffic_ctl config set`` without an explicit
``config reload``) do not create reload tasks and cannot be validated this way.


Shared Helpers
==============

``tests/uranium_tests/jsonrpc/config_reload_helpers.py`` collects the
operations the JSON-RPC reload scenarios share, including ``rpc()`` to invoke
and decode a JSON-RPC method, ``require_command()`` to require a successful
command and expected output fragments, and ``wait_for_status()`` to poll
``traffic_ctl config status`` for a token. Import from it, or move a helper
into ``tests/tools/uranium`` when tests outside ``jsonrpc`` need it.

The following tests are useful starting points:

- ``tests/uranium_tests/jsonrpc/test_config_reload_tracking.py`` — token
  generation, status queries, history, force reload, duplicate token rejection.
- ``tests/uranium_tests/jsonrpc/test_config_reload_rpc.py`` — inline reload,
  multiple configs, ``FileOnly`` rejection, large payloads.
- ``tests/uranium_tests/jsonrpc/test_config_reload_failures.py`` — error
  handling, broken configs, handler failure reporting.
- ``tests/uranium_tests/traffic_ctl/test_traffic_ctl_config_reload.py`` —
  ``traffic_ctl config reload`` and ``config status`` behavior.
- ``tests/uranium_tests/tls/test_ssl_multicert_partial_reload.py`` — waiting
  for the root task of a TLS reload before checking the new certificates.
