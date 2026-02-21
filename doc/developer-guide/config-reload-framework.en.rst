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

.. include:: ../common.defs

.. _config-reload-framework:

Configuration Reload Framework
******************************

This guide explains how to integrate a configuration module with the |TS| reload framework.
It covers registering handlers, reporting progress through ``ConfigContext``, and the rules
every handler must follow.

Overview
========

``ConfigRegistry`` is a centralized singleton that manages all configuration files, their reload
handlers, trigger records, and file dependencies. When a reload is requested (via
:program:`traffic_ctl` or the JSONRPC API — see :ref:`admin_config_reload` and
:ref:`get_reload_config_status`), it coordinates execution, tracks progress per handler,
and records the result in a queryable history.

Key capabilities:

- **Traceability** — every reload gets a token. Each handler reports its status and the results
  are aggregated into a task tree with per-handler timings and logs.
- **Centralized registration** — one place for config files, filename records, trigger records, and
  handlers.
- **Inline YAML injection** — handlers that opt in can receive YAML content directly via the RPC,
  without writing to disk.
- **Coordinated reload sessions** — concurrency control, timeout detection, and history.


Registration API
================

All registration calls are made during module startup, typically from a ``startup()`` method.

register_config
---------------

Register a file-based configuration handler.

.. code-block:: cpp

   void ConfigRegistry::register_config(
       const std::string &key,                        // unique registry key (e.g. "ip_allow")
       const std::string &default_filename,           // default filename (e.g. "ip_allow.yaml")
       const std::string &filename_record,            // record holding the filename, or "" if fixed
       ConfigReloadHandler handler,                   // reload callback
       ConfigSource source,                           // content source (FileOnly, FileAndRpc)
       std::initializer_list<const char *> triggers = {},  // records that trigger reload (optional)
       bool is_required = false                        // whether the file must exist on disk
   );

This is the primary registration method. It:

1. Adds the entry to the registry.
2. Registers the file with ``FileManager`` for mtime-based change detection.
3. Wires ``RecRegisterConfigUpdateCb`` callbacks for each trigger record.

**Example — ip_allow:**

.. code-block:: cpp

   config::ConfigRegistry::Get_Instance().register_config(
       "ip_allow",                                           // registry key
       ts::filename::IP_ALLOW,                               // default filename
       "proxy.config.cache.ip_allow.filename",               // record holding the filename
       [](ConfigContext ctx) { IpAllow::reconfigure(ctx); }, // handler
       config::ConfigSource::FileOnly,                       // no inline content
       {"proxy.config.cache.ip_allow.filename"});            // trigger records


register_record_config
----------------------

Register a handler that has no config file — it only reacts to record changes.

.. code-block:: cpp

   void ConfigRegistry::register_record_config(
       const std::string &key,                        // unique registry key
       ConfigReloadHandler handler,                   // reload callback
       std::initializer_list<const char *> triggers   // records that trigger reload
   );

Use this for modules like ``SSLTicketKeyConfig`` that are reloaded via record changes and need
visibility in the reload infrastructure, or for pure coordinator entries that own child file
dependencies.

**Example — ssl_client_coordinator (pure coordinator):**

.. code-block:: cpp

   config::ConfigRegistry::Get_Instance().register_record_config(
       "ssl_client_coordinator",
       [](ConfigContext ctx) { SSLClientCoordinator::reconfigure(ctx); },
       {"proxy.config.ssl.client.cert.path",
        "proxy.config.ssl.client.cert.filename",
        "proxy.config.ssl.server.session_ticket.enable"});


register_static_file
--------------------

Register a non-reloadable config file for inventory purposes. Static files have no reload handler
and no trigger records. This allows the registry to serve as the single source of truth for all
known configuration files, so that RPC endpoints (e.g. ``filemanager.get_files_registry``) can
expose this information.

.. code-block:: cpp

   void ConfigRegistry::register_static_file(
       const std::string &key,                        // unique registry key (e.g. "storage")
       const std::string &default_filename,           // default filename (e.g. "storage.config")
       const std::string &filename_record = {},       // record holding the filename (optional)
       bool is_required = false                        // whether the file must exist on disk
   );

