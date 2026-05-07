.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: cpp

TSCfgRegister
*************

Register a plugin's configuration file with the |TS| reload framework
and drive the per-reload context object delivered to the handler.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. type:: TSCfgSourceType

   What content sources a plugin's config handler supports.

   .. var:: TS_CFG_SOURCE_FILE_ONLY

      Handler reloads only from a file on disk. RPC-supplied YAML is
      rejected.

   .. var:: TS_CFG_SOURCE_FILE_AND_RPC

      Handler can also process YAML content supplied via RPC.

.. type:: TSCfgRegistrationInfo

   Options struct passed to :func:`TSCfgRegister`. Fields are set with
   C++20 designated initializers; new fields may be appended in future
   |TS| versions without breaking source compatibility for existing
   plugins.

   .. var:: std::string_view key

      Unique registry key. Doubles as the YAML node name when an operator
      reloads via ``traffic_ctl config reload --content '{<key>: {...}}'``.
      Convention: use the plugin name, or ``<plugin>.<sub>`` when a single
      plugin owns multiple configs. Required.

   .. var:: std::string_view config_path

      Default config file path (absolute, or relative to ``sysconfdir``).
      Required.

   .. var:: std::string_view filename_record

      Fully-qualified record name that holds the active config filename,
      or ``{}`` (default) if the path is fixed. When set, the operator
      can change the active config file at runtime by editing this
      record; ``config_path`` is used as the fallback when the record is
      empty or unset. Optional.

   .. var:: TSCfgLoadCb handler

      Reload callback invoked when the file changes, when an attached
      trigger record changes, or when the operator triggers an RPC
      reload. Required.

   .. var:: void *data

      Opaque pointer passed unmodified to ``handler`` on every invocation.
      The plugin owns it; ATS does not copy or free it. Must outlive the
      registration - typically a ``static`` or once-allocated heap object
      stashed in ``TSPluginInit``::

          static PluginState state;
          state.config_path = ...;

          TSCfgRegistrationInfo info{};
          info.handler = config_reload;
          info.data    = &state;
          TSCfgRegister(&info);

   .. var:: TSCfgSourceType source

      Whether ``handler`` can also process YAML content supplied via RPC.
      Defaults to :var:`TS_CFG_SOURCE_FILE_ONLY`.

   .. var:: bool is_required

      Hint propagated to FileManager: marks the file as "required" in
      catalog/inspection output and emits a ``Dbg`` line if the file
      is missing at registration time. The framework does **not**
      enforce this flag at reload-time today: the handler is still
      invoked even when the file is missing or invalid, and the
      plugin chooses whether to fail the reload. Defaults to ``false``.

.. type:: TSCfgFileDependencyInfo

   Options struct passed to :func:`TSCfgAddFileDependency`. Only ``key``
   and ``config_path`` are required.

   .. var:: std::string_view key

      Parent registry key as passed to :func:`TSCfgRegister`. Required.

   .. var:: std::string_view config_path

      Default companion file path (absolute, or relative to ``sysconfdir``).
      Required.

   .. var:: std::string_view filename_record

      Fully-qualified record name that holds the filename, or ``{}`` if
      the path is fixed. Optional.

   .. var:: std::string_view dep_key

      Routing key for inline YAML supplied via JSONRPC. When non-empty,
      content delivered under this top-level node is routed to the parent
      entry's handler, giving the plugin parity with core
      composite-config patterns. When empty (default), the dependency is
      file-change-only.

   .. var:: bool is_required

      Hint propagated to FileManager (catalog/inspection only); see the
      equivalent field on :type:`TSCfgRegistrationInfo`. Defaults to
      ``false``.

.. type:: TSCfgLoadCtx

   Opaque handle to a per-reload context. The context tracks the
   lifecycle of a single reload attempt for one registered config: it
   carries the reload token, supplied YAML (for RPC reloads), the
   filename (for file reloads), and the task state machine the framework
   uses to aggregate results.

.. type:: void (*TSCfgLoadCb)(TSCfgLoadCtx ctx, void *data)

   Plugin reload callback signature. ``data`` is the opaque pointer the
   plugin supplied via :var:`TSCfgRegistrationInfo::data`.

