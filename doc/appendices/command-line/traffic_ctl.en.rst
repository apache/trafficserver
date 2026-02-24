.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. |RPC| replace:: JSONRPC 2.0

.. _JSONRPC: https://www.jsonrpc.org/specification
.. _JSON: https://www.json.org/json-en.html

.. _traffic_ctl_jsonrpc:

traffic_ctl
***********

Synopsis
========

:program:`traffic_ctl` [OPTIONS] SUBCOMMAND [OPTIONS]


.. note::

   :program:`traffic_ctl` uses a `JSONRPC`_ protocol to communicate with :program:`traffic_server`.

Description
===========

:program:`traffic_ctl` is used to display, manipulate and configure
a running Traffic Server. :program:`traffic_ctl` includes a number
of subcommands that control different aspects of Traffic Server:


:program:`traffic_ctl config`
   Manipulate and display configuration records
:program:`traffic_ctl metric`
   Manipulate performance and status metrics
:program:`traffic_ctl server`
   Examine the server
:program:`traffic_ctl storage`
   Manipulate cache storage
:program:`traffic_ctl plugin`
   Interact with plugins.
:program:`traffic_ctl host`
   Manipulate host status.
:program:`traffic_ctl hostdb`
   Manipulate HostDB status.
:program:`traffic_ctl rpc`
   Interact directly with the |RPC| server in |TS|




Options
=======

.. program:: traffic_ctl
.. option:: --debug

   Enable debugging output.

.. option:: -V, --version

   Print version information and exit.

.. option:: -f, --format

   Specify the output print style.

   =================== ========================================================================
   Options             Description
   =================== ========================================================================
   ``json``            It will show the response message formatted to `JSON`_. This is ideal if you want to redirect the stdout to a different source.
                       It will only stream the json response, no other messages.
                       This option only applies to the RPC request or response.
   ``rpc``             Show the JSONRPC request and response + the default output.
                       This option only applies to the RPC request or response.
   =================== ========================================================================

   In case of a record request(config) ``--records`` overrides this flag.

   Example:

   .. code-block::

      $ traffic_ctl config get variable --format rpc
      --> {request}
      <-- {response}
      variable 1234

   .. code-block::

      $ traffic_ctl config get variable --format json
      {response}

   There will be no print out beside the json response. This is ideal to redirect to a file.


.. option:: --records

   Option available only for records request.

.. option:: --run-root

   Path to the runroot file.

Subcommands
===========

.. _traffic-control-command-alarm:

traffic_ctl alarm
-----------------

.. warning::

   Option not available in the |RPC| version.

.. _traffic-control-command-config:

traffic_ctl config
------------------

   Manipulate configuration records.

   .. program:: traffic_ctl config
   .. option:: --records

   Display the config output in YAML format. This out can be used directly into ATS if needed.


   .. program:: traffic_ctl config
   .. option:: --default

   Include the default value alonside with the current value. This can be used in combination with ``--records``

   .. code-block:: bash

      $ traffic_ctl config match proxy.config.diags.debug --records --default
      records:
         diags:
            debug:
               client_ip: "null"  # default: null
               enabled: 1  # default: 0
               tags: quic  # default: http|dns
               throttling_interval_msec: 0  # default: 0

.. program:: traffic_ctl config
.. option:: defaults [--records]

   :ref:`admin_lookup_records`

   Display the default values for all configuration records. The ``--records`` flag has the same
   behavior as :option:`traffic_ctl config get --records`.

.. program:: traffic_ctl config
.. option:: describe RECORD [RECORD...]

   :ref:`admin_lookup_records`

   Display all the known information about a configuration record. This includes the current and
   default values, the data type, the record class and syntax checking expression.

.. program:: traffic_ctl config
.. option:: diff [--records]

   :ref:`admin_lookup_records`

   Display configuration records that have non-default values. The ``--records`` flag has the same
   behavior as :option:`traffic_ctl config get --records`.

.. program:: traffic_ctl config
.. option:: get [--records, --default] RECORD [RECORD...]

   :ref:`admin_lookup_records`

Display the current value of a configuration record.

.. program:: traffic_ctl config get
.. option:: --records

   If this flag is provided, :option:`traffic_ctl config get` will emit results in internal ats variable format.

   The option :ref:`--cold <traffic_ctl_config_cold>` is available to get the values from a file.

.. program:: traffic_ctl config
.. option:: match [--records, --default] REGEX [REGEX...]

   :ref:`admin_lookup_records`

   Display the current values of all configuration variables whose names match the given regular
   expression. The ``--records`` flag has the same behavior as `traffic_ctl config get
   --records`.

