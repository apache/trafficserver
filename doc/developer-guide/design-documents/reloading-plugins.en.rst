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

.. _developer-plugins-reloading-plugins:

Reloading Plugins
*****************

Reloading plugin allows new versions of a plugin code to be loaded and executed and old versions to be unloaded without
restarting the |TS| process.

Plugins are Dynamic Shared Objects (DSO), new versions of the plugins are currently loaded by using a |TS|
configuration reload, i.e.::

  traffic_ctl config reload

Although this feature should be transparent to there plugin developers, the following are some design considerations
and implementation details.


Design Considerations
=====================

1. The mechanism of the plugin reload should be transparent to the plugin developers, plugin developers should be
   concerned only with properly initializing and cleaning up after the plugin and its instances.

2. With the current |TS| implementation new version plugin (re)load is only triggered by a configuration
   (re)load hence naturally the configuration should be always coupled with the set of plugins it loaded.

3. Due to its asynchronous nature, |TS| should allow running different newer and older versions of the same plugin at the same time.

4. Old plugin versions should be unloaded after the |TS| process no longer needs them after reload.

5. Running different versions of the configuration and plugin versions at the same time requires maintaining
   a "current active set" to be used by new transactions, new continuations, etc. and also multiple "previous inactive" sets as well.

6. The result of the plugin reloading should be consistent across operating systems, file systems, dynamic loader
   implementations.


Currently only loading of "remap" plugins (`remap.config`) is supported but (re)loading of "global" plugins
(`plugin.config`) could use same ideas and reuse some of the classes below.


Consistent (re)loading behavior
-------------------------------

The following are some of the problems noticed during the initial experimentation:

  a. There is an internal reference counting of the DSOs implemented inside the dynamic loader.
     If an older version of the plugin DSO is still loaded then loading of a newer version of the DSO by using
     the same filename does not load the new version.

  b. If the filename used by the dynamic loader reference counting contains symbolic links the results are not
     consistent across different operating/file systems and dynamic loader implementations.

The following possible solutions were considered:

  a. maintaining different plugin filenames for each version - this would put unnecessary burden on the
     configuration management tools

  b. experiments with Linux specific `dlmopen <http://man7.org/linux/man-pages/man3/dlopen.3.html>`_ yielded
     good results but it was not available on all supported platforms


A less efficient but more reliable solution was chosen - DSO files are temporarily copied to and consequently
loaded from a runtime location and the copies is kept until plugin is unloaded.

Each configuration / plugin reload would use a different runtime location, ``ATSUuid`` is used to create unique
runtime directories.


Reference counting against DSOs
-------------------------------

During the initial analysis a common sense solution was considered - to add instrumentation around handling
of registered hooks in order to unload plugins safely. This would be more involved and not sufficient since hooks
are not the only mechanism that relies on the plugin DSO being loaded. This design / implementation proposes
a different approach.

Plugin code can be called from HTTP state machine (1) while handling HTTP transactions or (2) while calling
event handling functions of continuations created by the plugin code.
The plugin reload mechanism should guarantee that all necessary plugin DSOs are still loaded when those calls
are performed.

Those continuations are created by :c:func:`TSContCreate` and :c:func:`TSVConnCreate` and
could be used for registering hooks (i.e. registered by :c:func:`TSHttpHookAdd`) or for
scheduling events in the future (i.e. :c:func:`TSContScheduleOnPool`).


Registering hooks always requires creating continuations from inside the plugin code and a separate
instrumentation around handling of hooks is not necessary.

There is an existing reference counting around ``UrlRewrite`` which makes sure it stays loaded until the HTTP state
machine (the last HTTP transaction) stops using it. By making all plugins loaded by a single configuration reload
a part of ``UrlRewrite`` (see `PluginFactory`_ below), we could guarantee the plugins are always loaded while
in use by the HTTP transactions.


Plugin context
--------------

Reference counting and managing different configuration and plugin sets require the continuation creation and
destruction to know in which plugin context they are running.

Traffic server API change was considered for ``TSCreateCont``, ``TSVConnCreate`` and ``TSDestroyCont`` but
it was decided to keep things hidden from the plugin developer by using thread local plugin context which
would be set/switched accordingly before executing the plugin code.

The continuations created by the plugin will have a context member added to them which will be used by
the reference counting and when continuations are destroyed or handle events.


TSHttpArgs
----------

|TS| sessions and transactions provide a fixed array of void pointers that can be used by plugins
to store information. To avoid collisions between plugins a plugin should first *reserve* an index in the array.

Since :c:func:`TSHttpTxnArgIndexReserve` and :c:func:`TSHttpSsnArgIndexReserve` are meant to be called during plugin
initialization we could end up "leaking" indices during plugins reload.
Hence it is necessary to make sure only one index is allocated per "plugin identifying name", current
:c:func:`TSHttpTxnArgIndexNameLookup` and :c:func:`TSHttpTxnArgIndexNameLookup` implementation assumes 1-1
index-to-name relationship as well.


PluginFactory
-------------

`PluginFactory` - creates and holds all plugin instances corresponding to a single configuration (re)load.

#. Instantiates and initializes 'remap' plugins, eventually signals plugin unload/destruction, makes sure each plugin
   version is loaded only once per configuration (re)load by maintaining a list of DSOs already loaded.

#. Initializes, keeps track of all resulting plugin instances and eventually signals each instance destruction.

#. Handles multiple plugin search paths.

#. Sets a common runtime path for all plugins loaded in a single configuration (re)load to guarantee
   `consistent (re)loading behavior`_.



RemapPluginInfo
---------------

`RemapPluginInfo` - a class representing a 'remap' plugin, derived from `PluginDso`, and handling 'remap' plugin specific
initialization and destruction and also sets up the right plugin context when its methods are called.



PluginDso
---------

`PluginDso` - a class performing the actual DSO loading and unloading and all related initialization and
cleanup plus related error handling. Its functionality is modularized into a separate class in hopes to
be reused by 'global' plugins in the future.


To make sure plugins are still loaded while their code is still in use there is reference counting done around ``PluginDso``
which inherits ``RefCountObj`` and implements ``acquire()`` and ``release()`` methods which are called by ``TSCreateCont``,
``TSVConnCreate`` and ``TSDestroyCont``.