Internally this delegates to ``register_config()`` with a ``nullptr`` handler, no trigger records,
and ``ConfigSource::FileOnly``. The file is registered with ``FileManager`` for mtime tracking
but no reload callback is wired.

**Example — startup-only files:**

.. code-block:: cpp

   auto &reg = config::ConfigRegistry::Get_Instance();
   reg.register_static_file("storage", ts::filename::STORAGE, {}, true);
   reg.register_static_file("socks", ts::filename::SOCKS, "proxy.config.socks.socks_config_file");
   reg.register_static_file("volume", ts::filename::VOLUME);
   reg.register_static_file("plugin", ts::filename::PLUGIN);
   reg.register_static_file("jsonrpc", ts::filename::JSONRPC, "proxy.config.jsonrpc.filename");


attach
------

Add an additional trigger record to an existing config entry. Can be called from any module at
any time after the entry has been registered.

.. code-block:: cpp

   int ConfigRegistry::attach(const std::string &key, const char *record_name);

Returns ``0`` on success, ``-1`` if the key is not found.

**Example:**

.. code-block:: cpp

   config::ConfigRegistry::Get_Instance().attach("ip_allow", "proxy.config.some.extra.record");


add_file_dependency
-------------------

Register an auxiliary file that a config module depends on. When this file changes on disk,
the parent config's handler is invoked.

.. code-block:: cpp

   int ConfigRegistry::add_file_dependency(
       const std::string &key,           // parent config key (must exist)
       const char *filename_record,      // record holding the filename
       const char *default_filename,     // default filename
       bool is_required                  // whether the file must exist
   );

**Example — ip_categories as a dependency of ip_allow:**

.. code-block:: cpp

   config::ConfigRegistry::Get_Instance().add_file_dependency(
       "ip_allow",
       "proxy.config.cache.ip_categories.filename",
       ts::filename::IP_CATEGORIES,
       false);


add_file_and_node_dependency
----------------------------

Like ``add_file_dependency()``, but also registers a **dependency key** so the RPC handler can
route inline YAML content to the parent entry's handler.

.. code-block:: cpp

   int ConfigRegistry::add_file_and_node_dependency(
       const std::string &key,           // parent config key (must exist)
       const std::string &dep_key,       // unique dependency key for RPC routing
       const char *filename_record,      // record holding the filename
       const char *default_filename,     // default filename
       bool is_required                  // whether the file must exist
   );

**Example — sni.yaml as a dependency of ssl_client_coordinator:**

.. code-block:: cpp

   config::ConfigRegistry::Get_Instance().add_file_and_node_dependency(
       "ssl_client_coordinator", "sni",
       "proxy.config.ssl.servername.filename",
       ts::filename::SNI, false);


ConfigContext API
=================

``ConfigContext`` is a lightweight value type passed to reload handlers. It provides methods to
report progress and access inline YAML content.

``ConfigContext`` is copyable (cheap — holds a ``weak_ptr`` and a ref-counted ``YAML::Node``).
Move is intentionally suppressed: ``std::move(ctx)`` silently copies, keeping the original valid.

in_progress(text)
   Mark the task as in-progress. Accepts an optional message.

log(text)
   Append a log message to the task. These appear in
   ``traffic_ctl config status -l`` output
   and in :ref:`get_reload_config_status` JSONRPC responses.

complete(text)
   Mark the task as successfully completed.

fail(reason) / fail(errata, summary)
   Mark the task as failed. Accepts a plain string or a ``swoc::Errata`` with a summary.

supplied_yaml()
   Returns the YAML node supplied via the RPC ``-d`` flag or ``configs`` parameter. If no inline
   content was provided, the returned node is undefined (``operator bool()`` returns ``false``).

add_dependent_ctx(description)
   Create a child sub-task. The parent aggregates status from all its children.

