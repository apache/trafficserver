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
.. default-domain:: c
.. _developer-plugins-remap:

Remap Plugins
*************

Remap plugins are called during remap (URL rewriting). The particular plugins and the order is
determined by the remap rule. The :file:`remap.config` file can contain explicit references to
remap plugins in a rule and the presence of such a reference in a rule causes the plugin to be
invoked when the rule is matched. For example, a rule such as

.. code-block:: text

   map http://example.one/ http://example.two/ @plugin=example.so @pparam=first_arg @pparm=second_arg

will, if matched, cause the plugin "example.so" to be called with parameters `http://example.one/`,
`http://example.two/`, "first_arg" and "second_arg". Please keep in mind that "from" URL and "to" URL
will be converted to their canonical view.

A key difference between global and remap plugins is reconfiguration and reloading. If
:file:`remap.config` is reloaded, then all remap plugins are reconfigured based on the new version
of the file. Global plugins need to handle their own configuration reloading, if any.

In addition, as of |TS| 9.0, remap plugins can be reloaded during runtime. During a
:file:`remap.config` reload, if the plugin image file has changed, a new one is loaded and used.

All of the externally invoked functions must be declared as :code:`extern "C"` in order to be
correctly located by the Traffic Server core. This is already done if :ts:git:`include/ts/remap.h`
is included, otherwise it must be done explicitly.

Initialization
==============

If any rule uses a plugin, the remap configuration loading will load the dynamic library and then
call :func:`TSRemapInit`. The plugin must return :macro:`TS_SUCCESS` or the configuration loading
will fail. If there is an error during the invocation of this function a C string style message
should be placed in :arg:`errbuff`, taking note of the maximum size of the buffer passed in
:arg:`errbuff_size`. The message is checked if the function returns a value other than
:macro:`TS_SUCCESS`.

If :func:`TSRemapInit` returns :macro:`TS_ERROR` then the remap configuration loading
is aborted immediately.

This function should perform any plugin global initialization, such as setting up static data
tables. It only be called immediately after the dynamic library is loaded from disk.

Configuration
=============

For each plugin invocation in a remap rule, :func:`TSRemapNewInstance` is called.

The parameters :arg:`argc`, :arg:`argv` specify an array of arguments to the invocation instance in
the standard way. :arg:`argc` is the number of arguments present and :arg:`argv` is an array of
pointers, each of which points to a plugin parameter. The number of valid elements is :arg:`argc`.
Note these pointers are valid only for the duration of the function call. If any part of them need
persistent storage, that must be provided by the plugin.

:arg:`ih` is for invocation instance storage data. This initially points at a :code:`nullptr`. If
that pointer is updated the new value will be preserved and passed back to the plugin in later
callbacks. This enables it to serve to identify which particular rule was matched and provide
context data. The standard use is to allocate a class instance, store rule relevant context data in
that instance, and update :arg:`ih` to point at the instance. The most common data is that derived
from the invocation arguments passed in :arg:`argc`, :arg:`argv`.

:arg:`errbuff` and :arg:`errbuff_size` specify a writeable buffer used to report errors. Error
messages must be C strings and must fit in the buffer, including the terminating null.

In essence, :func:`TSRemapNewInstance` is called to create an invocation instance for the plugin to
store rule local data. If the plugin is invoked multiples time on a rule, this will be called
multiple times for the rule, once for each invocation. Only the value store in :arg:`ih` will be
available when the rule is actually matched. In particular the plugin arguments will not be
available.

Calls to :func:`TSRemapNewInstance` are guaranteed to be serialized. All calls to
:func:`TSRemapNewInstance` for a given new configuration are guaranteed to be called and
completed before any calls to :func:`TSRemapDoRemap`.

If there is an error then the callback should return :macro:`TS_ERROR` and fill in the
:arg:`errbuff` with a C-string describing the error. Otherwise the function must return
:macro:`TS_SUCCESS`.

If :func:`TSRemapNewInstance` returns :macro:`TS_ERROR` then the remap configuration loading
is aborted immediately.


Configuration reload notifications
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Most of the plugins is assumed to use per-plugin-instance data-structures when reloading their
configs and only a few of them that wish to optimize performance or deal with the complexities
of using a per-plugin DSO "global" data-structures would use plugin configuration reload
notifications like :func:`TSRemapPreConfigReload` and :func:`TSRemapPostConfigReload`.