.. program:: traffic_ctl config
.. option:: reload

   :ref:`admin_config_reload`

   Initiate a Traffic Server configuration reload. Use this command to update the running
   configuration after any configuration file modification. If no configuration files have been
   modified since the previous configuration load, this command is a no-op.

   The reload is **asynchronous**: the command sends a JSONRPC request to |TS| and returns
   immediately — it does not block until every config handler finishes. The actual reload work
   runs on background threads (``ET_TASK``), where each registered config handler loads its
   configuration and reports success or failure.

   Every reload is assigned a **token** — a unique identifier for the reload operation. The token
   is the handle you use to track, monitor, or query the reload after it starts. If no token is
   provided via ``--token``, the server generates one automatically using a timestamp (e.g.
   ``rldtk-1739808000000``). You can supply a custom token via ``--token`` (e.g.
   ``-t deploy-v2.1``) to tag reloads with meaningful labels for CI pipelines, deploy scripts, or
   post-mortem analysis.

   Use the token to:

   - **Monitor** a reload in real-time: ``traffic_ctl config reload -t <token> -m``
   - **Query** the final status: ``traffic_ctl config status -t <token>``
   - **Get detailed logs**: ``traffic_ctl config status -t <token> -l``

   The timestamp of the last reconfiguration event (in seconds since epoch) is published in the
   ``proxy.process.proxy.reconfigure_time`` metric.

   **Behavior without options:**

   When called without ``--monitor`` or ``--show-details``, the command sends the reload request
   and immediately prints the assigned token along with suggested next-step commands:

   .. code-block:: bash

      $ traffic_ctl config reload
      ✔ Reload scheduled [rldtk-1739808000000]

        Monitor : traffic_ctl config reload -t rldtk-1739808000000 -m
        Details : traffic_ctl config reload -t rldtk-1739808000000 -s -l

   **When a reload is already in progress:**

   Only one reload can be active at a time. If a reload is already running, the command does
   **not** start a new one. Instead, it reports the in-progress reload's token and provides
   options to monitor it or force a new one:

   .. code-block:: bash

      $ traffic_ctl config reload
      ⟳ Reload in progress [rldtk-1739808000000]

        Monitor : traffic_ctl config reload -t rldtk-1739808000000 -m
        Details : traffic_ctl config status -t rldtk-1739808000000
        Force   : traffic_ctl config reload --force  (may conflict with the running reload)

   With ``--monitor``, it automatically switches to monitoring the in-progress reload instead of
   failing. With ``--show-details``, it displays the current status of the in-progress reload.

   **When a token already exists:**

   If the token provided via ``--token`` was already used by a previous reload (even a completed
   one), the command refuses to start a new reload to prevent ambiguity. Choose a different token
   or omit ``--token`` to let the server generate a unique one:

   .. code-block:: bash

      $ traffic_ctl config reload -t my-deploy
      ✗ Token 'my-deploy' already in use

        Status : traffic_ctl config status -t my-deploy
        Retry  : traffic_ctl config reload

   Supports the following options:

   .. option:: --token, -t <token>

      Assign a custom token to this reload. Tokens must be unique across the reload history — if
      a reload (active or completed) already has this token, the command is rejected. When omitted,
      the server generates a unique token automatically.

      Custom tokens are useful for tagging reloads with deployment identifiers, ticket numbers,
      or other meaningful labels that make it easier to query status later.

      .. code-block:: bash

         $ traffic_ctl config reload -t deploy-v2.1
         ✔ Reload scheduled [deploy-v2.1]

   .. option:: --monitor, -m

      Start the reload and monitor its progress with a live progress bar until completion. The
      progress bar updates in real-time showing the number of completed handlers, overall status,
      and elapsed time.

      Polls the server at regular intervals controlled by ``--refresh-int`` (default: every 0.5
      seconds). Before the first poll, waits briefly (see ``--initial-wait``) to allow the server
      time to dispatch work to all handlers.

      If a reload is already in progress, ``--monitor`` automatically attaches to that reload and
      monitors it instead of failing.

      If both ``--monitor`` and ``--show-details`` are specified, ``--monitor`` is ignored and
      ``--show-details`` takes precedence.

      .. code-block:: bash

         $ traffic_ctl config reload -t deploy-v2.1 -m
         ✔ Reload scheduled [deploy-v2.1]
         ✔ [deploy-v2.1] ████████████████████ 11/11  success  (245ms)

      Failed reload:

      .. code-block:: bash

         $ traffic_ctl config reload -t hotfix-cert -m
         ✔ Reload scheduled [hotfix-cert]
         ✗ [hotfix-cert] ██████████████░░░░░░ 9/11  fail  (310ms)

           Details : traffic_ctl config status -t hotfix-cert

   .. option:: --show-details, -s

      Start the reload and display a detailed status report. The command sends the reload request,
      waits for the configured initial wait (see ``--initial-wait``, default: 2 seconds) to allow
      handlers to start, then fetches and prints the full task tree with per-handler status and
      durations.

      If a reload is already in progress, shows the status of that reload immediately.

      Combine with ``--include-logs`` to also show per-handler log messages.

      .. code-block:: bash

         $ traffic_ctl config reload -s -l
         ✔ Reload scheduled [rldtk-1739808000000]. Waiting for details...

         ✔ Reload [success] — rldtk-1739808000000
           Started : 2025 Feb 17 12:00:00.123
           Duration: 245ms

           Tasks:
            ✔ ip_allow.yaml ·························· 18ms
            ✔ logging.yaml ··························· 120ms
            ...

   .. option:: --include-logs, -l

      Include per-handler log messages in the output. Only meaningful together with
      ``--show-details``. Log messages are set by handlers via ``ctx.log()`` and
      ``ctx.fail()`` during the reload.

   .. option:: --data, -d <source>

      Supply inline YAML configuration content for the reload. The content is passed directly to
      config handlers at runtime and is **not persisted to disk** — a server restart will revert
      to the file-based configuration. A warning is printed after a successful inline reload to
      remind the operator.

      Accepts the following formats:

      - ``@file.yaml`` — read content from a file
      - ``@-`` — read content from stdin
      - ``"yaml: content"`` — inline YAML string
      - Multiple ``-d`` arguments can be provided — their content is merged, with later values
        overriding earlier ones for the same key

      The YAML content uses **registry keys** (e.g. ``ip_allow``, ``sni``) as top-level keys.
      Each key maps to the full configuration content that the handler normally reads from its
      config file. A single file can target multiple handlers:

      .. code-block:: yaml

         # reload_rules.yaml
         # Each top-level key is a registry key.
         # The value is the config content (inner data, not the file's top-level wrapper).
         ip_allow:
           - apply: in
             ip_addrs: 0.0.0.0/0
             action: allow
         sni:
           - fqdn: "*.example.com"
             verify_client: NONE

      .. code-block:: bash

         # Reload from file
         $ traffic_ctl config reload -d @reload_rules.yaml -t update-rules -m

         # Reload from stdin
         $ cat rules.yaml | traffic_ctl config reload -d @- -m

      When used with ``-d``, only the handlers for the keys present in the YAML content are
      invoked — other config handlers are not triggered.

      .. note::

         Inline YAML reload requires the target config handler to support
         ``ConfigSource::FileAndRpc``. Handlers that only support ``ConfigSource::FileOnly``
         will return an error for the corresponding key. The JSONRPC response will contain
         per-key error details.

   .. option:: --force, -F

      Force a new reload even if one is already in progress. Without this flag, the server rejects
      a new reload when one is active.

      .. warning::

         ``--force`` does **not** stop or cancel the running reload. It starts a second reload
         alongside the first one. Handlers from both reloads may execute concurrently on separate
         ``ET_TASK`` threads. This can lead to unpredictable behavior if handlers are not designed
         for concurrent execution. Use this flag only for debugging or recovery situations.

   .. option:: --refresh-int, -r <seconds>

      Set the polling interval in seconds used with ``--monitor``. Accepts fractional values
      (e.g. ``0.5`` for 500ms). Controls how often ``traffic_ctl`` queries the server for
      updated reload status. Default: ``0.5``.

   .. option:: --initial-wait, -w <seconds>

      Initial wait in seconds before the first status poll. After scheduling a reload, the
      server needs a brief moment to dispatch work to all handlers. This delay avoids polling
      before any handler has started, which would show an empty or incomplete task tree.
      Accepts fractional values (e.g. ``1.5``). Default: ``2``.

   **Exit codes for config reload:**

   The ``config reload`` command sets the process exit code to reflect the outcome of the
   reload operation:

   ``0``
      Success. The reload was scheduled (without ``--monitor``) or completed successfully
      (with ``--monitor``).

   ``2``
      Error. The reload reached a terminal failure state (``fail`` or ``timeout``), or an
      RPC communication error occurred.

   ``75``
      Temporary failure (``EX_TEMPFAIL`` from ``sysexits.h``). A reload is already in
      progress and the command could not start a new one, or monitoring was interrupted
      (e.g. Ctrl+C) before the reload reached a terminal state. The caller is invited to
      retry or monitor the operation later.

   Example usage in scripts:

   .. code-block:: bash

      traffic_ctl config reload -m
      rc=$?
      case $rc in
        0)  echo "Reload completed successfully" ;;
        2)  echo "Reload failed" ;;
        75) echo "Reload still in progress, retry later" ;;
      esac

