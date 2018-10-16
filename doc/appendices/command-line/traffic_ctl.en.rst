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
:program:`traffic_ctl config`
    Manipulate and display configuration records
:program:`traffic_ctl metric`
    Manipulate performance and status metrics
:program:`traffic_ctl server`
    Stop, restart and examine the server
:program:`traffic_ctl storage`
    Manipulate cache storage
:program:`traffic_ctl plugin`
    Interact with plugins.
:program:`traffic_ctl host`
    Manipulate host status.  parents for now but will be expanded to origins.

To use :program:`traffic_ctl`, :ref:`traffic_manager` needs to be running.

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

    Initiate a Traffic Server configuration reload. Use this command
    to update the running configuration after any configuration
    file modification. If no configuration files have been modified
    since the previous configuration load, this command is a no-op.

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

.. _traffic-ctl-metric:

traffic_ctl metric
------------------
.. program:: traffic_ctl metric
.. option:: get METRIC [METRIC...]

    Display the current value of the specifies statistics.

.. program:: traffic_ctl metric
.. option:: match REGEX [REGEX...]

    Display the current values of all statistics whose names match
    the given regular expression.

.. program:: traffic_ctl metric
.. option:: zero METRIC [METRIC...]

    Reset the named statistics to zero.

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
.. option:: start

   Start :program:`traffic_server` if it is already running.

.. program:: traffic_ctl server start
.. option:: --clear-cache

   Clear the disk cache upon startup.

.. option:: --clear-hostdb

   Clear the DNS resolver cache upon startup.

.. program:: traffic_ctl server
.. option:: status

   Show the current proxy server status, indicating if we're running or not.

.. program:: traffic_ctl server
.. option:: stop

   Stop the running :program:`traffic_server` process.

.. program:: traffic_ctl server
.. option:: backtrace

    Show a full stack trace of all the :program:`traffic_server` threads.

traffic_ctl storage
-------------------
.. program:: traffic_ctl storage
.. option:: offline PATH [PATH ...]

   Mark a cache storage device as offline. The storage is identified
   by :arg:`PATH` which must match exactly a path specified in
   :file:`storage.config`. This removes the storage from the cache
   and redirects requests that would have used this storage to other
   storage. This has exactly the same effect as a disk failure for
   that storage. This does not persist across restarts of the
   :program:`traffic_server` process.

traffic_ctl plugin
-------------------
.. program:: traffic_ctl plugin
.. option:: msg TAG DATA

    Send a message to plugins. All plugins that have hooked the :cpp:enumerator:`TSLifecycleHookID::TS_LIFECYCLE_MSG_HOOK`
    will receive a callback for that hook. The :arg:`TAG` and :arg:`DATA` will be available to the
    plugin hook processing. It is expected that plugins will use :arg:`TAG` to select relevant messages
    and determine the format of the :arg:`DATA`.

traffic_ctl host
----------------
.. program:: traffic_ctl host
.. option:: status HOSTNAME [HOSTNAME ...]

    Get the current status of the hosts used in parent.config as a next hop in a multi-tiered cache heirarchy.  The value 0 or 1 is returned indicating that the host is marked as down '0' or marked as up '1'.  If a host is marked as down, it will not be used as the next hop parent, another host marked as up will be chosen.

.. program:: traffic_ctl host
.. option:: down --time seconds --reason 'manual|active|local' HOSTNAME [HOSTNAME ...]

    Marks the listed hosts as down so that they will not be chosen as a next hop parent.
    If the --time option is included, the host is marked down for the specified number of
    seconds after which the host will automatically be marked up.  0 seconds marks the host
    down indefinitely until marked up manually and is the default. A reason tag may be used
    when marking a host down.  Valid values are 'manual', 'active', or 'local', 'manual' is
    used as the default.  The tags are used to indicate wehter the host was marked down
    manually or by an 'active' or 'local' health check.  There are three reason tag
    metrics for each host that may be viewed to see the reason a host was marked down.

.. program:: traffic_ctl host
.. option:: up --reason 'manual|active|local' HOSTNAME [HOSTNAME ...]

    Marks the listed hosts as up so that they will be available for use as a next hop parent.
    By default, the 'manual' reason tag is used when marking up a host.  Use the --reason
    tag to mark the host reason stat as up using one of 'manual', 'active', or 'local'.

Examples
========

Configure Traffic Server to insert ``Via`` header in the response to
the client::

    $ traffic_ctl config set proxy.config.http.insert_response_via_str 1
    $ traffic_ctl config reload

See also
========

:manpage:`records.config(5)`,
:manpage:`storage.config(5)`