.. enum:: TSCfgLogLevel

   Severity for log entries emitted via :func:`TSCfgLoadCtxAddLog`.

   .. enumerator:: TS_CFG_LOG_NOTE

      Informational. Default level. Maps to ``DL_Note`` internally.

   .. enumerator:: TS_CFG_LOG_WARNING

      Concerning; the reload may still complete. Maps to ``DL_Warning``.

   .. enumerator:: TS_CFG_LOG_ERROR

      Failure cause; pair with a subsequent :func:`TSCfgLoadCtxFail`.
      Maps to ``DL_Error``.

   The framework deliberately does not expose the fatal-class diagnostics
   levels here: a reload handler should not be able to terminate the
   process. Plugins that truly need debug-only output should use
   ``Dbg(ctl, ...)`` instead of this API.

.. function:: TSReturnCode TSCfgRegister(const TSCfgRegistrationInfo *info)
.. function:: TSReturnCode TSCfgAttachReloadTrigger(std::string_view key, std::string_view record_name)
.. function:: TSReturnCode TSCfgAddFileDependency(const TSCfgFileDependencyInfo *info)
.. function:: void TSCfgLoadCtxInProgress(TSCfgLoadCtx ctx, std::string_view msg)
.. function:: void TSCfgLoadCtxComplete(TSCfgLoadCtx ctx, std::string_view msg)
.. function:: void TSCfgLoadCtxFail(TSCfgLoadCtx ctx, std::string_view msg)
.. function:: void TSCfgLoadCtxAddLog(TSCfgLoadCtx ctx, TSCfgLogLevel level, std::string_view msg)
.. function:: TSCfgLoadCtx TSCfgLoadCtxAddSubtask(TSCfgLoadCtx ctx, std::string_view description)
.. function:: std::string_view TSCfgLoadCtxGetFilename(TSCfgLoadCtx ctx)
.. function:: std::string_view TSCfgLoadCtxGetReloadToken(TSCfgLoadCtx ctx)
.. function:: TSYaml TSCfgLoadCtxGetSuppliedYaml(TSCfgLoadCtx ctx)
.. function:: TSYaml TSCfgLoadCtxGetReloadDirectives(TSCfgLoadCtx ctx)

Description
===========

These functions are the plugin-facing entry points to the |TS|
configuration reload framework. A plugin registers a config file
together with a reload callback, and the framework drives the callback
whenever the file changes, an attached trigger record changes, or an
operator triggers a reload via JSONRPC.

The plugin name (set via :func:`TSPluginRegister`) is automatically
attached to every registered entry, so reload-trace logs and
:option:`traffic_ctl config status` output identify which plugin owns
which entry as ``[plugin: <name>]``. Plugins do not pass their name
explicitly.

Registration
------------

:func:`TSCfgRegister`
   Registers a config file and reload handler.

   Must be called from :func:`TSPluginInit`, **after**
   :func:`TSPluginRegister`. Returns ``TS_ERROR`` if called outside
   ``TSPluginInit``, before ``TSPluginRegister``, with a null or
   incomplete ``info`` struct, or if another plugin (or core) has
   already registered the same key.