.. program:: traffic_ctl config
.. option:: set RECORD VALUE

   :ref:`admin_config_set_records`

   Set the named configuration record to the specified value. Refer to the :file:`records.yaml`
   documentation for a list of the configuration variables you can specify. Note that this is not a
   synchronous operation.

   Supports the following options.


.. _traffic_ctl_config_cold:

   .. option:: --cold, -c [filename]


   This option indicates to `traffic_ctl` that the action should be performed on a configuration file instead of using the ATS RPC
   facility to store the new value. `traffic_ctl` will save the value in the passed `filename`, if no `filename` passed, then the sysconfig
   :file:`records.yaml` will be attempted to be used.

   ATS supports parsing multiple documents from the same YAML stream, so if you attempt to set a variable on a document with
   none, one or multiple documents then a new document will be appended. In case you want to modify an existing field then `-u` option
   should be passed, so the latest(top to bottom) field will be modified, if there is no variable already set in any of the documents,
   then the new variable will be set in the latest document of the stream.

   Specifying the file name is not needed as `traffic_ctl` will try to use the build(or the runroot if used) information to figure
   out the path to the `records.yaml`.

   If the file exists and is empty a new document will be created. If a file does not exist, an attempt to create a new file will be done.

   This option(only for the config file changes) lets you use the prefix `proxy.config.` or `ts.` for variable names, either would work.
   If different prefix name is prepend, then traffic_ctl will work away using the provided variable name, this may not be what is intended
   so, make sure you use the right prefixes.


   Appending a new field in a records.yaml file.

   .. code-block:: bash

      $ traffic_ctl config set proxy.config.diags.debug.enabled 1 -c records.yaml
      $ cat records.yaml
      records:
      ...
      # Document modified by traffic_ctl Mon Feb 13 23:07:15 2023
      #
      ---
      records:
         diags:
            debug:
               enabled: 1

   .. note::

      The following options are only considered if ``--cold, -c`` is used, ignored otherwise.

   .. option:: --update -u

      Update latest field present. If there is no variable already set in any of the documents, then the new variable will be set in
      the latest document.

   .. option:: --type, -t int | float | str

      Inject a tag information on the modified/new field, this is useful when you set a non registered record inside ATS.

      .. code-block:: bash

         $ traffic_ctl config set ts.some.plugin.config.max 100 -t int -c records.yaml
         $ cat records.yaml
         ...
         # Document modified by traffic_ctl Mon Feb 13 23:07:15 2023
         #
         ---
         records:
            some:
               plugin:
                  config:
                    max: !<tag:yaml.org,2002:int> 100