All methods support ``swoc::bwprint`` format strings:

.. code-block:: cpp

   ctx.in_progress("Parsing {} rules", count);
   ctx.fail(errata, "Failed to load {}", filename);


.. _config-context-terminal-state:

Terminal State Rule
===================

.. warning::

   **Every** ``ConfigContext`` **must reach a terminal state** — either ``complete()`` or ``fail()``
   — **before the handler returns.** This is the single most important rule of the framework.

The entire tracing model depends on handlers reaching a terminal state. If a handler returns without
calling ``complete()`` or ``fail()``:

- The task stays **IN_PROGRESS** indefinitely.
- The parent task (and the entire reload) cannot finish.
- ``traffic_ctl config status`` will show the reload as stuck.
- Eventually, the timeout checker will mark the task as **TIMEOUT**
  (configurable via ``proxy.config.admin.reload.timeout``, default: 1 hour —
  see :ref:`reload-framework-records` below).

**Correct handler pattern:**

.. code-block:: cpp

   void MyConfig::reconfigure(ConfigContext ctx) {
       ctx.in_progress("Loading myconfig");

       auto [errata, config] = load_my_config();
       if (!errata.is_ok()) {
           ctx.fail(errata, "Failed to load myconfig");
           return;  // always return after fail
       }

       // ... apply config ...

       ctx.complete("Loaded successfully");
   }

**Every code path must end in** ``complete()`` **or** ``fail()`` — including error paths, early
returns, and exception handlers.

**Child contexts follow the same rule.** If you call ``add_dependent_ctx()``, every child must
also reach a terminal state:

.. code-block:: cpp

   void SSLClientCoordinator::reconfigure(ConfigContext ctx) {
       ctx.in_progress();

       SSLConfig::reconfigure(ctx.add_dependent_ctx("SSLConfig"));
       SNIConfig::reconfigure(ctx.add_dependent_ctx("SNIConfig"));

       ctx.complete("SSL configs reloaded");
   }

**Deferred handlers** — some handlers schedule work on other threads and return before completion.
The ``ConfigContext`` they hold remains valid across threads. They must call ``ctx.complete()`` or
``ctx.fail()`` from whatever thread finishes the work. If they don't, the timeout checker will mark
the task as ``TIMEOUT``.

.. note::

   ``ctx.complete()`` and ``ctx.fail()`` are **thread-safe**. The underlying
   ``ConfigReloadTask`` guards all state transitions with a ``std::shared_mutex``. Once a task
   reaches a terminal state, subsequent calls are rejected (a warning is logged). This means
   calling ``complete()`` or ``fail()`` from any thread — including a different ``ET_TASK``
   thread or a callback — is safe.

After ``ConfigRegistry::execute_reload()`` calls the handler, it checks whether the context reached
a terminal state and emits a warning if not:

.. code-block:: cpp

   entry_copy.handler(ctx);
   if (!ctx.is_terminal()) {
       Warning("Config '%s' handler returned without reaching a terminal state. "
               "If the handler deferred work to another thread, ensure ctx.complete() or "
               "ctx.fail() is called when processing finishes.",
               entry_copy.key.c_str());
   }


Parent Status Aggregation
-------------------------

Parent tasks derive their status from their children:

- **Any child failed or timed out** → parent is ``FAIL``
- **Any child still in progress** → parent stays ``IN_PROGRESS``
- **All children succeeded** → parent is ``SUCCESS``

This aggregation is recursive. A parent's ``complete()`` call sets its own status, but if any child
later fails, the parent status will be downgraded accordingly.


ConfigSource
============

``ConfigSource`` declares what content sources a handler supports:

``FileOnly``
   The handler only reloads from its file on disk. This is the default for most configs.
   Inline YAML via the RPC (:ref:`admin_config_reload`) is rejected.

``RecordOnly``
   The handler only reacts to record changes. It has no config file and no RPC content.
   Used by ``register_record_config()`` implicitly.

