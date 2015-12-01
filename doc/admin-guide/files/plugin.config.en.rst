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

plugin.config
*************

.. configfile:: plugin.config

Description
===========

The :file:`plugin.config` file controls run-time loadable plugins available to
|TS|, as well as their configuration.  Plugins listed in this file are referred
to as *global plugins* because they are always loaded and have global effect.
This is in contrast to plugins specified in :file:`remap.config`, whose effects
are limited to the specific mapping rules to which they are applied.

Each configuration line consists of a path to an ``.so`` file. This path can
either be absolute, or relative to the plugin directory (usually
``/usr/local/libexec/trafficserver``).  Failure to load a plugin is fatal, and
will cause |TS| to abort. In general, it is not possible to know whether it is
safe for the service to run without a particular plugin, since plugins can have
arbitrary effects on caching and authorization policies.

.. important::

   Plugins should only be listed once. The order in which the plugins
   are listed is also the order in which they are chained for request
   processing.

An option list of whitespace-separated arguments may follow the plugin name.
These are passed as an argument vector to the plugin's initialization function,
:c:func:`TSPluginInit`. Arguments that begin with the ``$`` character designate
|TS| configuration variables. These arguments will be replaced with the value
of the corresponding configuration variable before the plugin is loaded.  When
using configuration variable expansion, note that most |TS| configuration can
be changed. If a plugin requires the current value, it must obtain that using
the management API.

Examples
========

::

     # Comments start with a '#' and continue to the end of the line
     # Blank lines are ignored
     #
     # test-plugin.so arg1 arg2 arg3
     #
     plugins/iwx/iwx.so
     plugins/abuse/abuse.so etc/trafficserver/abuse.config
     plugins/icx/icx.so etc/trafficserver/icx.config $proxy.config.http.connect_attempts_timeout

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSPluginInit(3ts)`,
:manpage:`remap.config(5)`