.. program:: traffic_ctl config
.. option:: reset PATH [PATH ...]

   Reset configuration record(s) to their default values. The PATH argument is used as a
   regex pattern to match against record names. Multiple paths at once can be provided.

   - ``records`` - Reset all configuration records to defaults
   - A partial path like ``proxy.config.http`` or ``records.http`` - Reset all records matching the pattern
   - A full record name like ``proxy.config.diags.debug.enabled`` - Reset a specific record

   **Path Format Support**

   Both record name format and YAML format are supported. Paths starting with ``records.``
   are automatically converted to ``proxy.config.`` before matching:

   ======================================  ======================================
   YAML Format                             Record Name Format
   ======================================  ======================================
   ``records.http``                        ``proxy.config.http``
   ``records.diags.debug.enabled``         ``proxy.config.diags.debug.enabled``
   ``records.cache.ram_cache.size``        ``proxy.config.cache.ram_cache.size``
   ======================================  ======================================

   This allows you to use the same path style as in :file:`records.yaml` configuration files.

   Examples:

   Reset all records to defaults:

   .. code-block:: bash

      $ traffic_ctl config reset records

   Reset all HTTP configuration records (both formats are equivalent):

   .. code-block:: bash

      $ traffic_ctl config reset proxy.config.http
      $ traffic_ctl config reset records.http

   Reset a specific record:

   .. code-block:: bash

      $ traffic_ctl config reset proxy.config.diags.debug.enabled

   Using YAML-style path for the same record:

   .. code-block:: bash

      $ traffic_ctl config reset records.diags.debug.enabled



.. program:: traffic_ctl config
.. option:: status

   :ref:`get_reload_config_status`

   Display the status of configuration reloads. This is a read-only command — it does not trigger
   a reload, it only queries the server for information about past or in-progress reloads.

   **Behavior without options:**

   When called without ``--token`` or ``--count``, shows the most recent reload:

   .. code-block:: bash

      $ traffic_ctl config status
      ✔ Reload [success] — rldtk-1739808000000
        Started : 2025 Feb 17 12:00:00.123
        Finished: 2025 Feb 17 12:00:00.368
        Duration: 245ms

        ✔ 11 success  ◌ 0 in-progress  ✗ 0 failed  (11 total)

        Tasks:
         ✔ logging.yaml ··························· 120ms
         ✔ ip_allow.yaml ·························· 18ms
         ...

   **When no reloads have occurred:**

   If the server has not performed any reloads since startup, the command reports that no reload
   tasks were found.

   **Querying a specific reload:**

   Use ``--token`` to look up a specific reload by its token. If the token does not exist in
   the history, an error is returned:

   .. code-block:: bash

      $ traffic_ctl config status -t nonexistent
      ✗ Token 'nonexistent' not found

   **Failed reload report:**

   When a reload has failed handlers, the output shows which handlers succeeded and which failed,
   along with durations for each:

   .. code-block:: bash

      $ traffic_ctl config status -t hotfix-cert
      ✗ Reload [fail] — hotfix-cert
        Started : 2025 Feb 17 14:30:10.500
        Finished: 2025 Feb 17 14:30:10.810
        Duration: 310ms

        ✔ 9 success  ◌ 0 in-progress  ✗ 2 failed  (11 total)

        Tasks:
         ✔ ip_allow.yaml ·························· 18ms
         ✗ logging.yaml ·························· 120ms  ✗ FAIL
         ✗ ssl_client_coordinator ················· 85ms  ✗ FAIL
         ├─ ✔ sni.yaml ··························· 20ms
         └─ ✗ ssl_multicert.config ··············· 65ms  ✗ FAIL
         ...

   Supports the following options:

   .. option:: --token, -t <token>

      Show the status of a specific reload identified by its token. The token was either assigned
      by the server (e.g. ``rldtk-<timestamp>``) or provided by the operator via
      ``traffic_ctl config reload --token``.

      Returns an error if the token is not found in the reload history.

   .. option:: --count, -c <N|all>

      Show the last ``N`` reload records from the history. Use ``all`` to display every reload
      the server has recorded (up to the internal history limit).

      When ``--count`` is provided, ``--token`` is ignored.

      .. code-block:: bash

         # Show full history
         $ traffic_ctl config status -c all

         # Show last 5 reloads
         $ traffic_ctl config status -c 5

   **JSON output:**

   All ``config status`` commands support the global ``--format json`` option to output the raw
   JSONRPC response as JSON instead of the human-readable format. This is useful for automation,
   CI pipelines, monitoring tools, or any system that consumes structured output directly:

   .. code-block:: bash

      $ traffic_ctl config status -t deploy-v2.1 --format json
      {
        "tasks": [
          {
            "config_token": "deploy-v2.1",
            "status": "success",
            "description": "Main reload task - 2025 Feb 17 12:00:00",
            "sub_tasks": [ ...]
          }
        ]
      }

