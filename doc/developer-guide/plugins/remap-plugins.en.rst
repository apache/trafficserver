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

will, if matched, cause the plugin "example.so" to be called with parameters "first_arg" and "second_arg".

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

Calls to :func:`TSRemapNewInstance` are serialized.

If there is an error then the callback should return :macro:`TS_ERROR` and fill in the
:arg:`errbuff` with a C-string describing the error. Otherwise the function must return
:macro:`TS_SUCCESS`.

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

All calls to :func:`TSRemapNewInstance` for a given configuration will be called and completed
before any calls to :func:`TSRemapDoRemap`.

Cleanup
=======

When a new :file:`remap.config` is loaded successfully, the prior configuration is cleaned up. For
each call to :func:`TSRemapNewInstance` a corresponding call to :func:`TSRemapDeleteInstance` is
called. The only argument is the invocation instance handle, originally provided by the plugin to
:func:`TSRemapNewInstance`. This is expected to suffice for the plugin to clean up any rule specific
data.

As part of the old configuration cleanup :func:`TSRemapConfigReload` is called on the plugins in the
old configuration before any calls to :func:`TSRemapDeleteInstance`. This is an optional entry
point.

.. note::

   This is called per *plugin*, not per invocation of the plugin in :file:`remap.config`, and only
   called if the plugin was called at least once with :func:`TSRemapNewInstance` for that
   configuration.

.. note::

   There is no explicit indication or connection between the call to :func:`TSRemapConfigReload` and
   the "old" (existing) configuration. It is guaranteeed that :func:`TSRemapConfigReload` will be
   called on all the plugins before any :func:`TSRemapDeleteInstance` and these calls will be
   serial. Similarly, :func:`TSRemapConfigReload` will be called serially after all calls to
   :func:`TSRemapNewInstance` for a given configuration.

.. note::

   The old configuration, if any, is still active during the call to :func:`TSRemapConfigReload` and
   therefore calls to :func:`TSRemapDoRemap` may occur concurrently with that function.

The intention of :func:`TSRemapConfigReload` is to provide for temporary data structures used only
during configuration loading. These can be created as needed when an invocation instance is loaded
and used in subsequent invocation instance loading, then cleaned up in :func:`TSRemapConfigReload`.

If no rule uses a plugin, it may be unloaded. In that case :func:`TSRemapDone` is called. This is
an optional entry point, a plugin is not required to provide it. It is called once per plugin, not
per plugin invocation.
