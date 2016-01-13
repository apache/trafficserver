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

.. _traffic_ctl:

traffic_ctl
***********

Synopsis
========

:program:`traffic_ctl` [OPTIONS] SUBCOMMAND [OPTIONS]

.. _traffic-ctl-commands:

Description
===========

:program:`traffic_ctl` is used to display and manipulate configure
a running Traffic Server. :program:`traffic_ctl` includes a number
of subcommands that control different aspects of Traffic Server:

:program:`traffic_ctl alarm`
    Display and manipulate Traffic Server alarms
:program:`traffic_ctl cluster`
    Stop, restart and examine the cluster
:program:`traffic_ctl config`
    Manipulate and display configuration records
:program:`traffic_ctl metric`
    Manipulate performance and status metrics
:program:`traffic_ctl server`
    Stop, restart and examine the server
:program:`traffic_ctl storage`
    Manipulate cache storage

Options
=======

.. program:: traffic_ctl
.. option:: --debug

    Enable debugging output.

.. option:: -V, --version

    Print version information and exit.

Subcommands
===========

traffic_ctl alarm
-----------------
.. program:: traffic_ctl alarm
.. option:: list

   List all alarm events that have not been acknowledged (cleared).

.. program:: traffic_ctl alarm
.. option:: clear

   Clear (acknowledge) all current alarms.

.. program:: traffic_ctl alarm
.. option:: resolve ALARM [ALARM...]

   Clear (acknowledge) an alarm event. The arguments are a specific
   alarm number (e.g. ''1''), or an alarm string identifier (e.g.
   ''MGMT_ALARM_PROXY_CONFIG_ERROR'').

traffic_ctl cluster
-------------------
.. program:: traffic_ctl cluster
.. option:: restart [--drain] [--manager]

    Shut down and immediately restart Traffic Server, node by node across the
    cluster. The *--drain* and *--manager* options have the same behavior as
    for the :option:`traffic_ctl server restart` subcommand.

.. program:: traffic_ctl cluster
.. option:: status

   Show the current cluster status.

traffic_ctl config
------------------
.. program:: traffic_ctl config
.. option:: defaults [--records]

    Display the default values for all configuration records. The
    *--records* flag has the same behavior as :option:`traffic_ctl
    config get --records`.

.. program:: traffic_ctl config
.. option:: describe RECORD [RECORD...]

    Display all the known information about a configuration record.
    This includes the current and default values, the data type,
    the record class and syntax checking expression.

.. program:: traffic_ctl config
.. option:: diff [--records]

    Display configuration records that have non-default values. The
    *--records* flag has the same behavior as :option:`traffic_ctl
    config get --records`.

.. program:: traffic_ctl config
.. option:: get [--records] RECORD [RECORD...]

    Display the current value of a configuration record.

.. program:: traffic_ctl config get
.. option:: --records

    If this flag is provided, :option:`traffic_ctl config get` will emit
    results in :file:`records.config` format.

.. program:: traffic_ctl config
.. option:: match [--records] REGEX [REGEX...]

    Display the current values of all configuration variables whose
    names match the given regular expression. The *--records* flag
    has the same behavior as :option:`traffic_ctl config get --records`.

.. program:: traffic_ctl config
.. option:: reload

    Initiate a Traffic Server configuration reload. Use this
    command to update the running configuration after any configuration
    file modification.

    The timestamp of the last reconfiguration event (in seconds
    since epoch) is published in the `proxy.node.config.reconfigure_time`
    metric.

.. program:: traffic_ctl config
.. option:: set RECORD VALUE

    Set the named configuration record to the specified value.
    Refer to the :file:`records.config` documentation for a list
    of the configuration variables you can specify. Note that this
    is not a synchronous operation.

.. program:: traffic_ctl config
.. option:: status

    Display detailed status about the Traffic Server configuration
    system. This includes version information, whether the internal
    configuration store is current and whether any daemon processes
    should be restarted.

traffic_ctl metric
------------------
.. program:: traffic_ctl metric
.. option:: clear [--cluster]

    Reset all statistics to zero. The *--cluster* option
    applies this across all cluster nodes.

.. program:: traffic_ctl metric
.. option:: get METRIC [METRIC...]

    Display the current value of the specifies statistics.

.. program:: traffic_ctl metric
.. option:: match REGEX [REGEX...]

    Display the current values of all statistics whose names match
    the given regular expression.

.. program:: traffic_ctl metric
.. option:: zero [--cluster] METRIC [METRIC...]

    Reset the named statistics to zero. The *--cluster* option applies this
    across all cluster nodes.

traffic_ctl server
------------------
.. program:: traffic_ctl server
.. option:: restart

    Shut down and immediately restart Traffic Server

.. program:: traffic_ctl server restart
.. option:: --drain

    This option modifies the behavior of :option:`traffic_ctl server restart`
    such that :program:`traffic_server` is not shut down until the
    number of active client connections drops to the number given
    by the :ts:cv:`proxy.config.restart.active_client_threshold`
    configuration variable.

.. option:: --manager

    The default behavior of :option:`traffic_ctl server restart` is to restart
    :program:`traffic_server`. If this option is specified,
    :program:`traffic_manager` is also restarted.

.. program:: traffic_ctl server
.. option:: status

   Show the current proxy server status, indicating if we're running or not.

.. program:: traffic_ctl server
.. option:: backtrace

    Show a full stack trace of all the :program:`traffic_server` threads.

traffic_ctl storage
-------------------
.. program:: traffic_ctl storage
.. option:: offline DEVICE [DEVICE ...]

   Mark a cache storage device as offline. The storage is identified
   by a *path* which must match exactly a path specified in
   :file:`storage.config`. This removes the storage from the cache
   and redirects requests that would have used this storage to other
   storage. This has exactly the same effect as a disk failure for
   that storage. This does not persist across restarts of the
   :program:`traffic_server` process.

Examples
========

Configure Traffic Server to log in Squid format::

    $ traffic_ctl config set proxy.config.log.squid_log_enabled 1
    $ traffic_ctl config set proxy.config.log.squid_log_is_ascii 1
    $ traffic_ctl config reload

See also
========

:manpage:`records.config(5)`,
:manpage:`storage.config(5)`