.. program:: traffic_ctl config
.. option:: registry

   :ref:`filemanager.get_files_registry`

   Display information about the registered files in |TS|. This includes the full file path, config record name, parent config (if any)
   if needs root access and if the file is required in |TS|.

.. program:: traffic_ctl config
.. option:: ssl-multicert show [--yaml | --json]

   Display the current ``ssl_multicert.yaml`` configuration. By default, output is in YAML format.
   Use ``--json`` or ``-j`` to output in JSON format.

   .. option:: --yaml, -y

      Output in YAML format (default).

   .. option:: --json, -j

      Output in JSON format.

   Example:

   .. code-block:: bash

      $ traffic_ctl config ssl-multicert show
      ssl_multicert:
        - ssl_cert_name: server.pem
          dest_ip: "*"
          ssl_key_name: server.key

   .. code-block:: bash

      $ traffic_ctl config ssl-multicert show --json
      {"ssl_multicert": [{"ssl_cert_name": "server.pem", "dest_ip": "*", "ssl_key_name": "server.key"}]}

.. _traffic-control-command-metric:

traffic_ctl metric
------------------

.. program:: traffic_ctl metric
.. option:: get METRIC [METRIC...]

   :ref:`admin_lookup_records`

   Display the current value of the specified statistics.

.. program:: traffic_ctl metric
.. option:: match REGEX [REGEX...]

   :ref:`admin_lookup_records`

   Display the current values of all statistics whose names match
   the given regular expression.

.. program:: traffic_ctl metric
.. option:: describe RECORD [RECORD...]

   :ref:`admin_lookup_records`

   Display all the known information about a metric record.

.. program:: traffic_ctl metric
.. option:: monitor [-i, -c] METRIC [METRIC...]

   Display the current value of the specified metric(s) using an interval time
   and a count value. Use ``-i`` to set the interval time between requests, and
   ``-c`` to set the number of requests the program will send in total per metric.
   The program will terminate execution after requesting <count> metrics.
   If ``count=0`` is passed or ``count`` is not specified then the program should be terminated
   by SIGINT.
   Note that the metric will display `+` or `-` depending on the value of the last
   metric and the current being shown, if current is greater, then  `+` will be
   added beside the metric value, `-` if the last value is less than current,
   and no symbol is the same.

   Example:

   .. code-block:: bash

      $ traffic_ctl  metric monitor proxy.process.eventloop.time.min.10s -i 2 -c 10
      proxy.process.eventloop.time.min.10s: 4025085
      proxy.process.eventloop.time.min.10s: 4025085
      proxy.process.eventloop.time.min.10s: 4025085
      proxy.process.eventloop.time.min.10s: 4025085
      proxy.process.eventloop.time.min.10s: 4011194 -
      proxy.process.eventloop.time.min.10s: 4011194
      proxy.process.eventloop.time.min.10s: 4011194
      proxy.process.eventloop.time.min.10s: 4011194
      proxy.process.eventloop.time.min.10s: 4011194
      proxy.process.eventloop.time.min.10s: 4018669 +
      --- metric monitor statistics(10) ---
      ┌ proxy.process.eventloop.time.min.10s
      └─ min/avg/max = 4011194/4017498/4025085

.. _traffic-control-command-server:

traffic_ctl server
------------------

.. program:: traffic_ctl server

.. _traffic-control-command-server-drain:

