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

===========
plugin.yaml
===========

.. configfile:: plugin.yaml

The :file:`plugin.yaml` file provides a YAML-based alternative to
:file:`plugin.config` for configuring global plugins available to |TS|.
Global plugins are loaded at startup and have global effect on all
transactions. This is in contrast to plugins specified in
:file:`remap.config` or :file:`remap.yaml`, whose effects are limited to
specific mapping rules.

Configuration File Fallback
============================

|TS| will attempt to load :file:`plugin.yaml` first. If this file is
not found, it will fall back to loading :file:`plugin.config`. If both
files exist, only :file:`plugin.yaml` will be used. This allows for a
gradual migration from the legacy configuration format to YAML.

Format
======

The :file:`plugin.yaml` file uses YAML syntax with a single required key:

- ``plugins`` (required): A sequence of plugin entries.

Each plugin entry is a YAML mapping with the following fields:

``path``
--------

**Required.** Path to the ``.so`` file. This path can be absolute or
relative to the plugin directory (usually
``/usr/local/libexec/trafficserver``).

``enabled``
-----------

**Optional.** Boolean. When set to ``false``, the plugin is skipped
entirely during startup — no ``dlopen``, no ``TSPluginInit``. The
configuration entry remains in the file for easy re-enabling.

**Default:** ``true``

``params``
----------

**Optional.** A YAML sequence of string arguments passed to the plugin's
``TSPluginInit`` function as ``argc/argv``. Arguments that begin
with ``$`` designate |TS| configuration variables and will be expanded
to their current value before the plugin is loaded.

``load_order``
--------------

**Optional.** Integer. Provides explicit control over the order in which
plugins are loaded and therefore the order in which they are chained for
request processing.

The loading rules are:

1. Plugins **with** ``load_order`` are loaded first, sorted ascending by
   value (lowest number loads first).
2. Among plugins with the **same** ``load_order`` value, their relative
   order in the YAML file is preserved (stable sort).
3. Plugins **without** ``load_order`` are loaded after all ordered
   plugins, in the order they appear in the YAML file.

Most deployments do not need ``load_order`` — simply list plugins in the
desired order in the YAML file. Use ``load_order`` when the file is
managed by automation tools that may reorder entries, or when you want
to guarantee a specific plugin loads first regardless of where it
appears in the file.

**Default:** Unset (YAML sequence order).

Basic Structure
===============

.. code-block:: yaml

   plugins:
     - path: stats_over_http.so

     - path: abuse.so
       params:
         - etc/trafficserver/abuse.config

     - path: header_rewrite.so
       params:
         - etc/trafficserver/header_rewrite.config

     - path: icx.so
       params:
         - etc/trafficserver/icx.config
         - $proxy.config.http.connect_attempts_timeout

     - path: experimental_plugin.so
       enabled: false
       params:
         - --verbose

.. important::

   **Loading order matters.** Plugins are loaded in the order they
   appear in the YAML file, and this is the order in which they are
   chained for request processing (hooks are called in load order). If
   you need a plugin to run before another, place it earlier in the file
   or assign it a lower ``load_order`` value.

New Features Over plugin.config
================================

:file:`plugin.yaml` introduces several features not available in the
legacy :file:`plugin.config` format:

*  **Disable without deleting** — set ``enabled: false`` to skip a
   plugin without removing or commenting out the line.
*  **Explicit load ordering** — use ``load_order`` to control loading
   priority independent of file position.
*  **Variable expansion** — ``$record`` references in ``params`` are
   expanded to their current value at load time (same as
   :file:`plugin.config`).
*  **Startup logging** — each plugin produces a ``NOTE``-level log line
   showing its load sequence number, path, and status.
*  **Runtime introspection** — ``traffic_ctl plugin list`` shows the
   loaded plugins and their status via JSONRPC.
*  **Automated migration** — ``traffic_ctl config convert plugin_config``
   converts an existing :file:`plugin.config` to :file:`plugin.yaml`.

Examples
========

Disabling a Plugin
------------------

.. code-block:: yaml

   plugins:
     - path: debug_plugin.so
       enabled: false

The plugin entry remains in the configuration file but is not loaded.
Set ``enabled: true`` (or remove the field) to re-enable it.

Plugin Loading Order
---------------------

By default, plugins load in the order they appear in the YAML file —
top to bottom. This is the same behavior as :file:`plugin.config` and
is sufficient for most deployments:

.. code-block:: yaml

   plugins:
     - path: certifier.so           # loads 1st
     - path: header_rewrite.so      # loads 2nd
     - path: stats_over_http.so     # loads 3rd

When ``load_order`` is set, it overrides the file order. Plugins with
``load_order`` always load before plugins without it:

.. code-block:: yaml

   plugins:
     - path: stats_over_http.so
       load_order: 300

     - path: certifier.so
       load_order: 100

     - path: header_rewrite.so
       load_order: 200

     - path: xdebug.so

Despite the YAML sequence order, the actual load order is:

1. ``certifier.so`` (load_order: 100)
2. ``header_rewrite.so`` (load_order: 200)
3. ``stats_over_http.so`` (load_order: 300)
4. ``xdebug.so`` (no load_order — loaded last, in file order)

.. tip::

   Use gaps between ``load_order`` values (e.g. 100, 200, 300) so new
   plugins can be inserted later without renumbering.

Configuration Variable Expansion
---------------------------------

.. code-block:: yaml

   plugins:
     - path: icx.so
       params:
         - etc/trafficserver/icx.config
         - $proxy.config.http.connect_attempts_timeout

Arguments beginning with ``$`` are expanded to the current value of the
corresponding |TS| configuration variable before the plugin is loaded.

Migration from plugin.config
==============================

.. list-table::
   :header-rows: 1
   :widths: 50 50

   * - plugin.config
     - plugin.yaml
   * - ::

         stats_over_http.so
     - .. code-block:: yaml

         plugins:
           - path: stats_over_http.so

   * - ::

         abuse.so etc/trafficserver/abuse.config
     - .. code-block:: yaml

         plugins:
           - path: abuse.so
             params:
               - etc/trafficserver/abuse.config

   * - ::

         icx.so etc/trafficserver/icx.config $proxy.config.http.connect_attempts_timeout
     - .. code-block:: yaml

         plugins:
           - path: icx.so
             params:
               - etc/trafficserver/icx.config
               - $proxy.config.http.connect_attempts_timeout

   * - ::

         # header_rewrite.so etc/trafficserver/header_rewrite.config
     - .. code-block:: yaml

         plugins:
           - path: header_rewrite.so
             params:
               - etc/trafficserver/header_rewrite.config
             enabled: false

Startup Logging
===============

When plugins are loaded from :file:`plugin.yaml`, each plugin produces a
``NOTE``-level log line showing its load sequence number, path, and status:

::

   [NOTE] plugin #1 loading: certifier.so (load_order: 100)
   [NOTE] plugin #2 loading: header_rewrite.so
   [NOTE] plugin #3 skipped: experimental_plugin.so (enabled: false)

See Also
========

:doc:`plugin.config.en`,
:manpage:`TSAPI(3ts)`,
:manpage:`TSPluginInit(3ts)`,
:doc:`remap.config.en`,
:doc:`remap.yaml.en`