Instead of trying to foresee the needs or the expectations of each use-case, a more "open-ended"
and straight-forward design was chosen for the configuration reload notifications.
The notifications are broadcast to all loaded plugins at the moments before and after
the reload attempt, regardless of whether a plugin is part of the new configuration or not.

:func:`TSRemapPreConfigReload` is called *before* the parsing of a new remap configuration starts
to notify plugins of the coming configuration reload. It is called on all already loaded plugins,
invoked by current and all previous still used configurations. This is an optional entry point.

:func:`TSRemapPostConfigReload` is called to indicate the end of the new remap configuration
load. It is called on the newly and previously loaded plugins, invoked by the new, current and
previous still used configurations. It also indicates whether the configuration reload was successful
by passing :macro:`TSREMAP_CONFIG_RELOAD_FAILURE` in case of failure and to notify the plugins if they
are going to be part of the new configuration by passing :macro:`TSREMAP_CONFIG_RELOAD_SUCCESS_PLUGIN_USED`
or :macro:`TSREMAP_CONFIG_RELOAD_SUCCESS_PLUGIN_UNUSED`. This is an optional entry point.

These calls are called per *plugin*, not per invocation of the plugin in :file:`remap.config`
and only will be called if the plugin was instantiated by at least one configuration loaded
after |TS| started and at least one configuration using it is still loaded.

:func:`TSRemapPreConfigReload` will be called serially for all loaded plugins
before any call to :func:`TSRemapNewInstance` during parsing of the new configuration.

:func:`TSRemapPostConfigReload` will be called serially for all plugins after
all calls to :func:`TSRemapNewInstance` during parsing of the new configuration.

The intention of these callbacks can be demonstrated with the following use-case.
A plugin could use :func:`TSRemapPreConfigReload` as a signal to drop (or allocate) temporary
per plugin data structures. These structures can be created (or updated) as needed
when a plugin invocation instance is loaded (:func:`TSRemapNewInstance` is called).
Then it could be used in subsequent invocation instances loading. After the configuration
reload is done :func:`TSRemapPostConfigReload` could be used to confirm the data
structures update if reload was successful, recover / clean-up after a failed
reload attempt, or if so wishes to ignore the notification if plugin is not part
of the new configuration..


Runtime
=======

At runtime, if a remap rule is matched, the plugin is invoked by calling :func:`TSRemapDoRemap`.
This function is responsible for performing the plugin operation for the transaction.

:arg:`ih` is the same value set in :func:`TSRemapNewInstance` for the invocation instance. This is
not examined or checked by the core. :arg:`rh` is the transaction for which the rule matched.
:arg:`rri` is information about the rule and the transaction.

The callback is required to return a :type:`TSRemapStatus` indicating whether it performed a remap.
This is used for verifying a request was remapped if remapping is required. This can also be used
to prevent further remapping, although this should be used with caution.

Calls to :func:`TSRemapDoRemap` are not serialized, they can be concurrent, even for the same
invocation instance. However, the callbacks for a single rule for a single transaction are
serialized in the order the plugins are invoked in the rule.

No calls to :func:`TSRemapDoRemap` will occur before :func:`TSRemapPostConfigReload` for
all plugin instances invoked by the new configuration.

The old configurations, if any, are still active during the calls to :func:`TSRemapPreConfigReload`
and :func:`TSRemapPreConfigReload` and therefore calls to :func:`TSRemapDoRemap` may occur
concurrently with those functions.


Cleanup
=======

When a new :file:`remap.config` is loaded successfully, the prior configuration is cleaned up. For
each call to :func:`TSRemapNewInstance` a corresponding call to :func:`TSRemapDeleteInstance` is
called. The only argument is the invocation instance handle, originally provided by the plugin to
:func:`TSRemapNewInstance`. This is expected to suffice for the plugin to clean up any rule specific
data. Calls to :func:`TSRemapDeleteInstance` will be serial for all plugin invocations in a
remap configuration.

If no rule uses a plugin, it may be unloaded. In that case :func:`TSRemapDone` is called. This is
an optional entry point, a plugin is not required to provide it. It corresponds to :func:`TSRemapInit`.
It is called once per plugin, not per plugin invocation.