``FileAndRpc``
   The handler can reload from file **or** from YAML content supplied via the RPC. The handler
   checks ``ctx.supplied_yaml()`` to determine the source at runtime.


ConfigType
==========

``ConfigType`` identifies the file format. It is **auto-detected** from the filename extension
during registration:

- ``.yaml``, ``.yml`` → ``ConfigType::YAML``
- All others → ``ConfigType::LEGACY``

You do not set this manually — ``register_config()`` infers it from the ``default_filename``.


Adding a New Config Module
==========================

Step-by-step guide for adding a new configuration file to the reload framework.

Step 1: Choose a Registry Key
------------------------------

Pick a short, lowercase, underscore-separated name that identifies the config. This key is used
in ``traffic_ctl config status`` output, JSONRPC APIs, and inline YAML reload files.

Examples: ``ip_allow``, ``logging``, ``cache_control``, ``ssl_ticket_key``, ``ssl_client_coordinator``

For record-only configs (registered via ``register_record_config()``), the key identifies a group
of records that share a handler — e.g. ``ssl_client_coordinator``.

.. note::

   Not all records support runtime reload. Records declared with ``RECU_DYNAMIC`` in
   ``RecordsConfig.cc`` can trigger a handler at runtime. Records marked ``RECU_RESTART_TS``
   require a server restart and are **not** affected by the reload framework. Only register
   records that are ``RECU_DYNAMIC`` as trigger records for your handler.

Step 2: Accept a ``ConfigContext`` Parameter
--------------------------------------------

Your handler function must accept a ``ConfigContext`` parameter. Use a default value so the
handler can also be called at startup without a reload context:

.. code-block:: cpp

   // In the header — any function name is fine, "reconfigure" is the common convention:
   static void reconfigure(ConfigContext ctx = {});

A default-constructed ``ConfigContext{}`` is a **no-op context**: all status calls
(``in_progress()``, ``complete()``, ``fail()``, ``log()``) are safe no-ops. This means the
same handler works at startup (no active reload) and during a reload (with tracking).

Step 3: Report Progress
-----------------------

Inside the handler, report progress through the context:

.. code-block:: cpp

   void MyConfig::reconfigure(ConfigContext ctx) {
       ctx.in_progress();

       // ... load and parse config ...

       if (error) {
           ctx.fail(errata, "Failed to load myconfig.yaml");
           return;
       }

       ctx.log("Loaded {} rules", rule_count);
       ctx.complete("Finished loading");
   }

.. warning::

   Remember the :ref:`terminal state rule <config-context-terminal-state>`:
   every code path must end with ``complete()`` or ``fail()``.

Step 4: Register with ``ConfigRegistry``
-----------------------------------------

Call ``register_config()`` (or ``register_record_config()``) during your module's initialization
— typically in a function you call at server startup. The function name is up to you; the
convention in existing code is ``startup()``, but any name works.

.. code-block:: cpp

   void MyConfig::startup() {   // or init(), or any name
       config::ConfigRegistry::Get_Instance().register_config(
           "myconfig",                                            // registry key
           "myconfig.yaml",                                       // default filename
           "proxy.config.mymodule.filename",                      // record holding filename
           [](ConfigContext ctx) { MyConfig::reconfigure(ctx); }, // handler
           config::ConfigSource::FileOnly,                        // content source
           {"proxy.config.mymodule.filename"});                   // triggers

       // Initial load — ConfigContext{} is a no-op, so all ctx calls are safe
       reconfigure();
   }

Step 5: Add File Dependencies (if needed)
------------------------------------------

If your config depends on auxiliary files, register them:

.. code-block:: cpp

   config::ConfigRegistry::Get_Instance().add_file_dependency(
       "myconfig",
       "proxy.config.mymodule.aux_filename",
       "myconfig_aux.yaml",
       false);  // not required

Step 6: Support Inline YAML (optional)
---------------------------------------

To accept YAML content via the RPC (``traffic_ctl config reload -d`` /
:ref:`admin_config_reload` with ``configs``):