:func:`TSCfgAttachReloadTrigger`
   Wires a record so that changing its value re-runs the reload handler
   registered for ``key``. Internally registers a record-change
   callback (``RecRegisterConfigUpdateCb``) and routes the event back
   into the same reload pipeline as file changes: the plugin's
   :type:`TSCfgLoadCb` is invoked with the resolved file path
   available via :func:`TSCfgLoadCtxGetFilename`, exactly as it would
   be for an on-disk file change. May be called multiple times to
   attach more than one trigger record to the same entry.

   **Core analog.** :cpp:func:`!ConfigRegistry::register_config()`
   accepts a ``trigger_records`` initializer-list at registration time
   (e.g. ``ssl_multicert`` lists ~10 record names there). The
   post-registration form is :cpp:func:`!ConfigRegistry::attach`. This
   function is the plugin-facing wrapper for the latter; the
   initializer-list shape is intentionally not exposed on
   :type:`TSCfgRegistrationInfo` so the option struct stays
   ABI-stable.

   **It triggers a reload, nothing else.** Specifically the plugin
   cannot:

   - Register a free-form record-change callback. There is no
     ``TSRecordRegisterChangeCb`` today; the underlying primitive
     (``RecRegisterConfigUpdateCb``) is internal-only.
   - Receive record-change details. The handler gets no record name,
     no old/new value, no event payload. Use ``TSMgmt*Get()`` inside
     the handler if it needs the value.
   - Subscribe a record without a registered config key. Calling with
     an unknown ``key`` returns ``TS_ERROR``.
   - Multiplex one record across two keys, or attach in any shape
     other than (one record, one config key, per call).

   The reload is always treated as file-driven (no RPC payload):
   :func:`TSCfgLoadCtxGetSuppliedYaml` and
   :func:`TSCfgLoadCtxGetReloadDirectives` return ``nullptr`` for
   record-triggered invocations. Standalone record changes (i.e.
   ``traffic_ctl config set`` outside an active reload cycle) invoke
   the handler with an empty context that does not surface in
   :option:`traffic_ctl config status` - same as core record-triggered
   reloads outside a reload cycle.

:func:`TSCfgAddFileDependency`
   Adds a companion file to a previously registered key. When the
   companion file changes on disk - or when an operator submits inline
   YAML under :var:`TSCfgFileDependencyInfo::dep_key` via JSONRPC, when
   set - the plugin's handler is invoked. With ``dep_key`` empty the
   dependency is file-change-only.

Per-reload context (TSCfgLoadCtx)
---------------------------------

A :type:`TSCfgLoadCtx` is created by the framework before invoking the
plugin's :type:`TSCfgLoadCb`. The handler must drive the context to a
**terminal state** - either :func:`TSCfgLoadCtxComplete` or
:func:`TSCfgLoadCtxFail` - so the reload tree can finish. See
:ref:`config-context-terminal-state` for the complete contract.

State transitions:

:func:`TSCfgLoadCtxInProgress`
   Set the task to ``IN_PROGRESS`` and emit an optional progress
   message. Calling it more than once on the same context is
   discouraged - use :func:`TSCfgLoadCtxAddLog` for periodic log
   output. Pass ``{}`` for ``msg`` if no message is needed.

:func:`TSCfgLoadCtxComplete`
   Mark the task as successfully completed. Pass ``{}`` for ``msg`` if
   no message is needed. After this call the framework deletes the
   context handle - **do not access** ``ctx`` after a successful
   Complete.

:func:`TSCfgLoadCtxFail`
   Mark the task as failed. Pass ``{}`` for ``msg`` if no message is
   needed. After this call the framework deletes the context handle -
   **do not access** ``ctx`` after a Fail.

:func:`TSCfgLoadCtxAddLog`
   Append a log entry at ``level`` to the task. Visible in
   :option:`traffic_ctl config status` output and in the
   :ref:`get_reload_config_status` JSONRPC response. Does not change the
   task state.

:func:`TSCfgLoadCtxAddSubtask`
   Create a child subtask. Returns a new context that the plugin must
   independently drive to a terminal state. Use this when a single
   config reload spawns several distinct units of work that should be
   tracked separately. The parent's status aggregates from its
   children: any child failing causes the parent to be marked failed.

   The subtask is born in ``CREATED`` and follows the same state
   machine as the parent. Two transition patterns are valid:

   - ``CREATED -> IN_PROGRESS -> SUCCESS / FAILED`` (recommended for
     substantive work) - call :func:`TSCfgLoadCtxInProgress` once you
     start, then :func:`TSCfgLoadCtxComplete` /
     :func:`TSCfgLoadCtxFail` when done. The "in progress" phase is
     visible in :option:`traffic_ctl config status`.
   - ``CREATED -> SUCCESS / FAILED`` (shortcut for trivial subtasks) -
     call :func:`TSCfgLoadCtxComplete` / :func:`TSCfgLoadCtxFail`
     directly on the returned handle.

Inputs:

:func:`TSCfgLoadCtxGetFilename`
   Returns the path the framework expects this handler to read.
   Two-step resolution:

   1. If the registration's ``filename_record`` was set AND the record
      currently has a non-empty value, that value is returned (the
      operator can override the filename at runtime via
      ``traffic_ctl config set <record>``).
   2. Otherwise, returns ``config_path`` as-registered.

   Most plugins don't set ``filename_record`` and could equivalently
   use their own stashed copy of the registered path - this function
   is the canonical way to get the filename only when
   ``filename_record`` is in play.

   **Always populated for plugin handlers**, including on RPC reloads.
   To detect RPC content, check :func:`TSCfgLoadCtxGetSuppliedYaml` -
   not this function.

   **Core analog.** :cpp:func:`!ConfigReloadTask::get_filename`. For
   core handlers, the value is empty on RPC reloads; the plugin
   wrapper always populates it so plugins can use
   :func:`TSCfgLoadCtxGetSuppliedYaml` as the canonical RPC-detection
   signal.

:func:`TSCfgLoadCtxGetReloadToken`
   Returns the reload-cycle correlation token (e.g.
   ``rldtk-<timestamp>``). Useful for plugin-side log lines that need
   to match the operator's
   ``traffic_ctl config status -t <token>`` view.

:func:`TSCfgLoadCtxGetSuppliedYaml`
   Returns the YAML content supplied via the JSONRPC reload payload as
   an opaque :type:`TSYaml` handle (a ``YAML::Node *``). The
   framework-reserved ``_reload`` directives are stripped before
   delivery. The returned node is undefined when the reload was driven
   by file change rather than RPC. Only meaningful when the entry was
   registered with :var:`TS_CFG_SOURCE_FILE_AND_RPC`.

:func:`TSCfgLoadCtxGetReloadDirectives`
   Returns the YAML map extracted from the ``_reload`` key in the
   RPC-supplied content as an opaque :type:`TSYaml` handle. Used by
   handlers that need scoped reload behavior (for example, reload only
   one entry, dry-run mode, or a version constraint). See
   :ref:`config-reload-framework` for the directive convention.

The two ``std::string_view`` accessors return views whose backing
storage is owned by the framework and is valid for the lifetime of the
handler call. Copy the data if you need to retain it past the callback
return. The :type:`TSYaml` accessors return handles whose lifetime is
also tied to the handler call.

Lifecycle and threading
-----------------------

The handler runs on an ``ET_TASK`` thread. It does **not** have to
complete synchronously: the plugin may stash ``ctx`` in its own state,
return from the callback, finish work on a different thread, and then
call :func:`TSCfgLoadCtxComplete` or :func:`TSCfgLoadCtxFail` from
there. This is the standard pattern for plugins that perform
asynchronous I/O during reload.

If the handler never reaches a terminal state, the per-reload progress
checker eventually marks the task as ``TIMEOUT`` and unblocks the
overall reload (see :ref:`config-context-terminal-state`). The context
handle then leaks for the process lifetime, so reaching a terminal
state on every code path is mandatory.

Recommended pattern: parse first, apply second
----------------------------------------------

The framework does not provide a separate validation phase. The
handler is the single point where the plugin parses the new
configuration and applies it. To avoid leaving live state half-mutated
on a partial parse, plugins should follow a two-step pattern inside
the same handler:

1. **Parse** the file (or supplied YAML) into a fresh, staging-side
   structure. Validate fully. Do not touch live state during this
   step. On parse failure, call :func:`TSCfgLoadCtxFail` and return -
   live state remains untouched.

2. **Swap** the staging structure into place atomically. This is
   typically a pointer swap into a ``std::shared_ptr`` /
   ``std::atomic`` slot the request path reads from. After the swap
   succeeds, call :func:`TSCfgLoadCtxComplete`.

This pattern matches what core configs already do (for example,
``ip_allow``, ``remap.config``) and gives operators predictable
behavior on a malformed reload: the previous configuration stays in
effect until a fully valid one is loaded.

Restrictions
============

- Not supported for remap plugins (:func:`TSRemapInit` /
  :func:`TSRemapNewInstance`).