.. program:: traffic_ctl server
.. option:: drain

   Enable graceful connection draining mode. When enabled, |TS| signals clients
   to close their connections gracefully by:

   - Adding ``Connection: close`` headers to HTTP/1.1 responses
   - Sending GOAWAY frames to HTTP/2 clients for graceful shutdown

   This allows active connections to complete naturally while encouraging clients
   to establish new connections. |TS| continues accepting new connections while
   in drain mode.

   .. note::

      Drain mode does NOT close listening sockets or terminate existing connections.
      It only signals to clients that they should close their connections.

   .. option:: --undo, -U

      Disable drain mode and return to normal operation.

   .. option:: --no-new-connection, -N

      This option is accepted but not yet implemented. It has no effect in the
      current version.

   Examples:

   Enable drain mode:

   .. code-block:: bash

      $ traffic_ctl server drain

   Disable drain mode:

   .. code-block:: bash

      $ traffic_ctl server drain --undo

   Check drain status:

   .. code-block:: bash

      $ traffic_ctl server status | jq '.is_draining'
      "false"

   For detailed information about drain behavior and best practices, see
   :ref:`admin-graceful-shutdown`.

   See also:

   - :ref:`admin_server_start_drain` (JSONRPC API)
   - :ref:`admin_server_stop_drain` (JSONRPC API)


.. _traffic-control-command-server-status:

.. program:: traffic_ctl server
.. option:: status

   Display basic |TS| internal running information.

   Example:

   .. code-block:: bash

      $ traffic_ctl server status | jq
      {
         "initialized_done": "true",
         "is_ssl_handshaking_stopped": "false",
         "is_draining": "false",
         "is_event_system_shut_down": "false"
      }

.. _traffic-control-command-server-debug:

.. program:: traffic_ctl server
.. option:: debug enable

   Enables diagnostic messages at runtime. This is equivalent to
   manually setting the below records but this is done in one go.

   Note that if you just set this to enable, the :ts:cv:`proxy.config.diags.debug.enabled`
   will be set to ``1`` unless you specify the ``--client_ip,-c`` option.

   :ts:cv:`proxy.config.diags.debug.enabled`

   :ts:cv:`proxy.config.diags.debug.tags`

   :ts:cv:`proxy.config.diags.debug.client_ip`


   Enables logging for diagnostic messages. See :ts:cv:`proxy.config.diags.debug.enabled` for information.

   .. option:: --tags, -t  tags

   This string should contain an anchored regular expression that filters the messages based on the debug tag tag.
   Please refer to :ts:cv:`proxy.config.diags.debug.tags` for more information

   .. option:: --append, -a

   Append the specified tags to the existing debug tags instead of replacing them. This option requires
   ``--tags`` to be specified. The new tags will be combined with existing tags using the ``|`` separator.

   .. option:: --client_ip, -c ip

   Please see :ts:cv:`proxy.config.diags.debug.client_ip` for information.



.. program:: traffic_ctl server
.. option:: debug disable

   Disables logging for diagnostic messages. Equivalent to set :ts:cv:`proxy.config.diags.debug.enabled` to ``0``.


   Examples:

   .. code-block:: bash

      # Set debug tags (replaces existing tags)
      $ traffic_ctl server debug enable --tags "quic|quiche"
      ■ TS Runtime debug set to »ON(1)« - tags »"quic|quiche"«, client_ip »unchanged«

      # Append debug tags to existing tags
      $ traffic_ctl server debug enable --tags "http" --append
      ■ TS Runtime debug set to »ON(1)« - tags »"quic|quiche|http"«, client_ip »unchanged«

      # Disable debug logging
      $ traffic_ctl server debug disable
      ■ TS Runtime debug set to »OFF(0)«

.. _traffic-control-command-storage:

traffic_ctl storage
-------------------

.. program:: traffic_ctl storage
.. option:: offline PATH [PATH ...]

   :ref:`admin_storage_set_device_offline`

   Mark a cache storage device as offline. The storage is identified by :arg:`PATH` which must match
   exactly a path specified in :file:`storage.config`. This removes the storage from the cache and
   redirects requests that would have used this storage to other storage. This has exactly the same
   effect as a disk failure for that storage. This does not persist across restarts of the
   :program:`traffic_server` process.

.. program:: traffic_ctl storage
.. option:: status PATH [PATH ...]

   :ref:`admin_storage_get_device_status`

   Show the storage configuration status.

.. _traffic-control-command-plugin:

traffic_ctl plugin
-------------------

.. program:: traffic_ctl plugin
.. option:: msg TAG DATA

   :ref:`admin_plugin_send_basic_msg`

   Send a message to plugins. All plugins that have hooked the
   ``TSLifecycleHookID::TS_LIFECYCLE_MSG_HOOK`` will receive a callback for that hook.
   The :arg:`TAG` and :arg:`DATA` will be available to the plugin hook processing. It is expected
   that plugins will use :arg:`TAG` to select relevant messages and determine the format of the
   :arg:`DATA`.

.. _traffic-control-command-host:

traffic_ctl host
----------------
.. program:: traffic_ctl host