1. Change the source to ``ConfigSource::FileAndRpc`` in the registration call.
2. Check ``ctx.supplied_yaml()`` in the handler:

.. code-block:: cpp

   void MyConfig::reconfigure(ConfigContext ctx) {
       ctx.in_progress();

       YAML::Node root;
       if (auto yaml = ctx.supplied_yaml()) {
           // Inline mode: content from RPC. Not persisted to disk.
           root = yaml;
       } else {
           // File mode: read from disk.
           root = YAML::LoadFile(config_filename);
       }

       // ... parse and apply ...

       ctx.complete("Loaded successfully");
   }


Composite Configs
=================

Some config modules coordinate multiple sub-configs. For example, ``SSLClientCoordinator`` owns
``sni.yaml`` and ``ssl_multicert.config`` as children.

Pattern:

1. Register with ``register_record_config()`` (no primary file).
2. Add file dependencies with ``add_file_and_node_dependency()`` for each child.
3. In the handler, create child contexts with ``add_dependent_ctx()``.

.. code-block:: cpp

   void SSLClientCoordinator::startup() {
       config::ConfigRegistry::Get_Instance().register_record_config(
           "ssl_client_coordinator",
           [](ConfigContext ctx) { SSLClientCoordinator::reconfigure(ctx); },
           {"proxy.config.ssl.client.cert.path",
            "proxy.config.ssl.server.session_ticket.enable"});

       config::ConfigRegistry::Get_Instance().add_file_and_node_dependency(
           "ssl_client_coordinator", "sni",
           "proxy.config.ssl.servername.filename", "sni.yaml", false);

       config::ConfigRegistry::Get_Instance().add_file_and_node_dependency(
           "ssl_client_coordinator", "ssl_multicert",
           "proxy.config.ssl.server.multicert.filename", "ssl_multicert.config", false);
   }

   void SSLClientCoordinator::reconfigure(ConfigContext ctx) {
       ctx.in_progress();
       SSLConfig::reconfigure(ctx.add_dependent_ctx("SSLConfig"));
       SNIConfig::reconfigure(ctx.add_dependent_ctx("SNIConfig"));
       SSLCertificateConfig::reconfigure(ctx.add_dependent_ctx("SSLCertificateConfig"));
       ctx.complete("SSL configs reloaded");
   }

In :option:`traffic_ctl config status`, this renders as a tree:

.. code-block:: text

   ✔ ssl_client_coordinator ················· 35ms
   ├─ ✔ SSLConfig ·························· 10ms
   ├─ ✔ SNIConfig ·························· 12ms
   └─ ✔ SSLCertificateConfig ·············· 13ms


Startup vs. Reload
==================

A common pattern is to call the same handler at startup (initial config load) and during runtime
reloads, but this is not mandatory — it is up to the developer. The only requirement is that the
handler exposed to ``ConfigRegistry`` accepts a ``ConfigContext`` parameter.

At startup there is no active reload task, so all ``ConfigContext`` methods are **safe no-ops** —
they check the internal weak pointer and return immediately.

This means the same handler code works in both cases without branching:

.. code-block:: cpp

   void MyConfig::reconfigure(ConfigContext ctx) {
       ctx.in_progress();  // no-op at startup, tracks progress during reload
       // ... load config ...
       ctx.complete();     // no-op at startup, marks task as SUCCESS during reload
   }


Thread Model
============

All reload work runs on **ET_TASK** threads — never on the RPC thread or event-loop threads.

1. **RPC thread** — receives the JSONRPC request (:ref:`admin_config_reload`), creates the reload
   token and task via ``ReloadCoordinator::prepare_reload()``, schedules the actual work on
   ``ET_TASK``, and returns immediately. The RPC response is sent back before any handler runs.

2. **ET_TASK — file-based reload** — ``ReloadWorkContinuation`` fires on ``ET_TASK``. It calls
   ``FileManager::rereadConfig()``, which walks every registered file and invokes
   ``ConfigRegistry::execute_reload()`` for each changed config. Each handler runs synchronously.