- :func:`TSCfgRegister`, :func:`TSCfgAttachReloadTrigger`, and
  :func:`TSCfgAddFileDependency` must all be called from
  :func:`TSPluginInit`, after :func:`TSPluginRegister`.

Example - registration and synchronous handler
==============================================

.. code-block:: cpp

   #include <ts/ts.h>
   #include <string>

   namespace
   {
   constexpr char PLUGIN_NAME[] = "my_plugin";

   struct PluginState {
     std::string config_path;
   };

   void
   config_reload(TSCfgLoadCtx ctx, void *data)
   {
     auto *state = static_cast<PluginState *>(data);

     std::string_view filename = TSCfgLoadCtxGetFilename(ctx);
     if (filename.empty()) {
       TSCfgLoadCtxFail(ctx, "no filename available");
       return;
     }

     if (!parse_and_apply(state, std::string{filename})) {
       TSCfgLoadCtxFail(ctx, "parse failed");
       return;
     }

     TSCfgLoadCtxComplete(ctx, "Reloaded my_plugin");
   }
   } // anonymous namespace

   void
   TSPluginInit(int /* argc */, const char * /* argv */[])
   {
     TSPluginRegistrationInfo plugin{};
     plugin.plugin_name   = PLUGIN_NAME;
     plugin.vendor_name   = "Example Inc.";
     plugin.support_email = "support@example.com";

     if (TSPluginRegister(&plugin) != TS_SUCCESS) {
       TSError("[%s] plugin registration failed", PLUGIN_NAME);
       return;
     }

     static PluginState state;
     state.config_path = std::string{TSConfigDirGet()} + "/my_plugin.yaml";

     TSCfgRegistrationInfo info{};
     info.key         = PLUGIN_NAME;
     info.config_path = state.config_path;
     info.handler     = config_reload;
     info.data        = &state;
     info.source      = TS_CFG_SOURCE_FILE_AND_RPC;
     info.is_required = false;
     if (TSCfgRegister(&info) != TS_SUCCESS) {
       TSError("[%s] TSCfgRegister failed for '%s'", PLUGIN_NAME,
               state.config_path.c_str());
       return;
     }

     // Optional: attach a record so changing it fires the handler.
     TSCfgAttachReloadTrigger(PLUGIN_NAME, "proxy.config.my_plugin.enabled");
   }

Example - RPC content with directives
=====================================

.. code-block:: cpp

   #include <yaml-cpp/yaml.h>

   void
   config_reload(TSCfgLoadCtx ctx, void * /* data */)
   {
     auto *yaml = static_cast<YAML::Node *>(TSCfgLoadCtxGetSuppliedYaml(ctx));
     auto *dirs = static_cast<YAML::Node *>(TSCfgLoadCtxGetReloadDirectives(ctx));

     if (dirs && dirs->IsDefined()) {
       // operator passed --directive ...
     }
     if (yaml && yaml->IsDefined()) {
       // RPC-supplied content; do not read the file.
     } else {
       // file-driven reload; read TSCfgLoadCtxGetFilename(ctx).
     }
     TSCfgLoadCtxComplete(ctx, {});
   }

Example - deferred completion
=============================

A plugin that schedules background work returns from the callback
without calling Complete or Fail; it stashes ``ctx`` and finishes from
another thread:

.. code-block:: cpp

   struct Work { TSCfgLoadCtx ctx; PluginState *state; };

   void
   config_reload(TSCfgLoadCtx ctx, void *data)
   {
     auto *work = new Work{ctx, static_cast<PluginState *>(data)};
     // schedule heavy_work(work) on a worker thread; it will eventually
     // call TSCfgLoadCtxComplete(work->ctx, {}) (or Fail) and delete work.
     schedule_async(work);
   }

A runnable example of the deferred pattern lives in
``tests/gold_tests/jsonrpc/plugins/cfg_plugin_deferred_test.cc``.

See Also
========

:ref:`config-reload-framework` is the complete framework guide,
covering the registry, terminal-state rule, RPC reload payload format,
``[plugin: <name>]`` attribution in :option:`traffic_ctl config status`,
diagnostic macros, and ``diags.log`` summaries.
