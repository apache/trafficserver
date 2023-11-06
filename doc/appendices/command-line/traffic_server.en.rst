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

.. _traffic_server:

traffic_server
**************

Description
===========

Options
=======

.. program:: traffic_server

.. option:: -a, --accepts_thread

.. option:: -B TAGS, --action_tags TAGS

.. option:: --bind_stdout FILE

The file to which the stdout stream for |TS| will be bound.

.. option:: --bind_stderr FILE

The file to which the stderr stream for |TS| will be bound.

.. option:: -C 'CMD [ARGS]', --command 'CMD [ARGS]'

Run a |TS| maintenance command. These commands perform various administrative
actions or queries against |TS|. Note that some commands (such as ``help`` and
``verify_global_plugin``) take an argument. To invoke the command and its
argument, surround the ``CMD`` and its argument in quotes. For instance, to
request help for the ``verify_global_plugin`` command, format your command like
so::

    traffic_server -C "help verify_global_plugin"

The following commands are supported:

list
   List the sizes of the host database and cache index as well as the storage
   available to the cache.
check
   Check the cache for inconsistencies or corruption. ``check`` does not make
   any changes to the data stored in the cache. ``check`` requires a scan of
   the contents of the cache and may take a long time for large caches.
clear
   Clear the entire cache, both the document and the host database caches.  All
   data in the cache is lost and the cache is reconfigured based on the current
   description of database sizes and available storage.
clear_cache
   Clear the document cache.  All documents in the cache are lost and the cache
   is reconfigured based on the current description of database sizes and
   available storage.
clear_hostdb
   Clear the entire host database cache.  All host name resolution information
   is lost.
verify_config
   Load the config and verify |TS| comes up correctly.
verify_global_plugin PLUGIN_SO_FILE
   Load a global plugin's shared object file and verify it meets minimal global
   plugin API requirements.
verify_remap_plugin PLUGIN_SO_FILE
   Load a remap plugin's shared object file and verify it meets minimal remap
   plugin API requirements.
help [CMD]
    Obtain a short description of a command. For example, ``'help clear'``
    prints a description of the ``clear`` maintenance command. If no argument
    is passed to ``help`` then a list of the supported maintenance commands are
    printed along with a brief description of each.

.. option:: -f, --disable_freelist

In order to improve performance, :program:`traffic_server` caches commonly used data structures in a
set of free object lists. This option disables these caches, causing :program:`traffic_server` to
use :manpage:`malloc(3)` for every allocation. Though this option should not commonly be needed, it
may be beneficial in memory-constrained environments or where the working set is highly variable.

.. option:: -F, --disable_pfreelist

Disable free list in ProxyAllocator which were left out by the -f option. This option includes the
functionality of :option:`-f`.

.. option:: -R LEVEL, --regression LEVEL

.. option:: -r TEST, --regression_test TEST

.. option:: -l, --regression_list

If Traffic Server was built with tests enabled, this option lists
the available tests.

.. option:: -T TAGS, --debug_tags TAGS

.. option:: -i COUNT, --interval COUNT

.. option:: -m COUNT, --maxRecords

The maximum number of entries in metrics and configuration variables. The default is 1600, which is
also the minimum. This may need to be increased if running plugins that create metrics.

.. option:: -M, --remote_management

Indicates the process should expect to be managed by :ref:`traffic_manager`. This option should not
be used except by that process.

.. option:: -n COUNT, --net_threads COUNT

.. option:: -k, --clear_hostdb

.. option:: -K, --clear_cache

.. option:: --accept_mss MSS

.. option:: -t MSECS, --poll_timeout MSECS

.. option:: -h, --help

   Print usage information and exit.

.. option:: -p PORT, --httpport PORT

.. option:: -U COUNT, --udp_threads COUNT

.. option:: -V, --version

   Print version information and exit.

Environment
===========

.. envvar:: PROXY_REMOTE_MGMT

This environment variable forces :program:`traffic_server` to believe that it is being managed by
:program:`traffic_manager`.

.. envvar:: PROXY_AUTO_EXIT

When this environment variable is set to an integral number of
seconds, :program:`traffic_server` will exit that many seconds after
startup. This is primarily useful for testing.

Signals
=======

SIGINT, SIGTERM
  On `SIGINT` and `SIGTERM`, :program:`traffic_server` exits.

SIGUSR1
  On `SIGUSR1`, :program:`traffic_server` logs its current memory usage.

SIGUSR2
  On `SIGUSR2`, :program:`traffic_server` re-opens its standard error and standard out file descriptors.

See also
========

:manpage:`traffic_ctl(8)`,
:manpage:`traffic_manager(8)`