A record to track status is created for each host. The name is the host fqdn.  The value of the
record when retrieved, is a serialized string representation of the status.
This contains the overall status and the status for each reason.  The
records may be viewed using the :program:`traffic_ctl host status` command.

.. option:: --time count

   Set the duration of an operation to ``count`` seconds. A value of ``0`` means no duration, the
   condition persists until explicitly changed. The default is ``0`` if an operation requires a time
   and none is provided by this option.

.. option:: --reason active | local | manual

   Sets the reason for the operation.

   ``active``
      Set the active health check reason.

   ``local``
      Set the local health check reason.

   ``manual``
      Set the administrative reason. This is the default reason if a reason is needed and not
      provided by this option.

   Internally the reason can be ``self_detect`` if
   :ts:cv:`proxy.config.http.parent_proxy.self_detect` is set to the value 2 (the default). This is
   used to prevent parent selection from creating a loop by selecting itself as the upstream by
   marking this reason as "down" in that case.

   .. note::

      The up / down status values are independent, and a host is consider available if and only if
      all of the statuses are "up".

.. option:: status HOSTNAME [HOSTNAME ...]

   :ref:`admin_lookup_records`

   Get the current status of the specified hosts with respect to their use as targets for parent
   selection. If the HOSTNAME arguments are omitted, all host records available are returned.

.. option:: down HOSTNAME [HOSTNAME ...]

   :ref:`admin_host_set_status`

   Marks the listed hosts as down so that they will not be chosen as a next hop parent. If
   :option:`--time` is included the host is marked down for the specified number of seconds after
   which the host will automatically be marked up. A host is not marked up until all reason codes
   are cleared by marking up the host for the specified reason code.

   Supports :option:`--time`, :option:`--reason`.

.. option:: up HOSTNAME [HOSTNAME ...]

   :ref:`admin_host_set_status`

   Marks the listed hosts as up so that they will be available for use as a next hop parent. Use
   :option:`--reason` to mark the host reason code. The 'self_detect' is an internal reason code
   used by parent selection to mark down a parent when it is identified as itself and


   Supports :option:`--reason`.

.. _traffic_ctl_rpc:

traffic_ctl hostdb
------------------
.. program:: traffic_ctl hostdb

.. option:: status [HOSTNAME]

   :ref:`admin_lookup_records`

   Get the current status of HostDB.

   If ``HOSTNAME`` is specified, the output is filtered to show only records whose names contain the given string.

traffic_ctl rpc
---------------
.. program:: traffic_ctl rpc

A mechanism to interact directly with the |TS| |RPC| endpoint. This means that this is not tied to any particular API
but rather to the rpc endpoint, so you can directly send requests and receive responses from the server.

.. option:: file

   Reads a file or a set of files from the disc, use the content of the files as message(s) to the |RPC| endpoint. All jsonrpc messages
   will be validated before sending them. If the file contains invalid  json|yaml format the message will not be send, in
   case of a set of files, if a particular file is not a proper json/yaml format then that particular file will be skipped.

   Example:

   .. code-block:: bash

      traffic_ctl rpc file jsonrpc_cmd1.json jsonrpc_cmd2.yaml

.. option:: get-api

   :ref:`show_registered_handlers`

   Request the entire admin api. This will retrieve all the registered methods and notifications on the server side.

   Example:

   .. code-block:: bash

      $ traffic_ctl rpc get-api
      Methods:
      - admin_host_set_status
      - admin_server_stop_drain
      - admin_server_start_drain
      - admin_plugin_send_basic_msg
      - admin_lookup_records
      - admin_config_set_records
      - admin_storage_get_device_status
      - admin_storage_set_device_offline
      - admin_config_reload
      - show_registered_handlers
      Notifications:
      - some_registered_notification_handler


.. option:: input

   Input mode, traffic_ctl will provide a control input from a stream buffer. Once the content is written the terminal :program:`traffic_ctl`
   will wait for the user to press Control-D to send the request to the rpc endpoint.
   This feature allows you to directly interact with the jsonrpc endpoint and test your API easily and without the need to know the low level
   implementation details of the transport layer.
   :program:`traffic_ctl` will validate the input format, not the message content. The message content will be validated by the server.
   See example `input_example_2`_.

   .. option:: --raw, -r

      No json/yaml parse validation will take place, the input content will be directly send to the server.

   Example:

   .. code-block::

      $ traffic_ctl rpc input
      >> Ctrl-D to fire the request
      {
         "id":"86e59b43-185b-4a0b-b1c1-babb1a3d5401",
         "jsonrpc":"2.0",
         "method":"admin_lookup_records",
         "params":[
            {
               "record_name":"proxy.config.diags.debug.tags",
               "rec_types":[
                  "1",
                  "16"
               ]
            }
         ]
      }
      <pressed Ctrl-D>

      <-- Server's response.
      {
         "jsonrpc":"2.0",
         "result":{
            "recordList":[
               {
                  "record":{
                     "record_name":"proxy.config.diags.debug.tags",
                     "record_type":"3",
                     "version":"0",
                     "raw_stat_block":"0",
                     "order":"423",
                     "config_meta":{
                        "access_type":"0",
                        "update_status":"0",
                        "update_type":"1",
                        "checktype":"0",
                        "source":"3",
                        "check_expr":"null"
                     },
                     "record_class":"1",
                     "overridable":"false",
                     "data_type":"STRING",
                     "current_value":"rpc",
                     "default_value":"http|dns"
                  }
               }
            ]
         },
         "id":"86e59b43-185b-4a0b-b1c1-babb1a3d5401"
      }