3. **ET_TASK — inline (RPC) reload** — ``ScheduledReloadContinuation`` fires on ``ET_TASK``.
   It calls ``ConfigRegistry::execute_reload()`` directly for the targeted config key(s).

4. **Deferred handlers** — some handlers schedule work on other threads and return before
   completion. The ``ConfigContext`` remains valid across threads. The handler must call
   ``ctx.complete()`` or ``ctx.fail()`` from whatever thread finishes the work.

5. **Timeout checker** — ``ConfigReloadProgress`` is a per-reload continuation on ``ET_TASK``
   that polls periodically and marks stuck tasks as ``TIMEOUT``.

Handlers block ``ET_TASK`` while they run. A slow handler delays all subsequent handlers in the
same reload cycle.


Naming Conventions
==================

- **Registry keys** — lowercase, underscore-separated: ``ip_allow``, ``cache_control``,
  ``ssl_ticket_key``, ``ssl_client_coordinator``.
- **Filename records** — follow the existing ``proxy.config.<module>.filename`` convention.
- **Trigger records** — any ``proxy.config.*`` record that should cause a reload when changed.


What NOT to Register
====================

Not every config file needs a **reload handler**. Startup-only configs that are never reloaded at
runtime (e.g. ``storage.config``, ``volume.config``, ``plugin.config``) should be registered via
``register_static_file()`` — this gives them visibility in the registry and RPC endpoints, but
does not wire any reload handler or trigger records. Do not use ``register_config()`` for files
that have no runtime reload support.


Logging Best Practices
======================

- Use ``ctx.log()`` for operational messages that appear in
  ``traffic_ctl config status -l`` and :ref:`get_reload_config_status` responses.
- Use ``ctx.fail(errata, summary)`` when you have a ``swoc::Errata`` with detailed error context.
- Use ``ctx.fail(reason)`` for simple error strings.
- Keep log messages concise — they are stored in memory and included in JSONRPC responses.

See the :ref:`get_reload_config_status` response examples for how log messages appear in the
task tree output.


Testing
========

After registering a new handler:

1. Start |TS| and verify your handler runs at startup (check logs for your config file).
2. Modify the config file on disk and run :option:`traffic_ctl config reload` ``-m`` to observe the
   live progress bar.
3. Run :option:`traffic_ctl config status` to verify the handler appears in the task tree with
   the correct status.
4. Introduce a parse error in the config file and reload — verify the handler reports ``FAIL``.
5. Use :option:`traffic_ctl config status` ``--format json`` to inspect the raw
   :ref:`get_reload_config_status` response for automation testing.

**Autests** — the project includes autest helpers for config reload testing. Use
``AddJsonRPCClientRequest`` with ``Request.admin_config_reload()`` to trigger reloads, and
``Testers.CustomJSONRPCResponse`` to validate responses programmatically. See the existing tests
for examples:

- ``tests/gold_tests/jsonrpc/config_reload_tracking.test.py`` — token generation, status
  queries, history, force reload, duplicate token rejection.
- ``tests/gold_tests/jsonrpc/config_reload_rpc.test.py`` — inline reload, multiple configs,
  ``FileOnly`` rejection, large payloads.
- ``tests/gold_tests/jsonrpc/config_reload_failures.test.py`` — error handling, broken configs,
  handler failure reporting.


.. _reload-framework-records:

Configuration Records
=====================

The reload framework uses the following configuration records:

.. ts:cv:: CONFIG proxy.config.admin.reload.timeout STRING 1h
   :reloadable:

   Maximum time a reload task can run before being marked as ``TIMEOUT``.
   Supports duration strings: ``30s``, ``5min``, ``1h``. Set to ``0`` to disable.
   Default: ``1h``.

.. ts:cv:: CONFIG proxy.config.admin.reload.check_interval STRING 2s
   :reloadable:

   How often the progress checker polls for stuck tasks (minimum: ``1s``).
   Supports duration strings: ``1s``, ``5s``, ``30s``.
   Default: ``2s``.