.. _input_example_2:

   Example 2:

   You can see a valid  json ``{}`` but an invalid |RPC| message. In this case the server is responding.

   .. code-block::

      $ traffic_ctl rpc input
      >> Ctrl-D to fire the request
      {}
      <pressed Ctrl-D>
      < -- Server's response
      {
         "jsonrpc":"2.0",
         "error":{
            "code":-32600,
            "message":"Invalid Request"
         }
      }


.. option:: invoke

   Invoke a remote call by using the method name as parameter. This could be a handy option if you are developing a new handler or you
   just don't want to expose the method in :program:`traffic_ctl`, for instance when implementing a custom handler inside a proprietary plugin.

   .. option:: --params, -p

      Parameters to be passed in the request, YAML or JSON format are accepted. If JSON is passed as param it should not
      be mixed with YAML. It's important that you follow the :ref:`jsonrpc-protocol` specs. If the passed param does not
      follows the specs the server will reject the request.

.. _rpc_invoke_example_1:

   Example 1:

   Call a jsonrpc method with no parameter.

   .. code-block::

      $ traffic_ctl rpc invoke some_jsonrpc_handler
      --> {"id": "0dbab88d-b78f-4ebf-8aa3-f100031711a5", "jsonrpc": "2.0", "method": "some_jsonrpc_handler"}
      <-- { response }

.. _rpc_invoke_example_2:

   Example 2:

   Call a jsonrpc method with parameters.

   .. code-block::

      $ traffic_ctl rpc invoke reload_files_from_folder --params 'filenames: ["file1", "file2"]' 'folder: "/path/to/folder"'
      --> {"id": "9ac68652-5133-4d5f-8260-421baca4c67f", "jsonrpc": "2.0", "method": "reload_files_from_folder", "params": {"filenames": ["file1", "file2"], "folder": "/path/to/folder"}}
      <-- { response }

Examples
========

Mark down a host with `traffic_ctl` and view the associated host stats::

   .. code-block:: bash

      # traffic_ctl host down cdn-cache-02.foo.com --reason manual

      # traffic_ctl metric match host_status
      proxy.process.host_status.cdn-cache-01.foo.com HOST_STATUS_DOWN,ACTIVE:UP:0:0,LOCAL:UP:0:0,MANUAL:DOWN:1556896844:0,SELF_DETECT:UP:0
      proxy.process.host_status.cdn-cache-02.foo.com HOST_STATUS_UP,ACTIVE:UP:0:0,LOCAL:UP:0:0,MANUAL:UP:0:0,SELF_DETECT:UP:0
      proxy.process.host_status.cdn-cache-origin-01.foo.com HOST_STATUS_UP,ACTIVE:UP:0:0,LOCAL:UP:0:0,MANUAL:UP:0:0,SELF_DETECT:UP:0

In the example above, 'cdn-cache-01.foo.com' is unavailable, `HOST_STATUS_DOWN` and was marked down
for the `manual` reason, `MANUAL:DOWN:1556896844:0`, at the time indicated by the UNIX time stamp
`1556896844`.  To make the host available, one would have to clear the `manual` reason using:

   .. code-block:: bash

      # traffic_ctl host up cdn-cache-01.foo.com --reason manual

Configure Traffic Server to insert ``Via`` header in the response to the client

   .. code-block:: bash

      # traffic_ctl config set proxy.config.http.insert_response_via_str 1
      # traffic_ctl config reload

Autest
======

Runroot needs to be configured in order to let `traffic_ctl` know where to find the socket. This is done by default
and there is no change you have to do to interact with it, but make sure that you are not overriding the `dump_runroot=False`
when creating the ATS Process, otherwise the `runroot.yaml` will not be set.

Exit Codes
==========

:program:`traffic_ctl` uses the following exit codes:

``0``
   Success. The requested operation completed successfully.

``2``
   Error. The operation failed. This may be returned when:

   - The RPC communication with :program:`traffic_server` failed (e.g. socket not found or connection refused).
   - The server response contains an error (e.g. invalid record name, malformed request).

``3``
   Unimplemented. The requested command is not yet implemented.

``75``
   Temporary failure (aligned with ``EX_TEMPFAIL`` from ``sysexits.h``). The caller is invited to retry later.

See also
========

:manpage:`records.yaml(5)`,
:manpage:`storage.config(5)`,
:ref:`admin-jsonrpc-configuration`,
:ref:`jsonrpc-protocol`
