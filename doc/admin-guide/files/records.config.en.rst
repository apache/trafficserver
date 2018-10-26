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

.. configfile:: records.config

records.config
**************

The :file:`records.config` file (by default, located in
``/usr/local/etc/trafficserver/``) is a list of configurable variables used by
the |TS| software. Many of the variables in :file:`records.config` are set
automatically when you set configuration options with :option:`traffic_ctl config set`. After you
modify :file:`records.config`, run the command :option:`traffic_ctl config reload`
to apply the changes.

Note: The configuration directory, containing the ``SYSCONFDIR`` value specified at build time
relative to the installation prefix, contains |TS| configuration files.
The ``$TS_ROOT`` environment variable can be used alter the installation prefix at run time.
The directory must allow read/write access for configuration reloads.

Format
======

Each variable has the following format::

   CONFIG variable_name DATATYPE variable_value

Data Type
---------

A variable's type is defined by the ``DATATYPE`` and must be one of:

========== ====================================================================
Type       Description
========== ====================================================================
``FLOAT``  Floating point, expressed as a decimal number without units or
           exponents.
``INT``    Integers, expressed with or without unit prefixes (as described
           below).
``STRING`` String of characters up to the first newline. No quoting necessary.
========== ====================================================================

Values
------

The *variable_value* must conform to the variable's type. For ``STRING``, this
is simply any character data until the first newline.

For integer (``INT``) variables, values are expressed as any normal integer,
e.g. ``32768``. They can also be expressed using more human readable values
using standard unit prefixes, e.g. ``32K``. The following prefixes are
supported for all ``INT`` type configurations:

====== ============ ===========================================================
Prefix Description  Equivalent in Bytes
====== ============ ===========================================================
``K``  Kilobytes    1,024 bytes
``M``  Megabytes    1,048,576 bytes (1024\ :sup:`2`)
``G``  Gigabytes    1,073,741,824 bytes (1024\ :sup:`3`)
``T``  Terabytes    1,099,511,627,776 bytes (1024\ :sup:`4`)
====== ============ ===========================================================

.. important::

   Unless :ts:cv:`proxy.config.disable_configuration_modification` is enabled,
   |TS| writes configurations back to disk periodically. When doing so, the
   unit prefixes are not preserved.

Floating point variables (``FLOAT``) must be expressed as a regular decimal
number. Unit prefixes are not supported, nor are alternate notations (scientific,
exponent, etc.).

Additional Attributes
---------------------

Deprecated
~~~~~~~~~~

A variable marked as *Deprecated* is still functional but should be avoided
as it may be removed in a future release without warning.

Reloadable
~~~~~~~~~~

A variable marked as *Reloadable* can be updated via the command::

   traffic_ctl config reload

This updates configuration parameters without restarting |TS| or interrupting
the processing of requests.

Overridable
~~~~~~~~~~~

A variable marked as *Overridable* can be changed on a per-remap basis using
plugins (like the :ref:`admin-plugins-conf-remap`), affecting operations within
the current transaction only.

Examples
========

In the following example, the variable `proxy.config.proxy_name`_ is
a ``STRING`` datatype with the value ``my_server``. This means that the
name of the |TS| proxy is ``my_server``. ::

   CONFIG proxy.config.proxy_name STRING my_server

If the server name should be ``that_server`` the line would be ::

   CONFIG proxy.config.proxy_name STRING that_server

In the following example, the variable ``proxy.config.arm.enabled`` is
a yes/no flag. A value of ``0`` (zero) disables the option; a value of
``1`` enables the option. ::

   CONFIG proxy.config.arm.enabled INT 0

In the following example, the variable sets the time to wait for a
DNS response to 10 seconds. ::

   CONFIG proxy.config.hostdb.lookup_timeout INT 10

The last examples configures a 64GB RAM cache, using a human readable
prefix. ::

   CONFIG proxy.config.cache.ram_cache.size INT 64G

Environment Overrides
=====================

Every :file:`records.config` configuration variable can be overridden
by a corresponding environment variable. This can be useful in
situations where you need a static :file:`records.config` but still
want to tweak one or two settings. The override variable is formed
by converting the :file:`records.config` variable name to upper
case, and replacing any dot separators with an underscore.

Overriding a variable from the environment is permanent and will
not be affected by future configuration changes made in
:file:`records.config` or applied with :program:`traffic_ctl`.

For example, we could override the `proxy.config.product_company`_ variable
like this::

   $ PROXY_CONFIG_PRODUCT_COMPANY=example traffic_manager &
   $ traffic_ctl config get proxy.config.product_company

.. _configuration-variables:

Configuration Variables
=======================

The following list describes the configuration variables available in
the :file:`records.config` file.

System Variables
----------------

.. ts:cv:: CONFIG proxy.config.product_company STRING Apache Software Foundation

   The name of the organization developing |TS|.

.. ts:cv:: CONFIG proxy.config.product_vendor STRING Apache

   The name of the vendor providing |TS|.

.. ts:cv:: CONFIG proxy.config.product_name STRING |TS|

   The name of the product.

.. ts:cv:: CONFIG proxy.config.proxy_name STRING build_machine
   :reloadable:

   The name of the |TS| node.

.. ts:cv:: CONFIG proxy.config.bin_path STRING bin

   The location of the |TS| ``bin`` directory.

.. ts:cv:: CONFIG proxy.config.proxy_binary STRING traffic_server

   The name of the executable that runs the :program:`traffic_server` process.

.. ts:cv:: CONFIG proxy.config.proxy_binary_opts STRING -M

   The command-line options for starting |TS|.

.. ts:cv:: CONFIG proxy.config.manager_binary STRING traffic_manager

   The name of the executable that runs the :program:`traffic_manager` process.

.. ts:cv:: CONFIG proxy.config.env_prep STRING

   The script executed before the :program:`traffic_manager` process spawns
   the :program:`traffic_server` process.

.. ts:cv:: CONFIG proxy.config.syslog_facility STRING LOG_DAEMON

   The facility used to record system log files. Refer to
   :ref:`admin-logging-understanding` for more in-depth discussion
   of the contents and interpretations of log files.


.. ts:cv:: CONFIG proxy.config.output.logfile  STRING traffic.out

   The name and location of the file that contains warnings, status messages, and error messages produced by the |TS|
   processes. If no path is specified, then |TS| creates the file in its logging directory.


.. ts:cv:: CONFIG proxy.config.output.logfile_perm STRING rw-r--r--

   The log file permissions. The standard UNIX file permissions are used (owner, group, other). Permissible values are:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``-`` No permissions.
   ``r`` Read permission.
   ``w`` Write permission.
   ``x`` Execute permission.
   ===== ======================================================================

   Permissions are subject to the umask settings for the |TS| process. This
   means that a umask setting of ``002`` will not allow write permission for
   others, even if specified in the configuration file. Permissions for
   existing log files are not changed when the configuration is modified.


.. ts:cv:: CONFIG proxy.config.output.logfile.rolling_enabled INT 0
   :reloadable:

   Specifies how the output log is rolled. You can specify the following values:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disables output log rolling.
   ``1`` Enables output log rolling at specific intervals (specified with the
         :ts:cv:`proxy.config.output.logfile.rolling_interval_sec` variable).
         The clock starts ticking on |TS| boot.
   ``2`` Enables output log rolling when the output log reaches a specific size
         (specified with :ts:cv:`proxy.config.output.logfile.rolling_size_mb`).
   ``3`` Enables output log rolling at specific intervals or when the output log
         reaches a specific size (whichever occurs first).
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.output.logfile.rolling_interval_sec INT 3600
   :reloadable:
   :units: seconds

   Specifies how often the output log is rolled, in seconds. The timer starts on |TS| bootup.

.. ts:cv:: CONFIG proxy.config.output.logfile.rolling_size_mb INT 100
   :reloadable:
   :units: megabytes

   Specifies at what size to roll the output log at.


Thread Variables
----------------

.. ts:cv:: CONFIG proxy.config.exec_thread.autoconfig INT 1

   When enabled (the default, ``1``), |TS| scales threads according to the
   available CPU cores. See the config option below.

.. ts:cv:: CONFIG proxy.config.exec_thread.autoconfig.scale FLOAT 1.5

   Factor by which |TS| scales the number of threads. The multiplier is usually
   the number of available CPU cores. By default this is scaling factor is
   ``1.5``.

.. ts:cv:: CONFIG proxy.config.exec_thread.limit INT 2

   The number of threads |TS| will create if `proxy.config.exec_thread.autoconfig`
   is set to ``0``, otherwise this option is ignored.

.. ts:cv:: CONFIG proxy.config.accept_threads INT 1

   The number of accept threads. If disabled (``0``), then accepts will be done
   in each of the worker threads.

.. ts:cv:: CONFIG proxy.config.thread.default.stacksize INT 1048576

   Default thread stack size, in bytes, for all threads (default is 1 MB).

.. ts:cv:: CONFIG proxy.config.exec_thread.affinity INT 1

   Bind threads to specific processing units.

   ===== =======================================
   Value Effect
   ===== =======================================
   ``0`` Assign threads to machine.
   ``1`` Assign threads to NUMA nodes [default].
   ``2`` Assign threads to sockets.
   ``3`` Assign threads to cores.
   ``4`` Assign threads to processing units.
   ===== =======================================

.. note::

   This option only has an affect when |TS| has been compiled with ``--enable-hwloc``.

.. ts:cv:: CONFIG proxy.config.system.file_max_pct FLOAT 0.9

   Set the maximum number of file handles for the traffic_server process as a percentage of the the fs.file-max proc value in Linux. The default is 90%.

.. ts:cv:: CONFIG proxy.config.crash_log_helper STRING traffic_crashlog

   This option directs :program:`traffic_server` to spawn a crash
   log helper at startup. The value should be the path to an
   executable program. If the path is not absolute, it is located
   relative to configured ``bin`` directory.  Any user-provided
   program specified here must behave in a fashion compatible with
   :program:`traffic_crashlog`. Specifically, it must implement
   the :option:`traffic_crashlog --wait` behavior.

   This setting not reloadable because the helper must be spawned
   before :program:`traffic_server` drops privilege. If this variable
   is set to ``NULL``, no helper will be spawned.

.. ts::vc:: CONFIG proxy.config.core_limit INT -1

   This option specifies the size limit for core files in the event
   that :program:`traffic_server` crashes. ``-1`` means there is
   no limit. A value of ``0`` prevents core dump creation.

.. ts:cv:: CONFIG proxy.config.restart.active_client_threshold INT 0
   :reloadable:

   This setting specifies the number of active client connections
   for use by :option:`traffic_ctl server restart --drain`.

.. ts:cv:: CONFIG proxy.config.restart.stop_listening INT 0
   :reloadable:

   This option specifies whether |TS| should close listening sockets while shutting down gracefully.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Listening sockets will be kept open.
   ``1`` Listening sockets will be closed when |TS| starts shutting down.
   ===== ======================================================================


.. ts:cv:: CONFIG proxy.config.stop.shutdown_timeout INT 0
   :reloadable:

   The shutdown timeout(in seconds) to apply when stopping Traffic
   Server, in which ATS can initiate graceful shutdowns. It only supports
   HTTP/2 graceful shutdown for now. Stopping |TS| here means sending
   `traffic_server` a signal either by `bin/trafficserver stop` or `kill`.

.. ts:cv:: CONFIG proxy.config.thread.max_heartbeat_mseconds INT 60
   :units: milliseconds

   Set the maximum heartbeat in milliseconds for threads, ranges from 0 to 1000.

   This controls the maximum amount of time the event loop will wait for I/O activity.
   On a system that is not busy, this option can be set to a higher value to decrease
   the spin around overhead. If experiencing unexpected delays, setting a lower value
   should improve the situation. Note that this setting should only be used by expert
   system tuners, and will not be beneficial with random fiddling.

Network
=======

.. ts:cv:: CONFIG proxy.config.net.connections_throttle INT 30000

   The total number of client and origin server connections that the server
   can handle simultaneously. This is in fact the max number of file
   descriptors that the :program:`traffic_server` process can have open at any
   given time. Roughly 10% of these connections are reserved for origin server
   connections, i.e. from the default, only ~9,000 client connections can be
   handled. This should be tuned according to your memory size, and expected
   work load.  If this is set to 0, the throttling logic is disabled.

.. ts:cv:: CONFIG proxy.config.net.default_inactivity_timeout INT 86400
   :reloadable:

   The connection inactivity timeout (in seconds) to apply when
   |TS| detects that no inactivity timeout has been applied
   by the HTTP state machine. When this timeout is applied, the
   `proxy.process.net.default_inactivity_timeout_applied` metric
   is incremented.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.net.inactivity_check_frequency INT 1

   How frequent (in seconds) to check for inactive connections. If you deal
   with a lot of concurrent connections, increasing this setting can reduce
   pressure on the system.

.. ts:cv:: LOCAL proxy.local.incoming_ip_to_bind STRING 0.0.0.0 [::]

   Controls the global default IP addresses to which to bind proxy server
   ports. The value is a space separated list of IP addresses, one per
   supported IP address family (currently IPv4 and IPv6).

   Unless explicitly specified in :ts:cv:`proxy.config.http.server_ports`, the
   server port will be bound to one of these addresses, selected by IP address
   family. The built in default is any address. This is used if no address for
   a family is specified. This setting is useful if most or all server ports
   should be bound to the same address.

.. note::

   This is ignored for inbound transparent server ports because they must be
   able to accept connections on arbitrary IP addresses.

.. topic:: Example

   Set the global default for IPv4 to ``192.168.101.18`` and leave the global
   default for IPv6 as any address::

      LOCAL proxy.local.incoming_ip_to_bind STRING 192.168.101.18

.. topic:: Example

   Set the global default for IPv4 to ``191.68.101.18`` and the global default
   for IPv6 to ``fc07:192:168:101::17``::

      LOCAL proxy.local.incoming_ip_to_bind STRING 192.168.101.18 [fc07:192:168:101::17]

.. ts:cv:: LOCAL proxy.local.outgoing_ip_to_bind STRING 0.0.0.0 [::]

   This controls the global default for the local IP address for outbound
   connections to origin servers. The value is a list of space separated IP
   addresses, one per supported IP address family (currently IPv4 and IPv6).

   Unless explicitly specified in :ts:cv:`proxy.config.http.server_ports`, one
   of these addresses, selected by IP address family, will be used as the local
   address for outbound connections. This setting is useful if most or all of
   the server ports should use the same outbound IP addresses.

.. note::

   This is ignored for outbound transparent ports as the local outbound address will be the same as the client local address.

.. topic:: Example

   Set the default local outbound IP address for IPv4 connections to ``192.168.101.18``.::

      LOCAL proxy.local.outgoing_ip_to_bind STRING 192.168.101.18

.. topic:: Example

   Set the default local outbound IP address to ``192.168.101.17`` for IPv4 and ``fc07:192:168:101::17`` for IPv6.::

      LOCAL proxy.local.outgoing_ip_to_bind STRING 192.168.101.17 [fc07:192:168:101::17]

.. ts:cv:: CONFIG proxy.config.net.event_period INT 10

   How often, in milli-seconds, to schedule IO event processing. This is
   unlikely to be necessary to tune, and we discourage setting it to a value
   smaller than 10ms (on Linux).

.. ts:cv:: CONFIG proxy.config.net.accept_period INT 10

   How often, in milli-seconds, to schedule accept() processing. This is
   unlikely to be necessary to tune, and we discourage setting it to a value
   smaller than 10ms (on Linux).

.. ts:cv:: CONFIG proxy.config.net.retry_delay INT 10
   :reloadable:

   How long to wait until we retry various events that would otherwise block
   the network processing threads (e.g. locks). We discourage setting this to
   a value smaller than 10ms (on Linux).

.. ts:cv:: CONFIG proxy.config.net.throttle_delay INT 50
   :reloadable:

   When we trigger a throttling scenario, this how long our accept() are delayed.

Local Manager
=============

.. ts:cv:: CONFIG proxy.config.admin.number_config_bak INT 3

   The maximum number of copies of rolled configuration files to keep.

.. ts:cv:: CONFIG proxy.config.admin.user_id STRING nobody

   Designates the non-privileged account to run the :program:`traffic_server`
   process as, which also has the effect of setting ownership of configuration
   and log files.

   As of version 2.1.1 if the user_id is prefixed with pound character (``#``)
   the remainder of the string is considered to be a
   `numeric user identifier <http://en.wikipedia.org/wiki/User_identifier>`_.
   If the value is set to ``#-1`` |TS| will not change the user during startup.

   .. important::

      Attempting to set this option to ``root`` or ``#0`` is now forbidden, as
      a measure to increase security. Doing so will cause a fatal failure upon
      startup in :program:`traffic_server`. However, there are two ways to
      bypass this restriction:

      * Specify ``-DBIG_SECURITY_HOLE`` in ``CXXFLAGS`` during compilation.

      * Set the ``user_id=#-1`` and start trafficserver as root.

.. ts:cv:: CONFIG proxy.config.admin.api.restricted INT 0

   This setting specifies whether the management API should be restricted to
   root processes. If this is set to ``0``, then on platforms that support
   passing process credentials, non-root processes will be allowed to make
   read-only management API calls. Any management API calls that modify server
   state (eg. setting a configuration variable) will still be restricted to
   root processes.

   This setting is not reloadable, since it is must be applied when
   program:`traffic_manager` initializes.

.. ts:cv:: CONFIG proxy.config.disable_configuration_modification INT 0
   :reloadable:

   This setting prevents |TS| from rewriting the :file:`records.config`
   configuration file. Dynamic configuration changes can still be made using
   :program:`traffic_ctl config set`, but these changes will not be persisted
   on service restarts or when :option:`traffic_ctl config reload` is run.

Alarm Configuration
===================

.. ts:cv:: CONFIG proxy.config.alarm_email STRING
   :reloadable:

   The address to which the alarm script should send email.

.. ts:cv:: CONFIG proxy.config.alarm.bin STRING example_alarm_bin.sh
   :reloadable:

   Name of the script file that can execute certain actions when
   an alarm is signaled. The script is invoked with up to 4 arguments:

   - The alarm message.
   - The value of :ts:cv:`proxy.config.product_name`.
   - The value of :ts:cv:`proxy.config.admin.user_id`.
   - The value of :ts:cv:`proxy.config.alarm_email`.

.. ts:cv:: CONFIG proxy.config.alarm.abs_path STRING NULL
   :reloadable:

   The absolute path to the directory containing the alarm script.
   If this is not set, the script will be located relative to
   :ts:cv:`proxy.config.bin_path`.

.. ts:cv:: CONFIG proxy.config.alarm.script_runtime INT 5
   :reloadable:

   The number of seconds that |TS| allows the alarm script
   to run before aborting it.

HTTP Engine
===========

.. ts:cv:: CONFIG proxy.config.http.server_ports STRING 8080 8080:ipv6

   Ports used for proxying HTTP traffic.

   This is a list, separated by space or comma, of :index:`port descriptors`.
   Each descriptor is a sequence of keywords and values separated by colons.
   Not all keywords have values, those that do are specifically noted. Keywords
   with values can have an optional ``=`` character separating the keyword and
   value. The case of keywords is ignored. The order of keywords is irrelevant
   but unspecified results may occur if incompatible options are used (noted
   below). Options without values are idempotent. Options with values use the
   last (right most) value specified, except for ``ip-out`` as detailed later.

   Quick reference chart:

   =========== =============== ========================================
   Name        Note            Definition
   =========== =============== ========================================
   *number*    Required        The local port.
   blind                       Blind (``CONNECT``) port.
   compress    Not Implemented Compressed.
   ipv4        Default         Bind to IPv4 address family.
   ipv6                        Bind to IPv6 address family.
   ip-in       Value           Local inbound IP address.
   ip-out      Value           Local outbound IP address.
   ip-resolve  Value           IP address resolution style.
   proto       Value           List of supported session protocols.
   ssl                         SSL terminated.
   tr-full                     Fully transparent (inbound and outbound)
   tr-in                       Inbound transparent.
   tr-out                      Outbound transparent.
   tr-pass                     Pass through enabled.
   mptcp                       Multipath TCP.
   =========== =============== ========================================

*number*
   Local IP port to bind. This is the port to which ATS clients will connect.

blind
   Accept only the ``CONNECT`` method on this port.

   Not compatible with: ``tr-in``, ``ssl``.

compress
   Compress the connection. Retained only by inertia, should be considered "not implemented".

ipv4
   Use IPv4. This is the default and is included primarily for completeness. This forced if the ``ip-in`` option is used with an IPv4 address.

ipv6
   Use IPv6. This is forced if the ``ip-in`` option is used with an IPv6 address.

ssl
   Require SSL termination for inbound connections. SSL :ref:`must be configured <admin-ssl-termination>` for this option to provide a functional server port.

   Not compatible with: ``blind``.

proto
   Specify the :ref:`session level protocols <session-protocol>` supported. These should be
   separated by semi-colons. For TLS proxy ports the default value is
   all available protocols. For non-TLS proxy ports the default is HTTP
   only.

tr-full
   Fully transparent. This is a convenience option and is identical to specifying both ``tr-in`` and ``tr-out``.

   Not compatible with: Any option not compatible with ``tr-in`` or ``tr-out``.

tr-in
   Inbound transparent. The proxy port will accept connections to any IP address on the port. To have IPv6 inbound transparent you must use this and the ``ipv6`` option. This overrides :ts:cv:`proxy.local.incoming_ip_to_bind` for this port.

   Not compatible with: ``ip-in``, ``blind``

tr-out
   Outbound transparent. If ATS connects to an origin server for a transaction on this port, it will use the client's address as its local address. This overrides :ts:cv:`proxy.local.outgoing_ip_to_bind` for this port.

   Not compatible with: ``ip-out``, ``ip-resolve``

tr-pass
   Transparent pass through. This option is useful only for inbound transparent proxy ports. If the parsing of the expected HTTP header fails, then the transaction is switched to a blind tunnel instead of generating an error response to the client. It effectively enables :ts:cv:`proxy.config.http.use_client_target_addr` for the transaction as there is no other place to obtain the origin server address.

ip-in
   Set the local IP address for the port. This is the address to which clients will connect. This forces the IP address family for the port. The ``ipv4`` or ``ipv6`` can be used but it is optional and is an error for it to disagree with the IP address family of this value. An IPv6 address **must** be enclosed in square brackets. If this option is omitted :ts:cv:`proxy.local.incoming_ip_to_bind` is used.

   Not compatible with: ``tr-in``.

ip-out
   Set the local IP address for outbound connections. This is the address used by ATS locally when it connects to an origin server for transactions on this port. If this is omitted :ts:cv:`proxy.local.outgoing_ip_to_bind` is used.

   This option can used multiple times, once for each IP address family. The address used is selected by the IP address family of the origin server address.

   Not compatible with: ``tr-out``.

ip-resolve
   Set the :ts:cv:`host resolution style <proxy.config.hostdb.ip_resolve>` for transactions on this proxy port.

   Not compatible with: ``tr-out`` - this option requires a value of ``client;none`` which is forced and should not be explicitly specified.

mptcp
   Enable Multipath TCP on this proxy port.

   Requires custom Linux kernel available at https://multipath-tcp.org.

.. topic:: Example

   Listen on port 80 on any address for IPv4 and IPv6.::

      80 80:ipv6

.. topic:: Example

   Listen transparently on any IPv4 address on port 8080, and
   transparently on port 8080 on local address ``fc01:10:10:1::1``
   (which implies ``ipv6``).::

      IPv4:tr-FULL:8080 TR-full:IP-in=[fc02:10:10:1::1]:8080

.. topic:: Example

   Listen on port 8080 for IPv6, fully transparent. Set up an SSL port on 443. These ports will use the IP address from :ts:cv:`proxy.local.incoming_ip_to_bind`.  Listen on IP address ``192.168.17.1``, port 80, IPv4, and connect to origin servers using the local address ``10.10.10.1`` for IPv4 and ``fc01:10:10:1::1`` for IPv6.::

      8080:ipv6:tr-full 443:ssl ip-in=192.168.17.1:80:ip-out=[fc01:10:10:1::1]:ip-out=10.10.10.1

.. topic:: Example

   Listen on port 9090 for TLS enabled HTTP/2 or HTTP connections, accept no other session protocols.::

      9090:proto=http2;http:ssl

.. topic:: Example

   Listen on port 9090 for TLS disabled HTTP/2 and enabled HTTP connections, accept no other session protocols.::

      9090:proto=http:ssl

.. ts:cv:: CONFIG proxy.config.http.connect_ports STRING 443

   The range of origin server ports that can be used for tunneling via ``CONNECT``.

   |TS| allows tunnels only to the specified ports. Supports both wildcards
   (``*``) and ranges (e.g. ``0-1023``).

.. note::

   These are the ports on the *origin server*, not |TS| :ts:cv:`proxy ports <proxy.config.http.server_ports>`.


.. ts:cv:: CONFIG proxy.config.http.forward_connect_method INT 0
   :reloadable:
   :overridable:

   The default, |TS| behavior for handling a CONNECT method request
   is to establish a tunnel to the requested destination. This
   configuration alters the behavior so that |TS| forwards the
   CONNECT method to the next hop, and establishes the tunnel after
   receiving a positive response. This behavior is useful in a proxy
   hierarchy, and is equivalent to setting
   :ts:cv:`proxy.local.http.parent_proxy.disable_connect_tunneling` to
   `0` when parent proxying is enabled.

.. ts:cv:: CONFIG proxy.config.http.insert_request_via_str INT 1
   :reloadable:
   :overridable:

   Set how the ``Via`` field is handled on a request to the origin server.

   ===== ====================================================================
   Value Effect
   ===== ====================================================================
   ``0`` Do not modify or set this Via header.
   ``1`` Add the basic protocol and proxy identifier.
   ``2`` Add basic transaction codes.
   ``3`` Add detailed transaction codes.
   ``4`` Add full user agent connection :ref:`protocol tags <protocol_tags>`.
   ===== ====================================================================

.. note::

   The ``Via`` transaction codes can be decoded with the `Via Decoder Ring <http://trafficserver.apache.org/tools/via>`_.

.. ts:cv:: CONFIG proxy.config.http.request_via_str STRING ApacheTrafficServer/${PACKAGE_VERSION}
   :reloadable:
   :overridable:

   Set the server and version string in the ``Via`` request header to the origin server which is inserted when the value of :ts:cv:`proxy.config.http.insert_request_via_str` is not ``0``.  Note that the actual default value is defined with ``"ApacheTrafficServer/" PACKAGE_VERSION`` in a C++ source code, and you must write such as ``ApacheTrafficServer/6.0.0`` if you really set a value with the version in :file:`records.config` file. If you want to hide the version, you can set this value to ``ApacheTrafficServer``.

.. ts:cv:: CONFIG proxy.config.http.insert_response_via_str INT 0
   :reloadable:
   :overridable:

   Set how the ``Via`` field is handled on the response to the client.

   ===== ==================================================================
   Value Effect
   ===== ==================================================================
   ``0`` Do not modify or set this Via header.
   ``1`` Add the basic protocol and proxy identifier.
   ``2`` Add basic transaction codes.
   ``3`` Add detailed transaction codes.
   ``4`` Add full upstream connection :ref:`protocol tags <protocol_tags>`.
   ===== ==================================================================

.. note::

   The ``Via`` transaction acode can be decoded with the `Via Decoder Ring <http://trafficserver.apache.org/tools/via>`_.

.. ts:cv:: CONFIG proxy.config.http.response_via_str STRING ApacheTrafficServer/${PACKAGE_VERSION}
   :reloadable:
   :overridable:

   Set the server and version string in the ``Via`` response header to the client which is inserted when the value of :ts:cv:`proxy.config.http.insert_response_via_str` is not ``0``.  Note that the actual default value is defined with ``"ApacheTrafficServer/" PACKAGE_VERSION`` in a C++ source code, and you must write such as ``ApacheTrafficServer/6.0.0`` if you really set a value with the version in :file:`records.config` file. If you want to hide the version, you can set this value to ``ApacheTrafficServer``.

.. ts:cv:: CONFIG proxy.config.http.send_100_continue_response INT 0
   :reloadable:

   You can specify one of the following:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` |TS| will buffer the request until the post body has been recieved and
         then send the request to the origin server.
   ``1`` Immediately return a ``100 Continue`` from |TS| without waiting for
         the post body.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.http.response_server_enabled INT 1
   :reloadable:
   :overridable:

   You can specify one of the following:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` No ``Server`` header is added to the response.
   ``1`` The ``Server`` header is added according to
         :ts:cv:`proxy.config.http.response_server_str`.
   ``2`` The ``Server`` header is added only if the response from origin does
         not have one already.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.http.response_server_str STRING ATS/${PACKAGE_VERSION}
   :reloadable:
   :overridable:

   The ``Server`` string that |TS| will insert in a response header (if
   requested, see above). Note that the actual default value is defined with
   ``"ATS/" PACKAGE_VERSION`` in the C++ source, and you must write such as
   ``ATS/6.0.0`` if you really set a value with the version in
   :file:`records.config`. If you want to hide the version, you can set this
   value to ``ATS``.

.. ts:cv:: CONFIG proxy.config.http.insert_age_in_response INT 1
   :reloadable:
   :overridable:

   This option specifies whether |TS| should insert an ``Age`` header in the
   response. The value is the cache's estimate of the amount of time since the
   response was generated or revalidated by the origin server.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` No ``Age`` header is added.
   ``1`` ``Age`` header is added.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.http.chunking_enabled INT 1
   :reloadable:
   :overridable:

   Specifies whether |TS| can generate a chunked response:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Never respond with chunked encoding.
   ``1`` Always respond with chunked encoding.
   ``2`` Generate a chunked response if the origin server has previously
         returned HTTP/1.1.
   ``3`` Generate a chunked response if the client request is HTTP/1.1 and the
         origin server has previously returned HTTP/1.1.
   ===== ======================================================================

.. note::

   If HTTP/1.1 is used, then |TS| can use keep-alive connections with
   pipelining to origin servers.

   If HTTP/1.0 is used, then |TS| can use keep-alive connections without
   pipelining to origin servers.

   If HTTP/0.9 is used, then |TS| does not use keep-alive connections to
   origin servers.

.. ts:cv:: CONFIG proxy.config.http.chunking.size INT 4096
   :overridable:

   If chunked transfer encoding is enabled with :ts:cv:`proxy.config.http.chunking_enabled`,
   and the conditions specified by that option's setting are met by the current
   request, this option determines the size of the chunks, in bytes, to use
   when sending content to an HTTP/1.1 client.

.. ts:cv:: CONFIG proxy.config.http.send_http11_requests INT 1
   :reloadable:
   :overridable:

   Specifies when and how |TS| uses HTTP/1.1 to communicate with the origin
   server.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Never use HTTP/1.1.
   ``1`` Always use HTTP/1.1.
   ``2`` Use HTTP/1.1 with origin connections only if the server has previously
         returned HTTP/1.1.
   ``3`` If the client request is HTTP/1.1 and the origin server has previously
         returned HTTP/1.1, then use HTTP/1.1 for origin server connections.
   ===== ======================================================================

.. note::

   If :ts:cv:`proxy.config.http.use_client_target_addr` is set to ``1``, then
   options ``2`` and ``3`` for this configuration variable cause the proxy
   to use the client HTTP version for upstream requests.

.. ts:cv:: CONFIG proxy.config.http.server_tcp_init_cwnd INT 0
   :overridable:

   Configures the size, in packets, of the initial TCP congestion window on
   sockets used by the HTTP engine. This option may only be used on operating
   systems which support the ``TCP_INIT_CWND`` option on TCP sockets.

.. ts:cv:: CONFIG proxy.config.http.auth_server_session_private INT 1
   :overridable:

   If enabled (``1``) anytime a request contains a ``Authorization``,
   ``Proxy-Authorization``, or ``Www-Authenticate`` header the connection will
   be closed and not reused. This marks the connection as private. When disabled
   (``0``) the connection will be available for reuse.

.. ts:cv:: CONFIG proxy.config.http.server_session_sharing.match STRING both
   :overridable:

   Enable and set the ability to re-use server connections across client
   connections. The valid values are:

   ======== ===================================================================
   Value    Description
   ======== ===================================================================
   ``none`` Do not match and do not re-use server sessions. If using this in
            :ref:`ts-overridable-config` (like the :ref:`admin-plugins-conf-remap`),
            use the integer ``0`` instead.
   ``both`` Re-use server sessions, if *both* the IP address and fully qualified
            domain name match. If using this in :ref:`ts-overridable-config` (like
            the :ref:`admin-plugins-conf-remap`), use the integer ``1`` instead.
   ``ip``   Re-use server sessions, checking only that the IP address and port
            of the origin server matches. If using this in
            :ref:`ts-overridable-config` (like the :ref:`admin-plugins-conf-remap`),
            use the integer ``2`` instead.
   ``host`` Re-use server sessions, checking only that the fully qualified
            domain name matches. If using this in :ref:`ts-overridable-config`
            (like the :ref:`admin-plugins-conf-remap`), use the integer ``3`` instead.
   ======== ===================================================================

   It is strongly recommended to use either ``none`` or ``both`` for this value
   unless you have a specific need for the other settings. The most common
   reason is virtual hosts that share an IP address in which case performance
   can be enhanced if those sessions can be re-used. However, not all web
   servers support requests for different virtual hosts on the same connection
   so use with caution.

.. note::

   Server sessions to different ports never match even if the FQDN and IP
   address match.

.. ts:cv:: CONFIG proxy.config.http.server_session_sharing.pool STRING thread

   Control the scope of server session re-use if it is enabled by
   :ts:cv:`proxy.config.http.server_session_sharing.match`. Valid values are:

   ========== =================================================================
   Value      Description
   ========== =================================================================
   ``global`` Re-use sessions from a global pool of all server sessions.
   ``thread`` Re-use sessions from a per-thread pool.
   ========== =================================================================

.. ts:cv:: CONFIG proxy.config.http.attach_server_session_to_client INT 0
   :overridable:

   Control the re-use of an server session by a user agent (client) session.

   If a user agent performs more than one HTTP transaction on its connection to |TS| a server session must be
   obtained for the second (and subsequent) transaction as for the first. This settings affects how that server session
   is selected.

   If this setting is ``0`` then after the first transaction the server session for that transaction is released to the
   server pool (if any). When a server session is needed for subsequent transactions one is selected from the server
   pool or created if there is no suitable server session in the pool.

   If this setting is not ``0`` then the current server session for the user agent session is "sticky". It will be
   preferred to any other server session (either from the pool or newly created). The server session will be detached
   from the user agent session only if it cannot be used for the transaction. This is determined by the
   :ts:cv:`proxy.config.http.server_session_sharing.match` value. If the server session matches the next transaction
   according to this setting then it will be used, otherwise it will be released to the pool and a different session
   selected or created.

.. ts:cv:: CONFIG proxy.config.http.use_client_target_addr  INT 0

   For fully transparent ports use the same origin server address as the client.

   This option causes |TS| to avoid where possible doing DNS lookups in forward
   transparent proxy mode. The option is only effective if the following three
   conditions are true:

   * |TS| is in forward proxy mode.
   * The proxy port is inbound transparent.
   * The target URL has not been modified by either remapping or a plugin.

   If any of these conditions are not true, then normal DNS processing is done
   for the connection.

   There are three valid values.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disables the feature.
   ``1`` Enables the feature with address verification. The proxy does the
         regular DNS processing. If the client-specified origin address is not
         in the set of addresses found by the proxy, the request continues to
         the client specified address, but the result is not cached.
   ``2`` Enables the feature with no address verification. No DNS processing is
         performed. The result is cached (if allowed otherwise). This option is
         vulnerable to cache poisoning if an incorrect ``Host`` header is
         specified, so this option should be used with extreme caution.  See
         bug TS-2954 for details.
   ===== ======================================================================

   If all of these conditions are met, then the origin server IP address is
   retrieved from the original client connection, rather than through HostDB or
   DNS lookup. In effect, client DNS resolution is used instead of |TS| DNS.

   This can be used to be a little more efficient (looking up the target once
   by the client rather than by both the client and |TS|) but the primary use
   is when client DNS resolution can differ from that of |TS|. Two known uses
   cases are:

   #. Embedded IP addresses in a protocol with DNS load sharing. In this case,
      even though |TS| and the client both make the same request to the same
      DNS resolver chain, they may get different origin server addresses. If
      the address is embedded in the protocol then the overall exchange will
      fail. One current example is Microsoft Windows update, which presumably
      embeds the address as a security measure.

   #. The client has access to local DNS zone information which is not
      available to |TS|. There are corporate nets with local DNS information
      for internal servers which, by design, is not propagated outside the core
      corporate network. Depending a network topology it can be the case that
      |TS| can access the servers by IP address but cannot resolve such
      addresses by name. In such as case the client supplied target address
      must be used.

   This solution must be considered interim. In the longer term, it should be
   possible to arrange for much finer grained control of DNS lookup so that
   wildcard domain can be set to use |TS| or client resolution. In both known
   use cases, marking specific domains as client determined (rather than a
   single global switch) would suffice. It is possible to do this crudely with
   this flag by enabling it and then use identity URL mappings to re-disable it
   for specific domains.

.. ts:cv:: CONFIG proxy.config.http.keep_alive_enabled_in  INT 1
   :overridable:

   Enables (``1``) or disables (``0``) incoming keep-alive connections.

.. ts:cv:: CONFIG proxy.config.http.keep_alive_enabled_out  INT 1
   :overridable:

   Enables (``1``) or disables (``0``) outgoing keep-alive connections.

.. note::

   Enabling keep-alive does not automatically enable purging of keep-alive
   requests when nearing the connection limit, that is controlled by
   :ts:cv:`proxy.config.http.server_max_connections`.

.. ts:cv:: CONFIG proxy.config.http.keep_alive_post_out  INT 1
   :overridable:

   Controls whether new POST requests re-use keep-alive sessions (``1``) or
   create new connections per request (``0``).

.. ts:cv:: CONFIG proxy.config.http.disallow_post_100_continue INT 0

   Allows you to return a 405 Method Not Supported with Posts also
   containing an Expect: 100-continue.

   When a Post w/ Expect: 100-continue is blocked the stat
   proxy.process.http.disallowed_post_100_continue will be incremented.

.. ts:cv:: CONFIG proxy.config.http.default_buffer_size INT 8

   Configures the default buffer size, in bytes, to allocate for incoming
   request bodies which lack a ``Content-length`` header.

.. ts:cv:: CONFIG proxy.config.http.default_buffer_water_mark INT 32768

.. ts:cv:: CONFIG proxy.config.http.request_header_max_size INT 131072

   Controls the maximum size, in bytes, of an HTTP header in requests. Headers
   in a request which exceed this size will cause the entire request to be
   treated as invalid and rejected by the proxy.

.. ts:cv:: CONFIG proxy.config.http.response_header_max_size INT 131072

   Controls the maximum size, in bytes, of headers in HTTP responses from the
   proxy. Any responses with a header exceeding this limit will be treated as
   invalid and a client error will be returned instead.

.. ts:cv:: CONFIG proxy.config.http.global_user_agent_header STRING null
   :overridable:

   An arbitrary string value that, if set, will be used to replace any request
   ``User-Agent`` header.

.. ts:cv:: CONFIG proxy.config.http.strict_uri_parsing INT 0

   Enables (``1``) or disables (``0``) |TS| to return a 400 Bad Request
   if client's request URI includes character which is not RFC 3986 compliant

.. ts:cv:: CONFIG proxy.config.http.errors.log_error_pages INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) the logging of responses to bad requests
   to the error logging destination. Disabling this option prevents error
   responses (such as ``403``\ s) from appearing in the error logs. Any HTTP
   response status codes equal to, or higher, than the minimum code defined by
   :c:data:`TS_HTTP_STATUS_BAD_REQUEST` are affected by this setting.

Parent Proxy Configuration
==========================

.. ts:cv:: CONFIG proxy.config.http.parent_proxy_routing_enable INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) the parent caching option. Refer to :ref:`admin-hierarchical-caching`.

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.retry_time INT 300
   :reloadable:
   :overridable:

   The amount of time allowed between connection retries to a parent cache that is unavailable.

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.fail_threshold INT 10
   :reloadable:
   :overridable:

   The number of times the connection to the parent cache can fail before |TS| considers the parent unavailable.

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.total_connect_attempts INT 4
   :reloadable:
   :overridable:

   The total number of connection attempts for a specific transaction allowed to
   a parent cache before |TS| bypasses the parent or fails the request
   (depending on the ``go_direct`` option in the :file:`parent.config` file). The
   number of parents tried is
   ``proxy.config.http.parent_proxy.fail_threshold / proxy.config.http.parent_proxy.total_connect_attempts``

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.per_parent_connect_attempts INT 2
   :reloadable:
   :overridable:

   The total number of connection attempts allowed per parent for a specific
   transaction, if multiple parents are used.

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.connect_attempts_timeout INT 30
   :reloadable:
   :overridable:

   The timeout value (in seconds) for parent cache connection attempts.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.mark_down_hostdb INT 0
   :reloadable:
   :overridable:

   Enables (``1``) or disables (``0``) marking parent proxies down in hostdb when a connection
   error is detected.  Normally parent selection manages parent proxies and will mark them as unavailable
   as needed.  But when parents are defined in dns with multiple ip addresses, it may be useful to mark the
   failing ip down in hostdb.  In this case you would enable these updates.

.. ts:cv:: CONFIG proxy.config.http.forward.proxy_auth_to_parent INT 0
   :reloadable:
   :overridable:

   Configures |TS| to send proxy authentication headers on to the parent cache.

.. ts:cv:: CONFIG proxy.config.http.no_dns_just_forward_to_parent INT 0
   :reloadable:

   Don't try to resolve DNS, forward all DNS requests to the parent. This is off (``0``) by default.

.. ts:cv:: CONFIG proxy.local.http.parent_proxy.disable_connect_tunneling INT 0

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.self_detect INT 2

   For each host that has been specified in a ``parent`` or ``secondary_parent`` list in the
   :file:`parent.config` file, determine if the host is the same as the current host.
   Obvious examples include ``localhost`` and ``127.0.0.1``. If a match is found,
   take an action depending upon the value below.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disables the feature by not checking for matches.
   ``1`` Remove the matching host from the list.
   ``2`` Mark the host down. This is the default.
   ===== ======================================================================

HTTP Connection Timeouts
========================

.. ts:cv:: CONFIG proxy.config.http.keep_alive_no_activity_timeout_in INT 120
   :reloadable:
   :overridable:

   Specifies how long |TS| keeps connections to clients open for a
   subsequent request after a transaction ends. A value of ``0`` will disable
   the no activity timeout.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.keep_alive_no_activity_timeout_out INT 120
   :reloadable:
   :overridable:

   Specifies how long |TS| keeps connections to origin servers open
   for a subsequent transfer of data after a transaction ends. A value of
   ``0`` will disable the no activity timeout.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.transaction_no_activity_timeout_in INT 30
   :reloadable:
   :overridable:

   Specifies how long |TS| keeps connections to clients open if a
   transaction stalls.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.transaction_no_activity_timeout_out INT 30
   :reloadable:
   :overridable:

   Specifies how long |TS| keeps connections to origin servers open if the transaction stalls.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.websocket.no_activity_timeout INT 600
   :reloadable:
   :overridable:

   Specifies how long |TS| keeps connections open if a websocket stalls.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.websocket.active_timeout INT 3600
   :reloadable:
   :overridable:

   The maximum amount of time |TS| keeps websocket connections open.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.transaction_active_timeout_in INT 900
   :reloadable:
   :overridable:

   The maximum amount of time |TS| can remain connected to a client. If the transfer to the client is not complete before this
   timeout expires, then |TS| closes the connection.

   The value of ``0`` specifies that there is no timeout.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.transaction_active_timeout_out INT 0
   :reloadable:
   :overridable:

   The maximum amount of time |TS| waits for fulfillment of a connection request to an origin server. If |TS| does not
   complete the transfer to the origin server before this timeout expires, then |TS| terminates the connection request.

   The default value of ``0`` specifies that there is no timeout.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.accept_no_activity_timeout INT 120
   :reloadable:

   The timeout interval in seconds before |TS| closes a connection that has no activity.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.background_fill_active_timeout INT 0
   :reloadable:
   :overridable:

   Specifies how long |TS| continues a background fill before giving up and dropping the origin server connection.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.background_fill_completed_threshold FLOAT 0.0
   :reloadable:
   :overridable:

   The proportion of total document size already transferred when a client aborts at which the proxy continues fetching the document
   from the origin server to get it into the cache (a **background fill**).

HTTP Redirection
================

.. ts:cv:: CONFIG proxy.config.http.number_of_redirections INT 0
   :reloadable:
   :overridable:

   This setting determines the maximum number of times Trafficserver does a redirect follow location on receiving a 3XX Redirect response
   for a given client request.

   .. note:: When :ts:cv:`proxy.config.http.number_of_redirections` is set to a positive value and |TS| has previously cached a 3XX Redirect response, the cached response will continue to be refreshed and returned until the response is no longer in the cache.

.. ts:cv:: CONFIG proxy.config.http.redirect_host_no_port INT 1
   :reloadable:

   This setting enables Trafficserver to not include the port in the Host header in the redirect follow request for default/standard ports
   (e.g. 80 for HTTP and 443 for HTTPS). Note that the port is still included in the Host header if it's non-default.

.. ts:cv:: CONFIG proxy.config.http.redirect_use_orig_cache_key INT 0
   :reloadable:
   :overridable:

   This setting enables Trafficserver to allow using original request cache key (for example, set using a TS API) during a 3xx redirect follow.
   The default behavior (0) is to use the URL specified by Location header in the 3xx response as the cache key.

.. ts:cv:: CONFIG proxy.config.http.post_copy_size INT 2048
   :reloadable:

   This setting determines the maximum size in bytes of uploaded content to be
   buffered for HTTP methods such as POST and PUT.

.. ts:cv:: CONFIG proxy.config.http.redirect.actions STRING routable:follow
   :reloadable:

   This setting determines how redirects should be handled. The setting consists
   of a comma-separated list of key-value pairs, where the keys are named IP
   address ranges and the values are actions.

   The following are valid keys:

   ============= ===============================================================
   Key           Description
   ============= ===============================================================
   ``self``      Addresses of the host's interfaces
   ``loopback``  IPv4 ``127.0.0.0/8`` and IPv6 ``::1``
   ``private``   IPv4 ``10.0.0.0/8`` ``100.64.0.0/10`` ``172.16.0.0/12`` ``192.168.0.0/16`` and IPv6 ``fc00::/7``
   ``multicast`` IPv4 ``224.0.0.0/4`` and IPv6 ``ff00::/8``
   ``linklocal`` IPv4 ``169.254.0.0/16`` and IPv6 ``fe80::/10``
   ``routable``  All publicly routable addresses
   ``default``   All address ranges not configured specifically
   ============= ===============================================================

   The following are valid values:

   ========== ==================================================================
   Value      Description
   ========== ==================================================================
   ``return`` Do not process the redirect, send it as the proxy response.
   ``reject`` Do not process the redirect, send a 403 as the proxy response.
   ``follow`` Internally follow the redirect up to :ts:cv:`proxy.config.http.number_of_redirections`. **Use this setting with caution!**
   ========== ==================================================================

   .. warning:: Following a redirect to other than ``routable`` addresses can be
      dangerous, as it allows the controller of an origin to arrange a probe the
      |TS| host. Enabling these redirects makes |TS| open to third party attacks
      and probing and therefore should be considered only in known safe
      environments.

   For example, a setting of
   ``loopback:reject,private:reject,routable:follow,default:return`` would send
   ``403`` as the proxy response to loopback and private addresses, routable
   addresses would be followed up to
   :ts:cv:`proxy.config.http.number_of_redirections`, and redirects to all other
   ranges will be sent as the proxy response.

   The action for ``self`` has the highest priority when an address would match
   multiple keys, and the action for ``default`` has the lowest priority. Other
   keys represent disjoint sets of addresses that will not conflict. If
   duplicate keys are present in the setting, the right-most key-value pair is
   used.

   The default value is ``routable:follow``, which means "follow routable
   redirects, return all other redirects". Note that
   :ts:cv:`proxy.config.http.number_of_redirections` must be positive also,
   otherwise redirects will be returned rather than followed.

Origin Server Connect Attempts
==============================

.. ts:cv:: CONFIG proxy.config.http.connect_attempts_max_retries INT 3
   :reloadable:
   :overridable:

   The maximum number of connection retries |TS| can make when the origin server is not responding.
   Each retry attempt lasts for `proxy.config.http.connect_attempts_timeout`_ seconds.  Once the maximum number of retries is
   reached, the origin is marked dead.  After this, the setting  `proxy.config.http.connect_attempts_max_retries_dead_server`_
   is used to limit the number of retry attempts to the known dead origin.

.. ts:cv:: CONFIG proxy.config.http.connect_attempts_max_retries_dead_server INT 1
   :reloadable:
   :overridable:

   Maximum number of connection retries |TS| can make while an origin is marked dead.  Typically this value is smaller than
   `proxy.config.http.connect_attempts_max_retries`_ so an error is returned to the client faster and also to reduce the load on the dead origin.
   The timeout interval `proxy.config.http.connect_attempts_timeout`_ in seconds is used with this setting.

.. ts:cv:: CONFIG proxy.config.http.server_max_connections INT 0
   :reloadable:

   Limits the number of socket connections across all origin servers to the
   value specified. To disable, set to zero (``0``).

   This value is used in determining when and if to prune active origin
   sessions. Without this value set, connections to origins can consume all the
   way up to ts:cv:`proxy.config.net.connections_throttle` connections, which
   in turn can starve incoming requests from available connections.

.. ts:cv:: CONFIG proxy.config.http.per_server.connection.max INT 0
   :reloadable:
   :overridable:

   Set a limit for the number of concurrent connections to an upstream server group. A value of
   ``0`` disables checking. If a transaction attempts to connect to a group which already has the
   maximum number of concurrent connections the transaction either rechecks after a delay or a 503
   (``HTTP_STATUS_SERVICE_UNAVAILABLE``) error response is sent to the user agent. To configure

   Number of transactions that can be delayed concurrently
      See :ts:cv:`proxy.config.http.per_server.connection.queue_size`.

   How long to delay before rechecking
      See :ts:cv:`proxy.config.http.per_server.connection.queue_delay`.

   Upstream server group definition
      See :ts:cv:`proxy.config.http.per_server.connection.match`.

   Frequency of alerts
      See :ts:cv:`proxy.config.http.per_server.connection.alert_delay`.

.. ts:cv:: CONFIG proxy.config.http.per_server.connection.match STRING ip
   :reloadable:
   :overridable:

   Control the definition of an upstream server group for
   :ts:cv:`proxy.config.http.per_server.connection.max`. This must be one of the following keywords.

   ip
      Group by IP address. Each IP address is a group.

   port
      Group by IP address and port. Each distinct IP address and port pair is a group.

   host
      Group by host name. The host name is the post remap FQDN used to resolve the upstream
      address.

   both
      Group by IP address, port, and host name. Each distinct combination is a group.

   To disable upstream server grouping, set :ts:cv:`proxy.config.http.per_server.connection.max` to ``0``.

.. ts:cv:: CONFIG proxy.config.http.per_server.connection.queue_size INT 0
   :reloadable:

   Controls the number of transactions that can be waiting on an upstream server group.

   ``-1``
      Unlimited.

   ``0``
      Never wait. If the connection maximum has been reached immediately respond with an error.

   A positive number
      If there are less than this many waiting transactions, delay this transaction and try again. Otherwise respond immediately with an error.

.. ts:cv:: CONFIG proxy.config.http.per_server.connection.queue_delay INT 100
   :reloadable:
   :units: milliseconds

   If a transaction is delayed due to too many connections in an upstream server group, delay this amount of time before checking again.

.. ts:cv:: CONFIG proxy.config.http.per_server.connection.alert_delay INT 60
   :reloadable:
   :units: seconds

   Throttle alerts per upstream server group to be no more often than this many seconds. Summary
   data is provided per alert to allow log scrubbing to generate accurate data.

.. ts:cv:: CONFIG proxy.config.http.per_server.min_keep_alive_connections INT 0
   :reloadable:

   Set a target for the minimum number of active connections to an upstream server group. When an
   outbound connection is in keep alive state and the inactivity timer expires, if there are fewer
   than this many connections in the group a new connection the timer is reset instead of closing
   the connection. Useful when the origin supports keep-alive, removing the time needed to set up a
   new connection from the next request at the expense of added (inactive) connections.

.. ts:cv:: CONFIG proxy.config.http.connect_attempts_rr_retries INT 3
   :reloadable:
   :overridable:

   The maximum number of failed connection attempts allowed before a round-robin entry is marked as 'down' if a server
   has round-robin DNS entries.

.. ts:cv:: CONFIG proxy.config.http.connect_attempts_timeout INT 30
   :reloadable:
   :overridable:

   The timeout value (in seconds) for time to first byte for an origin server
   connection.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.post_connect_attempts_timeout INT 1800
   :reloadable:
   :overridable:

   The timeout value (in seconds) for an origin server connection when the client request is a ``POST`` or ``PUT``
   request.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.post.check.content_length.enabled INT 1

    Enables (``1``) or disables (``0``) checking the Content-Length: Header for a POST request.

.. ts:cv:: CONFIG proxy.config.http.down_server.cache_time INT 60
   :reloadable:
   :overridable:

   Specifies how long (in seconds) |TS| remembers that an origin server was unreachable.

.. ts:cv:: CONFIG proxy.config.http.down_server.abort_threshold INT 10
   :reloadable:
   :overridable:

   The number of seconds before |TS| marks an origin server as unavailable after a client abandons a request
   because the origin server was too slow in sending the response header.

.. ts:cv:: CONFIG proxy.config.http.uncacheable_requests_bypass_parent INT 1
   :reloadable:
   :overridable:

   When enabled (1), |TS| bypasses the parent proxy for a request that is not cacheable.

Congestion Control
==================

.. ts:cv:: CONFIG proxy.config.http.flow_control.enabled INT 0
   :overridable:

   Transaction buffering / flow control is enabled if this is set to a non-zero value. Otherwise no flow control is done.

.. ts:cv:: CONFIG proxy.config.http.flow_control.high_water INT 0
   :units: bytes
   :overridable:

   The high water mark for transaction buffer control. External source I/O is halted when the total buffer space in use
   by the transaction exceeds this value.

.. ts:cv:: CONFIG proxy.config.http.flow_control.low_water INT 0
   :units: bytes
   :overridable:

   The low water mark for transaction buffer control. External source I/O is resumed when the total buffer space in use
   by the transaction is no more than this value.

.. ts:cv:: CONFIG proxy.config.http.websocket.max_number_of_connections INT -1
   :reloadable:

   When enabled >= (``0``), |TS| will enforce a maximum number of simultaneous websocket connections.

.. _admin-negative-response-caching:

Negative Response Caching
=========================

.. ts:cv:: CONFIG proxy.config.http.negative_caching_enabled INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| caches negative responses (such as ``404 Not Found``)
   when a requested page does not exist. The next time a client requests the
   same page, |TS| serves the negative response directly from cache.

   When disabled (``0``), |TS| will only cache the response if the response has
   ``Cache-Control`` headers.

   The following negative responses are cached by |TS| by default:

   ====================== =====================================================
   HTTP Response Code     Description
   ====================== =====================================================
   ``204``                No Content
   ``305``                Use Proxy
   ``400``                Bad Request
   ``403``                Forbidden
   ``404``                Not Found
   ``414``                URI Too Long
   ``405``                Method Not Allowed
   ``500``                Internal Server Error
   ``501``                Not Implemented
   ``502``                Bad Gateway
   ``503``                Service Unavailable
   ``504``                Gateway Timeout
   ====================== =====================================================

   The cache lifetime for objects cached from this setting is controlled via
   :ts:cv:`proxy.config.http.negative_caching_lifetime`.

.. ts:cv:: CONFIG proxy.config.http.negative_caching_lifetime INT 1800
   :reloadable:
   :overridable:

   How long (in seconds) |TS| keeps the negative responses  valid in cache. This value only affects negative
   responses that do NOT have explicit ``Expires:`` or ``Cache-Control:`` lifetimes set by the server.

.. ts:cv:: CONFIG proxy.config.http.negative_caching_list STRING 204 305 403 404 405 414 500 501 502 503 504
   :reloadable:

   The HTTP status code for negative caching. Default values are mentioned above. The unwanted status codes can be
   taken out from the list. Other status codes can be added. The variable is a list but parsed as STRING.

.. ts:cv:: CONFIG proxy.config.http.negative_revalidating_enabled INT 1
   :reloadable:
   :overridable:

   Negative revalidating allows |TS| to return stale content if revalidation to the origin fails due
   to network or HTTP errors. If it is enabled, rather than caching the negative response, the
   current stale content is preserved and served. Note this is considered only on a revalidation of
   already cached content. A revalidation failure means a connection failure or a 50x response code.

   A value of ``0`` disables serving stale content and a value of ``1`` enables keeping and serving stale content if revalidation fails.

.. ts:cv:: CONFIG proxy.config.http.negative_revalidating_lifetime INT 1800

   How long, in seconds, to consider a stale cached document valid if If
   :ts:cv:`proxy.config.http.negative_revalidating_enabled` is enabled and |TS| receives a negative
   (``5xx`` only) response from the origin server during revalidation.

Proxy User Variables
====================

.. ts:cv:: CONFIG proxy.config.http.anonymize_remove_from INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| removes the ``From`` header to protect the privacy of your users.

.. ts:cv:: CONFIG proxy.config.http.anonymize_remove_referer INT 0
   :reloadable:

   When enabled (``1``), |TS| removes the ``Referrer`` header to protect the privacy of your site and users.

.. ts:cv:: CONFIG proxy.config.http.anonymize_remove_user_agent INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| removes the ``User-agent`` header to protect the privacy of your site and users.

.. ts:cv:: CONFIG proxy.config.http.anonymize_remove_cookie INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| removes the ``Cookie`` header to protect the privacy of your site and users.

.. ts:cv:: CONFIG proxy.config.http.anonymize_remove_client_ip INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| removes ``Client-IP`` headers for more privacy.

.. ts:cv:: CONFIG proxy.config.http.insert_client_ip INT 1
   :reloadable:
   :overridable:

   Specifies whether |TS| inserts ``Client-IP`` headers to retain the client IP address:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Don't insert the ``Client-ip`` header
   ``1`` Insert the ``Client-ip`` header, but only if the UA did not send one
   ``2`` Always insert the ``Client-ip`` header
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.http.anonymize_other_header_list STRING NULL
   :reloadable:

   Comma separated list of headers |TS| should remove from outgoing requests.

.. ts:cv:: CONFIG proxy.config.http.insert_squid_x_forwarded_for INT 1
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| adds the client IP address to the ``X-Forwarded-For`` header.

.. ts:cv:: CONFIG proxy.config.http.insert_forwarded STRING none
   :reloadable:
   :overridable:

   The default value (``none``) means that |TS| does not insert or append information to any
   ``Forwarded`` header (described in IETF RFC 7239) in the request message.  To put information into a
   ``Forwarded`` header in the request, the value of this variable must be a list of the ``Forwarded``
   parameters to be inserted.

   ==================  ===============================================================
   Parameter           Value of parameter place in outgoing Forwarded header
   ==================  ===============================================================
   for                 Client IP address
   by=ip               Proxy IP address
   by=unknown          The literal string ``unknown``
   by=servername       Proxy server name
   by=uuid             Server UUID prefixed with ``_``
   proto               Protocol of incoming request
   host                The host specified in the incoming request
   connection=compact  Connection with basic transaction codes.
   connection=std      Connection with detailed transaction codes.
   connection=full     Full user agent connection :ref:`protocol tags <protocol_tags>`
   ==================  ===============================================================

   Each paramater in the list must be separated by ``|`` or ``:``.  For example, ``for|by=uuid|proto`` is
   a valid value for this variable.  Note that the ``connection`` parameter is a non-standard extension to
   RFC 7239.  Also note that, while |TS| allows multiple ``by`` parameters for the same proxy, this
   is prohibited by RFC 7239. Currently, for the ``host`` parameter to provide the original host from the
   incoming client request, `proxy.config.url_remap.pristine_host_hdr`_ must be enabled.

.. ts:cv:: CONFIG proxy.config.http.normalize_ae INT 1
   :reloadable:
   :overridable:

   Specifies normalization, if any, of ``Accept-Encoding:`` headers.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` No normalization.
   ``1`` ``Accept-Encoding: gzip`` (if the header has ``gzip`` or ``x-gzip`` with any ``q``) **OR**
         *blank* (for any header that does not include ``gzip``)
   ``2`` ``Accept-Encoding: br`` if the header has ``br`` (with any ``q``) **ELSE**
         normalize as for value ``1``
   ===== ======================================================================

   This is useful for minimizing cached alternates of documents (e.g. ``gzip, deflate`` vs. ``deflate, gzip``).
   Enabling this option is recommended if your origin servers use no encodings other than ``gzip`` or ``br`` (Brotli).

Security
========

.. ts:cv:: CONFIG proxy.config.http.push_method_enabled INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) the HTTP ``PUSH`` option, which allows you to deliver content directly to the cache without a user
   request.

   .. important::

       If you enable this option, then you must also specify
       a filtering rule in the ip_allow.config file to allow only certain
       machines to push content into the cache.

.. ts:cv:: CONFIG proxy.config.http.max_post_size INT 0
   :reloadable:

   This feature is disabled by default with a value of (``0``), any positive
   value will limit the size of post bodies. If a request is received with a
   post body larger than this limit the response will be terminated with
   413 - Request Entity Too Large and logged accordingly.

.. ts:cv:: CONFIG proxy.config.http.allow_multi_range INT 0
   :reloadable:
   :overridable:

   This option allows the administrator to configure different behavior and
   handling of requests with multiple ranges in the ``Range`` header.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Do not allow multiple ranges, effectively ignoring the ``Range`` header
   ``1`` Allows multiple ranges. This can be potentially dangerous since well
         formed requests can cause excessive resource consumption on the server.
   ``2`` Similar to 0, except return a 416 error code and no response body.
   ===== ======================================================================

Cache Control
=============

.. ts:cv:: CONFIG proxy.config.cache.enable_read_while_writer INT 1
   :reloadable:

   Specifies when to enable the ability to read a cached object while another
   connection is completing the write to cache for that same object. The goal
   here is to avoid multiple origin connections for the same cacheable object
   upon a cache miss. The possible values of this config are:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Never read while writing.
   ``1`` Always read while writing.
   ``2`` Always read while writing, but allow non-cached ``Range`` requests
         through to the origin server.
   ===== ======================================================================

   The ``2`` option is useful to avoid delaying requests which can not easily
   be satisfied by the partially written response.

   Several other configuration values need to be set for this to be
   usable. See :ref:`admin-configuration-reducing-origin-requests`.

.. ts:cv:: CONFIG proxy.config.cache.read_while_writer.max_retries INT 10
   :reloadable:

   Specifies how many retries trafficserver attempts to trigger read_while_writer on failing
   to obtain the write VC mutex or until the first fragment is downloaded for the
   object being downloaded. The retry duration is specified using the setting
   :ts:cv:`proxy.config.cache.read_while_writer_retry.delay`

.. ts:cv:: CONFIG proxy.config.cache.read_while_writer_retry.delay INT 50
   :reloadable:

   Specifies the delay in msec, trafficserver waits to reattempt read_while_writer
   on failing to obtain the write VC mutex or until the first fragment is downloaded
   for the object being downloaded. Note that trafficserver implements a progressive
   delay in reattempting, by doubling the configured duration from the third reattempt
   onwards.

.. ts:cv:: CONFIG proxy.config.cache.force_sector_size INT 0
   :reloadable:

   Forces the use of a specific hardware sector size, e.g. 4096, for all disks.

   SSDs and "advanced format” drives claim a sector size of 512; however, it is safe to force a higher
   size than the hardware supports natively as we count atomicity in 512 byte increments.

   4096-sized drives formatted for Windows will have partitions aligned on 63 512-byte sector boundaries,
   so they will be unaligned. There are workarounds, but you need to do some research on your particular
   drive. Some drives have a one-time option to switch the partition boundary, while others might require
   reformatting or repartitioning.

   To be safe in Linux, you could just use the entire drive: ``/dev/sdb`` instead of ``/dev/sdb1`` and
   |TS| will do the right thing. Misaligned partitions on Linux are auto-detected.

   For example: If ``/sys/block/sda/sda1/alignment_offset`` is non-zero, ATS will offset reads/writes to
   that disk by that alignment. If Linux knows about any existing partition misalignments, ATS will compensate.

   Partitions formatted to support hardware sector size of more than 512 (e.g. 4096) will result in all
   objects stored in the cache to be integral multiples of 4096 bytes, which will result in some waste for
   small files.

.. ts:cv:: CONFIG proxy.config.http.cache.http INT 1
   :reloadable:
   :overridable:

   Enables (``1``) or disables (``0``) caching of HTTP requests.

.. ts:cv:: CONFIG proxy.config.http.cache.generation INT -1
   :reloadable:
   :overridable:

   If set to a value other than ``-1``, the value if this configuration
   option is combined with the cache key at cache lookup time.
   Changing this value has the effect of an instantaneous, zero-cost
   cache purge since it will cause all subsequent cache keys to
   change. Since this is an overrideable configuration, it can be
   used to purge the entire cache, or just a specific :file:`remap.config`
   rule.

.. ts:cv:: CONFIG proxy.config.http.cache.allow_empty_doc INT 1
   :reloadable:
   :deprecated:

   Enables (``1``) or disables (``0``) caching objects that have an empty
   response body. This is particularly useful for caching 301 or 302 responses
   with a ``Location`` header but no document body. This only works if the
   origin response also has a ``Content-Length`` header.

.. ts:cv:: CONFIG proxy.config.http.doc_in_cache_skip_dns INT 1
   :reloadable:
   :overridable:

   When enabled (``1``), do not perform origin server DNS resolution if a fresh
   copy of the requested document is available in the cache. This setting has
   no effect if HTTP caching is disabled or if there are IP based ACLs
   configured.

   Note that plugins, particularly authorization plugins, which use the
   :c:data:`TS_HTTP_OS_DNS_HOOK` hook may require this configuration variable
   to be disabled (``0``) in order to function properly. This will ensure that
   the hook will be evaluated and plugin execution will occur even when there
   is a fresh copy of the requested object in the cache (which would normally
   allow the DNS lookup to be skipped, thus eliminating the hook evaluation).

   The downside is that the performance gain by skipping otherwise unnecessary
   DNS lookups is lost. Because the variable is overridable, you may retain
   this performance benefit for portions of your cache which do not require the
   use of :c:data:`TS_HTTP_OS_DNS_HOOK` plugins, by ensuring that the setting
   is first disabled within only the relevant transactions. Refer to the
   documentation on :ref:`admin-plugins-conf-remap` for more information.

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_client_no_cache INT 1
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| ignores client requests to bypass the cache.

.. ts:cv:: CONFIG proxy.config.http.cache.ims_on_client_no_cache INT 1
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| issues a conditional request to the origin server if an incoming request has a ``No-Cache`` header.

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_server_no_cache INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| ignores origin server requests to bypass the cache.

.. ts:cv:: CONFIG proxy.config.http.cache.cache_responses_to_cookies INT 1
   :reloadable:
   :overridable:

   Specifies how cookies are cached:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Do not cache any responses to cookies.
   ``1`` Cache for any content-type.
   ``2`` Cache only for image types.
   ``3`` Cache for all but text content-types.
   ``4`` Cache for all but text content-types; except origin server response
         without ``Set-Cookie`` or with ``Cache-Control: public``.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_authentication INT 0
   :overridable:

   When enabled (``1``), |TS| ignores ``WWW-Authentication`` headers in responses ``WWW-Authentication`` headers are removed and
   not cached.

.. ts:cv:: CONFIG proxy.config.http.cache.cache_urls_that_look_dynamic INT 1
   :reloadable:
   :overridable:

   Enables (``1``) or disables (``0``) caching of URLs that look dynamic, i.e.: URLs that end in ``.asp`` or contain a question
   mark (``?``), a semicolon (``;``), or ``cgi``. For a full list, please refer to
   `HttpTransact::url_looks_dynamic </link/to/doxygen>`_

.. ts:cv:: CONFIG proxy.config.http.cache.enable_default_vary_headers INT 0
   :reloadable:
   :overridable:

   Enables (``1``) or disables (``0``) caching of alternate versions of HTTP objects that do not contain the ``Vary`` header.

.. ts:cv:: CONFIG proxy.config.http.cache.when_to_revalidate INT 0
   :reloadable:
   :overridable:

   Specifies when to revalidate content:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Use cache directives or heuristic (the default value).
   ``1`` Stale if heuristic.
   ``2`` Always stale (always revalidate).
   ``3`` Never stale.
   ``4`` Use cache directives or heuristic (0) unless the request has an
         ``If-Modified-Since`` header.
   ===== ======================================================================

   If the request contains the ``If-Modified-Since`` header, then |TS| always
   revalidates the cached content and uses the client's ``If-Modified-Since``
   header for the proxy request.

.. ts:cv:: CONFIG proxy.config.http.cache.required_headers INT 2
   :reloadable:
   :overridable:

   The type of headers required in a request for the request to be cacheable.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` No headers required to make document cacheable.
   ``1`` Either the ``Last-Modified`` header, or an explicit lifetime header
         (``Expires`` or ``Cache-Control: max-age``) is required.
   ``2`` Explicit lifetime is required, from either ``Expires`` or
         ``Cache-Control: max-age``.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.http.cache.max_stale_age INT 604800
   :reloadable:
   :overridable:

   The maximum age allowed for a stale response before it cannot be cached.

.. ts:cv:: CONFIG proxy.config.http.cache.range.lookup INT 1
   :overridable:

   When enabled (``1``), |TS| looks up range requests in the cache.

.. ts:cv:: CONFIG proxy.config.http.cache.range.write INT 0
   :overridable:

   When enabled (``1``), |TS| will attempt to write (lock) the URL
   to cache. This is rarely useful (at the moment), since it'll only be able
   to write to cache if the origin has ignored the ``Range:`` header. For a use
   case where you know the origin will respond with a full (``200``) response,
   you can turn this on to allow it to be cached.

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_accept_mismatch INT 2
   :reloadable:
   :overridable:

   When enabled with a value of ``1``, |TS| serves documents from cache with a
   ``Content-Type:`` header even if it does not match the ``Accept:`` header of the
   request. If set to ``2`` (default), this logic only happens in the absence of a
   ``Vary`` header in the cached response (which is the recommended and safe use).

.. note::

   This option should only be enabled with ``1`` if you're having
   problems with caching *and* you origin server doesn't set the ``Vary``
   header. Alternatively, if the origin is incorrectly setting
   ``Vary: Accept`` or doesn't respond with ``406 (Not Acceptable)``,
   you can also enable this configuration with a ``1``.

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_accept_language_mismatch INT 2
   :reloadable:
   :overridable:

   When enabled with a value of ``1``, |TS| serves documents from cache with a
   ``Content-Language:`` header even if it does not match the ``Accept-Language:``
   header of the request. If set to ``2`` (default), this logic only happens in the absence of a
   ``Vary`` header in the cached response (which is the recommended and safe use).

.. note::

   This option should only be enabled with ``1`` if you're having
   problems with caching *and* you origin server doesn't set the ``Vary``
   header. Alternatively, if the origin is incorrectly setting
   ``Vary: Accept-Language`` or doesn't respond with ``406 (Not Acceptable)``,
   you can also enable this configuration with a ``1``.

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_accept_encoding_mismatch INT 2
   :reloadable:
   :overridable:

   When enabled with a value of ``1``, |TS| serves documents from cache with a
   ``Content-Encoding:`` header even if it does not match the ``Accept-Encoding:``
   header of the request. If set to ``2`` (default), this logic only happens in the absence of a
   ``Vary`` header in the cached response (which is the recommended and safe use).

.. note::

   This option should only be enabled with ``1`` if you're having
   problems with caching *and* you origin server doesn't set the ``Vary``
   header. Alternatively, if the origin is incorrectly setting
   ``Vary: Accept-Encoding`` or doesn't respond with ``406 (Not Acceptable)``
   you can also enable this configuration with a ``1``.

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_accept_charset_mismatch INT 2
   :reloadable:
   :overridable:

   When enabled with a value of ``1``, |TS| serves documents from cache with a
   ``Content-Type:`` header even if it does not match the ``Accept-Charset:`` header
   of the request. If set to ``2`` (default), this logic only happens in the absence of a
   ``Vary`` header in the cached response (which is the recommended and safe use).

.. note::

   This option should only be enabled with ``1`` if you're having
   problems with caching *and* you origin server doesn't set the ``Vary``
   header. Alternatively, if the origin is incorrectly setting
   ``Vary: Accept-Charset`` or doesn't respond with ``406 (Not Acceptable)``,
   you can also enable this configuration with a ``1``.

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_client_cc_max_age INT 1
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| ignores any ``Cache-Control:
   max-age`` headers from the client. This technically violates the HTTP RFC,
   but avoids a problem where a client can forcefully invalidate a cached object.

.. ts:cv:: CONFIG proxy.config.cache.max_doc_size INT 0

   Specifies the maximum object size that will be cached. ``0`` is unlimited.

.. ts:cv:: CONFIG proxy.config.cache.min_average_object_size INT 8000

   Specifies the lower boundary of average object sizes in the cache and is
   used in determining the number of :term:`directory buckets <directory bucket>`
   to allocate for the in-memory cache directory.

.. ts:cv:: CONFIG proxy.config.cache.permit.pinning INT 0
   :reloadable:

   When enabled (``1``), |TS| will keep certain HTTP objects in the cache for a certain time as specified in cache.config.

.. ts:cv:: CONFIG proxy.config.cache.hit_evacuate_percent INT 0

   The size of the region (as a percentage of the total content storage in a :term:`cache stripe`) in front of the
   :term:`write cursor` that constitutes a recent access hit for evacutating the accessed object.

   When an object is accessed it can be marked for evacuation, that is to be copied over the write cursor and
   thereby preserved from being overwritten. This is done if it is no more than a specific number of bytes in front of
   the write cursor. The number of bytes is a percentage of the total number of bytes of content storage in the cache
   stripe where the object is stored and that percentage is set by this variable.

   By default, the feature is off (set to 0).

.. ts:cv:: CONFIG proxy.config.cache.hit_evacuate_size_limit INT 0
   :units: bytes

   Limit the size of objects that are hit evacuated.

   Objects larger than the limit are not hit evacuated. A value of 0 disables the limit.

.. ts:cv:: CONFIG proxy.config.cache.limits.http.max_alts INT 5

   The maximum number of alternates that are allowed for any given URL.
   Disable by setting to 0.

.. ts:cv:: CONFIG proxy.config.cache.target_fragment_size INT 1048576

   Sets the target size of a contiguous fragment of a file in the disk cache.
   When setting this, consider that larger numbers could waste memory on slow
   connections, but smaller numbers could increase (waste) seeks.

.. ts:cv:: CONFIG proxy.config.cache.alt_rewrite_max_size INT 4096

   Configures the size, in bytes, of an alternate that will be considered
   small enough to trigger a rewrite of the resident alt fragment within a
   write vector. For further details on cache write vectors, refer to the
   developer documentation for :cpp:class:`CacheVC`.

RAM Cache
=========

.. ts:cv:: CONFIG proxy.config.cache.ram_cache.size INT -1

   By default the RAM cache size is automatically determined, based on
   disk cache size; approximately 10 MB of RAM cache per GB of disk cache.
   Alternatively, it can be set to a fixed value such as
   **20GB** (21474836480)

.. ts:cv:: CONFIG proxy.config.cache.ram_cache_cutoff INT 4194304

   Objects greater than this size will not be kept in the RAM cache.
   This should be set high enough to keep objects accessed frequently
   in memory in order to improve performance.
   **4MB** (4194304)

.. ts:cv:: CONFIG proxy.config.cache.ram_cache.algorithm INT 1

   Two distinct RAM caches are supported, the default (0) being the **CLFUS**
   (*Clocked Least Frequently Used by Size*). As an alternative, a simpler
   **LRU** (*Least Recently Used*) cache is also available, by changing this
   configuration to 1.

.. ts:cv:: CONFIG proxy.config.cache.ram_cache.use_seen_filter INT 1

   Enabling this option will filter inserts into the RAM cache to ensure that
   they have been seen at least once.  For the **LRU**, this provides scan
   resistance. Note that **CLFUS** already requires that a document have history
   before it is inserted, so for **CLFUS**, setting this option means that a
   document must be seen three times before it is added to the RAM cache.

.. ts:cv:: CONFIG proxy.config.cache.ram_cache.compress INT 0

   The **CLFUS** RAM cache also supports an optional in-memory compression.
   This is not to be confused with ``Content-Encoding: gzip`` compression.
   The RAM cache compression is intended to try to save space in the RAM,
   and is not visible to the User-Agent (client).

   Possible values are:

   ======== ===================================================================
   Value    Description
   ======== ===================================================================
   ``0``    No compression
   ``1``    Fastlz (extremely fast, relatively low compression)
   ``2``    Libz (moderate speed, reasonable compression)
   ``3``    Liblzma (very slow, high compression)
   ======== ===================================================================

   Compression runs on task threads. To use more cores for RAM cache
   compression, increase :ts:cv:`proxy.config.task_threads`.

.. _admin-heuristic-expiration:

Heuristic Expiration
====================

.. ts:cv:: CONFIG proxy.config.http.cache.heuristic_min_lifetime INT 3600
   :reloadable:
   :overridable:

   The minimum amount of time, in seconds, an HTTP object without an expiration
   date can remain fresh in the cache before is considered to be stale.

.. ts:cv:: CONFIG proxy.config.http.cache.heuristic_max_lifetime INT 86400
   :reloadable:
   :overridable:

   The maximum amount of time, in seconds, an HTTP object without an expiration
   date can remain fresh in the cache before is considered to be stale.

.. ts:cv:: CONFIG proxy.config.http.cache.heuristic_lm_factor FLOAT 0.10
   :reloadable:
   :overridable:

   The aging factor for freshness computations. |TS| stores an object for this
   percentage of the time that elapsed since it last changed.

.. ts:cv:: CONFIG proxy.config.http.cache.guaranteed_min_lifetime INT 0
   :reloadable:
   :overridable:

   Establishes a guaranteed minimum lifetime boundary for freshness heuristics.
   When heuristics are used, and the :ts:cv:`proxy.config.http.cache.heuristic_lm_factor`
   aging factor is applied, the final minimum age calculated will never be
   lower than the value in this variable.

.. ts:cv:: CONFIG proxy.config.http.cache.guaranteed_max_lifetime INT 31536000
   :reloadable:
   :overridable:

   Establishes a guaranteed maximum lifetime boundary for freshness heuristics.
   When heuristics are used, and the :ts:cv:`proxy.config.http.cache.heuristic_lm_factor`
   aging factor is applied, the final maximum age calculated will never be
   higher than the value in this variable.

.. ts:cv:: CONFIG proxy.config.http.cache.fuzz.time INT 0
   :deprecated:
   :reloadable:
   :overridable:

   How often |TS| checks for an early refresh, during the period before the
   document stale time. The interval specified must be in seconds.

.. note::

   Previous versions of Apache |TS| defaulted this to 240s. This
   feature is deprecated as of ATS v6.2.0.

.. ts:cv:: CONFIG proxy.config.http.cache.fuzz.probability FLOAT 0.0
   :deprecated:
   :reloadable:
   :overridable:

   The probability that a refresh is made on a document during the fuzz time
   specified in :ts:cv:`proxy.config.http.cache.fuzz.time`.

.. note::

   Previous versions of Apache |TS| defaulted this to 0.005 (0.5%).
   This feature is deprecated as of ATS v6.2.0

.. ts:cv:: CONFIG proxy.config.http.cache.fuzz.min_time INT 0
   :deprecated:
   :reloadable:
   :overridable:

   Handles requests with a TTL less than :ts:cv:`proxy.config.http.cache.fuzz.time`.
   It allows for different times to evaluate the probability of revalidation
   for small TTLs and big TTLs. Objects with small TTLs will start "rolling the
   revalidation dice" near the ``fuzz.min_time``, while objects with large TTLs
   would start at ``fuzz.time``. A logarithmic-like function between determines
   the revalidation evaluation start time (which will be between
   ``fuzz.min_time`` and ``fuzz.time``). As the object gets closer to expiring,
   the window start becomes more likely. By default this setting is not enabled,
   but should be enabled any time you have objects with small TTLs.

.. note::

    These fuzzing options are marked as deprecated as of v6.2.0, and will be
    removed for v7.0.0. Instead, we recommend looking at the new
    :ts:cv:`proxy.config.http.cache.open_write_fail_action` configuration and
    the features around thundering heard avoidance (see
    :ref:`http-proxy-caching` for details).

Dynamic Content & Content Negotiation
=====================================

.. ts:cv:: CONFIG proxy.config.http.cache.vary_default_text STRING NULL
   :reloadable:
   :overridable:

   The header on which |TS| varies for text documents.

For example: if you specify ``User-agent``, then |TS| caches
all the different user-agent versions of documents it encounters.

.. ts:cv:: CONFIG proxy.config.http.cache.vary_default_images STRING NULL
   :reloadable:
   :overridable:

   The header on which |TS| varies for images.

.. ts:cv:: CONFIG proxy.config.http.cache.vary_default_other STRING NULL
   :reloadable:
   :overridable:

   The header on which |TS| varies for anything other than text and images.

.. ts:cv:: CONFIG proxy.config.http.cache.open_read_retry_time INT 10
   :reloadable:

    The number of milliseconds a cacheable request will wait before requesting the object from cache if an equivalent request is in flight.

.. ts:cv:: CONFIG proxy.config.http.cache.max_open_read_retries INT -1
   :reloadable:
   :overridable:

    The number of times to attempt fetching an object from cache if there was an equivalent request in flight.

.. ts:cv:: CONFIG proxy.config.http.cache.max_open_write_retries INT 1
   :reloadable:
   :overridable:

    The number of times to attempt a cache open write upon failure to get a write lock.

.. ts:cv:: CONFIG proxy.config.http.cache.open_write_fail_action INT 0
   :reloadable:
   :overridable:

    This setting indicates the action taken on failing to obtain the cache open write lock on either a cache miss or a cache
    hit stale. This typically happens when there is more than one request to the same cache object simultaneously. During such
    a scenario, all but one (which goes to the origin) request is served either a stale copy or an error depending on this
    setting.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Default. Disable cache and go to origin server.
   ``1`` Return a ``502`` error on a cache miss.
   ``2`` Serve stale if object's age is under
         :ts:cv:`proxy.config.http.cache.max_stale_age`. Otherwise, go to
         origin server.
   ``3`` Return a ``502`` error on a cache miss or serve stale on a cache
         revalidate if object's age is under
         :ts:cv:`proxy.config.http.cache.max_stale_age`. Otherwise, go to
         origin server.
   ``4`` Return a ``502`` error on either a cache miss or on a revalidation.
   ===== ======================================================================

Customizable User Response Pages
================================

.. ts:cv:: CONFIG proxy.config.body_factory.enable_customizations INT 1

   Specifies whether customizable response pages are language specific
   or not:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``1`` Enable customizable user response pages in the default directory only.
   ``2`` Enable language-targeted user response pages.
   ``3`` Enable host-targeted user response pages.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.body_factory.enable_logging INT 0

   Enables (``1``) or disables (``0``) logging for customizable response pages. When enabled, |TS| records a message in
   the error log each time a customized response page is used or modified.

.. ts:cv:: CONFIG proxy.config.body_factory.template_sets_dir STRING etc/trafficserver/body_factory

   The customizable response page default directory. If this is a
   relative path, |TS| resolves it relative to the
   ``PREFIX`` directory.

.. ts:cv:: CONFIG proxy.config.body_factory.template_base STRING ""
    :reloadable:
    :overridable:

    A prefix for the file name to use to find an error template file. If set (not the empty string)
    this value and an underscore are predended to the file name to find in the template sets
    directory. See :ref:`body-factory`.

.. ts:cv:: CONFIG proxy.config.body_factory.response_max_size INT 8192
    :reloadable:

    Maximum size of the error template response page.

.. ts:cv:: CONFIG proxy.config.body_factory.response_suppression_mode INT 0

   Specifies when |TS| suppresses generated response pages:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Never suppress generated response pages.
   ``1`` Always suppress generated response pages.
   ``2`` Suppress response pages only for intercepted traffic.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.http_ui_enabled INT 0

   Specifies which http Inspector UI endpoints to allow within :file:`remap.config`:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disable all http UI endpoints.
   ``1`` Enable only Cache Inspector endpoints.
   ``2`` Enable only stats endpoints.
   ``3`` Enable all http UI endpoints.
   ===== ======================================================================

   To enable any enpoint there needs to be an entry in :file:`remap.config` which
   specifically enables it. Such a line would look like: ::

        map / http://{cache}

   The following are the cache endpoints:

   ================ ===========================================================
   Name             Description
   ================ ===========================================================
   ``cache``        UI to interact with the cache.
   ================ ===========================================================

   The following are the stats endpoints:

   ================== =========================================================
   Name               Description
   ================== =========================================================
   ``cache-internal`` Statistics about cache evacuation and volumes.
   ``hostdb``         Lookups against the hostdb.
   ``http``           HTTPSM details, this endpoint is also gated by
                      :ts:cv:`proxy.config.http.enable_http_info`.
   ``net``            Lookup and listing of open connections.
   ================== =========================================================

.. ts:cv:: CONFIG proxy.config.http.enable_http_info INT 0

   Enables (``1``) or disables (``0``) access to an endpoint within
   :ts:cv:`proxy.config.http_ui_enabled` which shows details about inflight
   transactions (HttpSM).

DNS
===

.. ts:cv:: CONFIG proxy.config.dns.search_default_domains INT 0
   :Reloadable:

   |TS| can attempt to resolve unqualified hostnames by expanding to the local
   domain. For example if a client makes a request to an unqualified host (e.g.
   ``host_x``) and the |TS| local domain is ``y.com``, then |TS| will expand
   the hostname to ``host_x.y.com``.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disable local domain expansion.
   ``1`` Enable local domain expansion.
   ``2`` Enable local domain expansion, but do not split local domain name.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.dns.splitDNS.enabled INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) DNS server selection. When enabled, |TS| refers to the :file:`splitdns.config` file for
   the selection specification. Refer to :ref:`Configuring DNS Server Selection <admin-split-dns>`.

.. ts:cv:: CONFIG proxy.config.dns.resolv_conf STRING /etc/resolv.conf

   Allows to specify which ``resolv.conf`` file to use for finding resolvers. While the format of this file must be the same as the
   standard ``resolv.conf`` file, this option allows an administrator to manage the set of resolvers in an external configuration file,
   without affecting how the rest of the operating system uses DNS.

.. ts:cv:: CONFIG proxy.config.dns.round_robin_nameservers INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) DNS server round-robin.

.. ts:cv:: CONFIG proxy.config.dns.nameservers STRING NULL
   :reloadable:

   The DNS servers.

.. ts:cv:: CONFIG proxy.config.srv_enabled INT 0
   :reloadable:
   :overridable:

   Indicates whether to use SRV records for orgin server lookup.

.. ts:cv:: CONFIG proxy.config.dns.dedicated_thread INT 0

   Create and dedicate a thread entirely for DNS processing. This is probably
   most useful on system which do a significant number of DNS lookups,
   typically forward proxies. But even on other systems, it can avoid some
   contention on the first worker thread (which otherwise takes on the burden of
   all DNS lookups).

.. ts:cv:: CONFIG proxy.config.dns.validate_query_name INT 0

   When enabled (1) provides additional resilience against DNS forgery (for instance
   in DNS Injection attacks), particularly in forward or transparent proxies, but
   requires that the resolver populates the queries section of the response properly.

.. ts:cv:: CONFIG proxy.config.dns.connection_mode INT 0

   Three connection modes between |TS| and nameservers can be set -- UDP_ONLY,
   TCP_RETRY, TCP_ONLY.


   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` UDP_ONLY:  |TS| always talks to nameservers over UDP.
   ``1`` TCP_RETRY: |TS| first UDP, retries with TCP if UDP response is truncated.
   ``2`` TCP_ONLY:  |TS| always talks to nameservers over TCP.
   ===== ======================================================================

HostDB
======

.. ts:cv:: CONFIG proxy.config.hostdb.lookup_timeout INT 30
   :units: seconds
   :reloadable:

   Time to wait for a DNS response in seconds.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.hostdb.serve_stale_for INT
   :units: seconds
   :reloadable:

   The number of seconds for which to use a stale NS record while initiating a
   background fetch for the new data.

   If not set then stale records are not served.

.. ts:cv:: CONFIG proxy.config.hostdb.max_size INT 10737418240
   :units: bytes

   The maximum amount of space (in bytes) allocated to ``hostdb``.
   Setting this value to ``-1`` will disable size limit enforcement.

.. ts:cv:: CONFIG proxy.config.hostdb.max_count INT -1

   The maximum number of entries that can be stored in hostdb. A value of ``-1``
   disables item count limit enforcement.

.. note::

   For values above ``200000``, you must increase :ts:cv:`proxy.config.hostdb.max_size`
   by at least 44 bytes per entry.

.. ts:cv:: proxy.config.hostdb.round_robin_max_count INT 16

   The maximum count of DNS answers per round robin hostdb record. The default variable is 16.

.. ts:cv:: CONFIG proxy.config.hostdb.ttl_mode INT 0
   :reloadable:

   A host entry will eventually time out and be discarded. This variable
   controls how that time is calculated. A DNS request will return a TTL value
   and an internal value can be set with :ts:cv:`proxy.config.hostdb.timeout`.
   This variable determines which value will be used.

   ===== ======================================================================
   Value TTL
   ===== ======================================================================
   ``0`` The TTL from the DNS response.
   ``1`` The internal timeout value.
   ``2`` The smaller of the DNS and internal TTL values. The internal timeout
         value becomes a maximum TTL.
   ``3`` The larger of the DNS and internal TTL values. The internal timeout
         value become a minimum TTL.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.hostdb.timeout INT 1440
   :units: seconds
   :reloadable:

   Internal time to live value for host DB entries in seconds.

   See :ts:cv:`proxy.config.hostdb.ttl_mode` for when this value
   is used.  See :ref:`admin-performance-timeouts` for more discussion
   on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.hostdb.fail.timeout INT 0

   Time to live value for "failed" hostdb lookups.

.. note::
   HostDB considers any response that does not contain a response to
   the query a failure. This means "failure" responses (such as SOA) are
   subject to this timeout

.. ts:cv:: CONFIG proxy.config.hostdb.strict_round_robin INT 0
   :reloadable:

   Set host resolution to use strict round robin.

   When this and :ts:cv:`proxy.config.hostdb.timed_round_robin` are both
   disabled (set to ``0``), |TS| always uses the same origin server for the
   same client, for as long as the origin server is available. Otherwise if
   this is set then IP address is rotated on every request. This setting takes
   precedence over :ts:cv:`proxy.config.hostdb.timed_round_robin`.

.. ts:cv:: CONFIG proxy.config.hostdb.timed_round_robin INT 0
   :reloadable:

   Set host resolution to use timed round robin.

   When this and :ts:cv:`proxy.config.hostdb.strict_round_robin` are both
   disabled (set to ``0``), |TS| always uses the same origin server for the
   same client, for as long as the origin server is available. Otherwise if
   this is set to *N* the IP address is rotated if more than *N* seconds have
   passed since the first time the current address was used.

.. ts:cv:: CONFIG proxy.config.hostdb.host_file.path STRING NULL

   Set the file path for an external host file.

   If this is set (non-empty) then the file is presumed to be a hosts file in
   the standard `host file format <http://tools.ietf.org/html/rfc1123#page-13>`_.
   It is read and the entries there added to the HostDB. The file is
   periodically checked for a more recent modification date in which case it is
   reloaded. The interval is set with :ts:cv:`proxy.config.hostdb.host_file.interval`.

   While not technically reloadable, the value is read every time the file is
   to be checked so that if changed the new value will be used on the next
   check and the file will be treated as modified.

.. ts:cv:: CONFIG proxy.config.hostdb.host_file.interval INT 86400
   :units: seconds
   :reloadable:

   Set the file changed check timer for :ts:cv:`proxy.config.hostdb.host_file.path`.

   The file is checked every this many seconds to see if it has changed. If so
   the HostDB is updated with the new values in the file.

.. ts:cv:: CONFIG proxy.config.hostdb.partitions INT 64

   The number of partitions for hostdb. If you are seeing lock contention within
   hostdb's cache (due to a large number of records) you can increase the number
   of partitions

.. ts:cv:: CONFIG proxy.config.hostdb.ip_resolve STRING NULL

   Set the host resolution style.

   This is an ordered list of keywords separated by semicolons that specify how
   a host name is to be resolved to an IP address. The keywords are case
   insensitive.

   ========== ====================================================
   Keyword    Description
   ========== ====================================================
   ``ipv4``   Resolve to an IPv4 address.
   ``ipv6``   Resolve to an IPv6 address.
   ``client`` Resolve to the same family as the client IP address.
   ``only``   Stop resolving.
   ========== ====================================================

   The order of the keywords is critical. When a host name needs to be resolved
   it is resolved in same order as the keywords. If a resolution fails, the
   next option in the list is tried. The keyword ``only`` means to give up
   resolution entirely. The keyword list has a maximum length of three
   keywords, more are never needed. By default there is an implicit
   ``ipv4;ipv6`` attached to the end of the string unless the keyword
   ``only`` appears.

.. topic:: Example

   Use the incoming client family, then try IPv4 and IPv6. ::

      client;ipv4;ipv6

   Because of the implicit resolution this can also be expressed as just ::

      client

.. topic:: Example

   Resolve only to IPv4. ::

      ipv4;only

.. topic:: Example

   Resolve only to the same family as the client (do not permit cross family transactions). ::

      client;only

   This value is a global default that can be overridden by :ts:cv:`proxy.config.http.server_ports`.

.. note::

   This style is used as a convenience for the administrator. During a resolution the *resolution order* will be
   one family, then possibly the other. This is determined by changing ``client`` to ``ipv4`` or ``ipv6`` based on the
   client IP address and then removing duplicates.

.. important::

   This option has no effect on outbound transparent connections The local IP address used in the connection to the
   origin server is determined by the client, which forces the IP address family of the address used for the origin
   server. In effect, outbound transparent connections always use a resolution style of "``client``".

.. ts:cv:: CONFIG proxy.config.hostdb.verify_after INT 720

    Set the interval (in seconds) in which to re-query DNS regardless of TTL status.

.. ts:cv:: CONFIG proxy.config.hostdb.filename STRING "host.db"

   The filename to persist hostdb to on disk.

.. ts:cv:: CONFIG proxy.config.cache.hostdb.sync_frequency INT 120

   Set the frequency (in seconds) to sync hostdb to disk.

   Note: hostdb is syncd to disk on a per-partition basis (of which there are 64).
   This means that the minumum time to sync all data to disk is :ts:cv:`proxy.config.cache.hostdb.sync_frequency` * 64

Logging Configuration
=====================

.. ts:cv:: CONFIG proxy.config.log.logging_enabled INT 3
   :reloadable:

   Enables and disables event logging:

   ======== ===================================================================
   Value    Effect
   ======== ===================================================================
   ``0``    Logging disabled.
   ``1``    Log errors only.
   ``2``    Log transactions only.
   ``3``    Dual logging (errors and transactions).
   ======== ===================================================================

   Refer to :ref:`admin-logging` for more information on event logging.

.. ts:cv:: CONFIG proxy.config.log.max_secs_per_buffer INT 5
   :reloadable:

   The maximum amount of time before data in the buffer is flushed to disk.

.. note::

   The effective lower bound to this config is whatever :ts:cv:`proxy.config.log.periodic_tasks_interval`
   is set to.

.. ts:cv:: CONFIG proxy.config.log.max_space_mb_for_logs INT 25000
   :units: megabytes
   :reloadable:

   The amount of space allocated to the logging directory (in MB).
   The headroom amount specified by
   :ts:cv:`proxy.config.log.max_space_mb_headroom` is taken from
   this space allocation.

.. note::

   All files in the logging directory contribute to the space used,
   even if they are not log files. In collation client mode, if
   there is no local disk logging, or
   :ts:cv:`proxy.config.log.max_space_mb_for_orphan_logs` is set
   to a higher value than :ts:cv:`proxy.config.log.max_space_mb_for_logs`,
   |TS| will take :ts:cv:`proxy.config.log.max_space_mb_for_orphan_logs`
   for maximum allowed log space.

.. ts:cv:: CONFIG proxy.config.log.max_space_mb_for_orphan_logs INT 25
   :units: megabytes
   :reloadable:

   The amount of space allocated to the logging directory (in MB) if this node is acting as a collation client.

.. note::

   When max_space_mb_for_orphan_logs is take as the maximum allowed log space in the logging system, the same rule apply
   to proxy.config.log.max_space_mb_for_logs also apply to proxy.config.log.max_space_mb_for_orphan_logs, ie: All files
   in the logging directory contribute to the space used, even if they are not log files. you may need to consider this
   when you enable full remote logging, and bump to the same size as proxy.config.log.max_space_mb_for_logs.

.. ts:cv:: CONFIG proxy.config.log.max_space_mb_headroom INT 1000
   :units: megabytes
   :reloadable:

   The tolerance for the log space limit (in megabytes). If the variable :ts:cv:`proxy.config.log.auto_delete_rolled_files` is set to ``1``
   (enabled), then autodeletion of log files is triggered when the amount of free space available in the logging directory is less than
   the value specified here.

.. ts:cv:: CONFIG proxy.config.log.hostname STRING localhost
   :reloadable:

   The hostname of the machine running |TS|.

.. ts:cv:: CONFIG proxy.config.log.logfile_dir STRING var/log/trafficserver
   :reloadable:

   The path to the logging directory. This can be an absolute path
   or a path relative to the ``PREFIX`` directory in which Traffic
   Server is installed.

.. note:: The directory you specify must already exist.

.. ts:cv:: CONFIG proxy.config.log.logfile_perm STRING rw-r--r--
   :reloadable:

   The log file permissions. The standard UNIX file permissions are used (owner, group, other). Permissible values are:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``-`` No permissions.
   ``r`` Read permission.
   ``w`` Write permission.
   ``x`` Execute permission.
   ===== ======================================================================

   Permissions are subject to the umask settings for the |TS| process. This
   means that a umask setting of ``002`` will not allow write permission for
   others, even if specified in the configuration file. Permissions for
   existing log files are not changed when the configuration is modified.

.. ts:cv:: LOCAL proxy.local.log.collation_mode INT 0
   :reloadable:
   :deprecated:

   Set the log collation mode.

   ===== ======================================================================
   Value Effect
   ===== ======================================================================
   ``0`` Log collation is disabled.
   ``1`` This host is a log collation server.
   ``2`` This host is a collation client and sends entries using standard
         formats to the collation server.
   ``3`` This host is a collation client and sends entries using the
         traditional custom formats to the collation server.
   ``4`` This host is a collation client and sends entries that use both the
         standard and traditional custom formats to the collation server.
   ===== ======================================================================

   For information on sending custom formats to the collation server,
   refer to :ref:`admin-logging-collating-custom-formats` and
   :file:`logging.yaml`.

.. note::

   Log collation is a *deprecated* feature as of ATS v8.0.0, and  will be
   removed in ATS v9.0.0. Our recommendation is to use one of the many existing
   log collection tools, such as Kafka, LogStash, FileBeat, Fluentd or even
   syslog / syslog-ng.

.. ts:cv:: CONFIG proxy.config.log.collation_host STRING NULL
   :deprecated:

   The hostname of the log collation server.

.. ts:cv:: CONFIG proxy.config.log.collation_port INT 8085
   :reloadable:
   :deprecated:

   The port used for communication between the collation server and client.

.. ts:cv:: CONFIG proxy.config.log.collation_secret STRING foobar
   :reloadable:
   :deprecated:

   The password used to validate logging data and prevent the exchange of unauthorized information when a collation server is being used.

.. ts:cv:: CONFIG proxy.config.log.collation_host_tagged INT 0
   :reloadable:
   :deprecated:

   When enabled (``1``), configures |TS| to include the hostname of the collation client that generated the log entry in each entry.

.. ts:cv:: CONFIG proxy.config.log.collation_retry_sec INT 5
   :reloadable:
   :deprecated:

   The number of seconds between collation server connection retries.

.. ts:cv:: CONFIG proxy.config.log.collation_host_timeout INT 86390
   :deprecated:

   The number of seconds before inactivity time-out events for the host side.
   This setting over-rides the default set with proxy.config.net.default_inactivity_timeout
   for log collation connections.

   The default is set for 10s less on the host side to help prevent any possible race
   conditions. If the host disconnects first, the client will see the disconnect
   before its own time-out and re-connect automatically. If the client does not see
   the disconnect, i.e., connection is "locked-up" for some reason, it will disconnect
   when it reaches its own time-out and then re-connect automatically.

.. ts:cv:: CONFIG proxy.config.log.collation_client_timeout INT 86400
   :deprecated:

   The number of seconds before inactivity time-out events for the client side.
   This setting over-rides the default set with proxy.config.net.default_inactivity_timeout
   for log collation connections.

.. ts:cv:: CONFIG proxy.config.log.rolling_enabled INT 1
   :reloadable:

   Specifies how log files are rolled. You can specify the following values:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disables log file rolling.
   ``1`` Enables log file rolling at specific intervals during the day
         (specified with the :ts:cv:`proxy.config.log.rolling_interval_sec` and
         :ts:cv:`proxy.config.log.rolling_offset_hr` variables).
   ``2`` Enables log file rolling when log files reach a specific size
         (specified with :ts:cv:`proxy.config.log.rolling_size_mb`).
   ``3`` Enables log file rolling at specific intervals during the day or when
         log files reach a specific size (whichever occurs first).
   ``4`` Enables log file rolling at specific intervals during the day when log
         files reach a specific size (i.e. at a specified time if the file is
         of the specified size).
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.log.rolling_interval_sec INT 86400
   :reloadable:

   The log file rolling interval, in seconds. The minimum value is ``60`` (1 minute). The maximum, and default, value is 86400 seconds (one day).

.. note:: If you start |TS| within a few minutes of the next rolling time, then rolling might not occur until the next rolling time.

.. ts:cv:: CONFIG proxy.config.log.rolling_offset_hr INT 0
   :reloadable:

   The file rolling offset hour. The hour of the day that starts the log rolling period.

.. ts:cv:: CONFIG proxy.config.log.rolling_size_mb INT 10
   :reloadable:

   The size, in megabytes, that log files must reach before rolling takes place.
   The minimum value for this setting is ``10``.

.. ts:cv:: CONFIG proxy.config.log.auto_delete_rolled_files INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) automatic deletion of rolled files.

.. ts:cv:: CONFIG proxy.config.log.sampling_frequency INT 1
   :reloadable:

   Configures |TS| to log only a sample of transactions rather than every
   transaction. You can specify the following values:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``1`` Log every transaction.
   ``2`` Log every second transaction.
   ``3`` Log every third transaction.
   *n*   ... and so on...
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.log.periodic_tasks_interval INT 5
   :reloadable:
   :units: seconds

   How often |TS| executes log related periodic tasks, in seconds

.. ts:cv:: CONFIG proxy.config.http.slow.log.threshold INT 0
   :reloadable:
   :units: milliseconds

   If set to a non-zero value :arg:`N` then any connection that takes longer than :arg:`N` milliseconds from accept to
   completion will cause its timing stats to be written to the :ts:cv:`debugging log file
   <proxy.config.output.logfile>`. This is identifying data about the transaction and all of the :c:type:`transaction milestones <TSMilestonesType>`.

.. ts:cv:: CONFIG proxy.config.log.config.filename STRING logging.yaml
   :reloadable:

   This configuration value specifies the path to the
   :file:`logging.yaml` configuration file. If this is a relative
   path, |TS| loads it relative to the ``SYSCONFDIR`` directory.

Diagnostic Logging Configuration
================================

.. ts:cv:: CONFIG proxy.config.diags.output.diag STRING E
.. ts:cv:: CONFIG proxy.config.diags.output.debug STRING E
.. ts:cv:: CONFIG proxy.config.diags.output.status STRING L
.. ts:cv:: CONFIG proxy.config.diags.output.note STRING L
.. ts:cv:: CONFIG proxy.config.diags.output.warning STRING L
.. ts:cv:: CONFIG proxy.config.diags.output.error STRING SL
.. ts:cv:: CONFIG proxy.config.diags.output.fatal STRING SL
.. ts:cv:: CONFIG proxy.config.diags.output.alert STRING L
.. ts:cv:: CONFIG proxy.config.diags.output.emergency STRING SL

   The diagnosic output configuration variables control where Traffic
   Server should log diagnostic output. Messages at each diagnostic level
   can be directed to any combination of diagnostic destinations.
   Valid diagnostic message destinations are:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``O`` Log to standard output.
   ``E`` Log to standard error.
   ``S`` Log to syslog.
   ``L`` Log to :file:`diags.log`.
   ===== ======================================================================

.. topic:: Example

   To log debug diagnostics to both syslog and `diags.log`::

        CONFIG proxy.config.diags.output.debug STRING SL

.. ts:cv:: CONFIG proxy.config.diags.show_location INT 1

   Annotates diagnostic messages with the source code location. Set to 1 to enable
   for Debug() messages only. Set to 2 to enable for all messages.

.. ts:cv:: CONFIG proxy.config.diags.debug.enabled INT 0
   :reloadable:

   When set to 1, enables logging for diagnostic messages whose log level is `diag` or `debug`.

   When set to 2, interprets the :ts:cv:`proxy.config.diags.debug.client_ip` setting determine whether diagnostic messages are logged.

.. ts:cv:: CONFIG proxy.config.diags.debug.client_ip STRING NULL

   if :ts:cv:`proxy.config.diags.debug.enabled` is set to 2, this value is tested against the source IP of the incoming connection.  If there is a match, all the diagnostic messages for that connection and the related outgoing connection will be logged.

.. ts:cv:: CONFIG proxy.config.diags.debug.tags STRING http|dns

   Each |TS| `diag` and `debug` level message is annotated with a subsytem tag.  This configuration
   contains an anchored regular expression that filters the messages based on the tag. The
   expressions are prefix matched which creates an implicit ``.*`` at the end. Therefore the default
   value ``http|dns`` will match tags such as ``http``, ``http_hdrs``, ``dns``, and ``dns_recv``.

   Some commonly used debug tags are:

   ============  =====================================================
   Tag           Subsytem usage
   ============  =====================================================
   dns           DNS query resolution
   http_hdrs     Logs the headers for HTTP requests and responses
   privileges    Privilege elevation
   ssl           TLS termination and certificate processing
   ============  =====================================================

   |TS| plugins will typically log debug messages using the :c:func:`TSDebug`
   API, passing the plugin name as the debug tag.


.. ts:cv:: CONFIG proxy.config.diags.logfile_perm STRING rw-r--r--

   The log file permissions. The standard UNIX file permissions are used (owner, group, other). Permissible values are:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``-`` No permissions.
   ``r`` Read permission.
   ``w`` Write permission.
   ``x`` Execute permission.
   ===== ======================================================================

   Permissions are subject to the umask settings for the |TS| process. This
   means that a umask setting of ``002`` will not allow write permission for
   others, even if specified in the configuration file. Permissions for
   existing log files are not changed when the configuration is modified.


.. ts:cv:: CONFIG proxy.config.diags.logfile.rolling_enabled INT 0
   :reloadable:

   Specifies how the diagnostics log is rolled. You can specify the following values:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disables diagnostics log rolling.
   ``1`` Enables diagnostics log rolling at specific intervals (specified with
         :ts:cv:`proxy.config.diags.logfile.rolling_interval_sec`). The "clock"
         starts ticking on |TS| startup.
   ``2`` Enables diagnostics log rolling when the diagnostics log reaches a
         specific size (specified with
         :ts:cv:`proxy.config.diags.logfile.rolling_size_mb`).
   ``3`` Enables diagnostics log rolling at specific intervals or when the
         diagnostics log reaches a specific size (whichever occurs first).
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.diags.logfile.rolling_interval_sec INT 3600
   :reloadable:
   :units: seconds

   Specifies how often the diagnostics log is rolled, in seconds. The timer starts on |TS| bootup.

.. ts:cv:: CONFIG proxy.config.diags.logfile.rolling_size_mb INT 100
   :reloadable:
   :units: megabytes

   Specifies at what size to roll the diagnostics log at.

Reverse Proxy
=============

.. ts:cv:: CONFIG proxy.config.reverse_proxy.enabled INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) HTTP reverse proxy.

.. ts:cv:: CONFIG proxy.config.header.parse.no_host_url_redirect STRING NULL
   :reloadable:

   The URL to which to redirect requests with no host headers (reverse
   proxy).

URL Remap Rules
===============

.. ts:cv:: CONFIG proxy.config.url_remap.filename STRING remap.config

   Sets the name of the :file:`remap.config` file.

.. ts:cv:: CONFIG proxy.config.url_remap.remap_required INT 1
   :reloadable:

   Set this variable to ``1`` if you want |TS| to serve
   requests only from origin servers listed in the mapping rules of the
   :file:`remap.config` file. If a request does not match, then the browser
   will receive an error.

.. ts:cv:: CONFIG proxy.config.url_remap.pristine_host_hdr INT 0
   :reloadable:
   :overridable:

   Set this variable to ``1`` if you want to retain the client host
   header in a request during remapping.

.. _records-config-ssl-termination:

SSL Termination
===============

.. ts:cv:: CONFIG proxy.config.ssl.server.cipher_suite STRING <see notes>

   Configures the set of encryption, digest, authentication, and key exchange
   algorithms provided by OpenSSL which |TS| will use for SSL connections. For
   the list of algorithms and instructions on constructing an appropriately
   formatting cipher_suite string, see
   `OpenSSL Ciphers <https://www.openssl.org/docs/manmaster/man1/ciphers.html>`_.

   The current default, included in the ``records.config.default`` example
   configuration is:

   ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:DHE-RSA-AES256-SHA:DHE-DSS-AES256-SHA:DHE-RSA-AES128-SHA:DHE-DSS-AES128-SHA:AES256-GCM-SHA384:AES128-GCM-SHA256:AES256-SHA256:AES128-SHA256:AES256-SHA:AES128-SHA:DES-CBC3-SHA:!aNULL:!eNULL:!EXPORT:!DES:!RC4:!MD5:!PSK:!aECDH:!EDH-DSS-DES-CBC3-SHA:!EDH-RSA-DES-CBC3-SHA:!KRB5-DES-CBC3-SHA

.. ts:cv:: CONFIG proxy.config.ssl.client.cipher_suite STRING <See notes under proxy.config.ssl.server.cipher_suite.>

   Configures the cipher_suite which |TS| will use for SSL connections to origin or next hop.

.. ts:cv:: CONFIG proxy.config.ssl.server.TLSv1_3.cipher_suites STRING <See notes>

   Configures the pair of the AEAD algorithm and hash algorithm to be
   used with HKDF provided by OpenSSL which |TS| will use for TLSv1.3
   connections. For the list of algorithms and instructions, see
   The ``-ciphersuites`` section of `OpenSSL Ciphers <https://www.openssl.org/docs/manmaster/man1/ciphers.html>`_.

   The current default value with OpenSSL is:

   TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256:TLS_AES_128_GCM_SHA256

   This configuration works with OpenSSL v1.1.1 and above.

.. ts:cv:: CONFIG proxy.config.ssl.client.TLSv1_3.cipher_suites STRING <See notes under proxy.config.ssl.server.tls.cipher_suites>

   Configures the cipher_suites which |TS| will use for TLSv1.3
   connections to origin or next hop. This configuration works
   with OpenSSL v1.1.1 and above.

.. ts:cv:: CONFIG proxy.config.ssl.server.groups_list STRING <See notes>

   Configures the list of supported groups provided by OpenSSL which
   |TS| will be used to determine the set of shared groups. The value
   is a colon separated list of group NIDs or names, for example
   "P-521:P-384:P-256". For instructions, see "Groups" section of
   `TLS1.3 - OpenSSLWiki <https://wiki.openssl.org/index.php/TLS1.3#Groups>`_.

   The current default value with OpenSSL is:

   X25519:P-256:X448:P-521:P-384

   This configuration works with OpenSSL v1.1.1 and above.

.. ts:cv:: CONFIG proxy.config.ssl.client.groups_list STRING <See notes under proxy.config.ssl.server.groups_list.>

   Configures the list of supported groups provided by OpenSSL which
   |TS| will use for the "key_share" and "supported groups" extention
   of TLSv1.3 connections. The value is a colon separated list of
   group NIDs or names, for example "P-521:P-384:P-256". For
   instructions, see "Groups" section of `TLS1.3 - OpenSSLWiki <https://wiki.openssl.org/index.php/TLS1.3#Groups>`_.

   This configuration works with OpenSSL v1.1.1 and above.

.. ts:cv:: CONFIG proxy.config.ssl.TLSv1 INT 1

   Enables (``1``) or disables (``0``) TLSv1.

.. ts:cv:: CONFIG proxy.config.ssl.TLSv1_1 INT 1

   Enables (``1``) or disables (``0``) TLS v1.1.  If not specified, enabled by default.  [Requires OpenSSL v1.0.1 and higher]

.. ts:cv:: CONFIG proxy.config.ssl.TLSv1_2 INT 1

   Enables (``1``) or disables (``0``) TLS v1.2.  If not specified, enabled by default.  [Requires OpenSSL v1.0.1 and higher]

.. ts:cv:: CONFIG proxy.config.ssl.client.certification_level INT 0

   Sets the client certification level:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Client certificates are **ignored**. |TS| does not verify client
         certificates during the SSL handshake. Access to |TS| depends on |TS|
         configuration options (such as access control lists).
   ``1`` Client certificates are **optional**. If a client has a certificate,
         then the certificate is validated. If the client does not have a
         certificate, then the client is still allowed access to |TS| unless
         access is denied through other |TS| configuration options.
   ``2`` Client certificates are **required**. The client must be authenticated
         during the SSL handshake. Clients without a certificate are not
         allowed to access |TS|.
   ===== ======================================================================


.. ts:cv:: CONFIG proxy.config.ssl.server.multicert.filename STRING ssl_multicert.config

   The location of the :file:`ssl_multicert.config` file, relative
   to the |TS| configuration directory. In the following
   example, if the |TS| configuration directory is
   `/etc/trafficserver`, the |TS| SSL configuration file
   and the corresponding certificates are located in
   `/etc/trafficserver/ssl`::

      CONFIG proxy.config.ssl.server.multicert.filename STRING ssl/ssl_multicert.config
      CONFIG proxy.config.ssl.server.cert.path STRING etc/trafficserver/ssl
      CONFIG proxy.config.ssl.server.private_key.path STRING etc/trafficserver/ssl

.. ts:cv:: CONFIG proxy.config.ssl.server.multicert.exit_on_load_fail INT 1

   By default (``1``), |TS| will not start unless all the SSL certificates listed in the
   :file:`ssl_multicert.config` file successfully load.  If false (``0``), SSL certificate
   load failures will not prevent |TS| from starting.

.. ts:cv:: CONFIG proxy.config.ssl.server.cert.path STRING /config

   The location of the SSL certificates and chains used for accepting
   and validation new SSL sessions. If this is a relative path,
   it is appended to the |TS| installation PREFIX. All
   certificates and certificate chains listed in
   :file:`ssl_multicert.config` will be loaded relative to this path.

.. ts:cv:: CONFIG proxy.config.ssl.server.private_key.path STRING NULL

   The location of the SSL certificate private keys. Change this
   variable only if the private key is not located in the SSL
   certificate file. All private keys listed in
   :file:`ssl_multicert.config` will be loaded relative to this
   path.

.. ts:cv:: CONFIG proxy.config.ssl.server.cert_chain.filename STRING NULL

   The name of a file containing a global certificate chain that
   should be used with every server certificate. This file is only
   used if there are certificates defined in :file:`ssl_multicert.config`.
   Unless this is an absolute path, it is loaded relative to the
   path specified by :ts:cv:`proxy.config.ssl.server.cert.path`.

.. ts:cv:: CONFIG proxy.config.ssl.server.dhparams_file STRING NULL

   The name of a file containing a set of Diffie-Hellman key exchange
   parameters. If not specified, 2048-bit DH parameters from RFC 5114 are
   used. These parameters are only used if a DHE (or EDH) cipher suite has
   been selected.

.. ts:cv:: CONFIG proxy.config.ssl.CA.cert.path STRING NULL

   The location of the certificate authority file that client
   certificates will be verified against.

.. ts:cv:: CONFIG proxy.config.ssl.CA.cert.filename STRING NULL

   The filename of the certificate authority that client certificates
   will be verified against.

.. ts:cv:: CONFIG proxy.config.ssl.server.ticket_key.filename STRING ssl_ticket.key

   The filename of the default and global ticket key for SSL sessions. The location is relative to the
   :ts:cv:`proxy.config.ssl.server.cert.path` directory. One way to generate this would be to run
   ``head -c48 /dev/urandom | openssl enc -base64 | head -c48 > file.ticket``. Also
   note that OpenSSL session tickets are sensitive to the version of the ca-certificates.

.. ts:cv:: CONFIG proxy.config.ssl.servername.filename STRING ssl_server_name.yaml

   The filename of the :file:`ssl_server_name.yaml` configuration file.
   If relative, it is relative to the configuration directory.

.. ts:cv:: CONFIG proxy.config.ssl.max_record_size INT 0

  This configuration specifies the maximum number of bytes to write
  into a SSL record when replying over a SSL session. In some
  circumstances this setting can improve response latency by reducing
  buffering at the SSL layer. This setting can have a value between 0
  and 16383 (max TLS record size).

  The default of ``0`` means to always write all available data into
  a single SSL record.

  A value of ``-1`` means TLS record size is dynamically determined. The
  strategy employed is to use small TLS records that fit into a single
  TCP segment for the first ~1 MB of data, but, increase the record size to
  16 KB after that to optimize throughput. The record size is reset back to
  a single segment after ~1 second of inactivity and the record size ramping
  mechanism is repeated again.

.. ts:cv:: CONFIG proxy.config.ssl.session_cache INT 2

   Enables the SSL session cache:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disables the session cache entirely.
   ``1`` Enables the session cache using OpenSSL's implementation.
   ``2`` Default. Enables the session cache using |TS|'s implementation. This
         implentation should perform much better than the OpenSSL
         implementation.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.ssl.session_cache.timeout INT 0

  This configuration specifies the lifetime of SSL session cache
  entries in seconds. If it is ``0``, then the SSL library will use
  a default value, typically 300 seconds. Note: This option has no affect
  when using the |TS| session cache (option ``2`` in
  ``proxy.config.ssl.session_cache``)

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.ssl.session_cache.auto_clear INT 1

  This will set the OpenSSL auto clear flag. Auto clear is enabled by
  default with ``1`` it can be disabled by changing this setting to ``0``.

.. ts:cv:: CONFIG proxy.config.ssl.session_cache.size INT 102400

  This configuration specifies the maximum number of entries
  the SSL session cache may contain.

.. ts:cv:: CONFIG proxy.config.ssl.session_cache.num_buckets INT 256

  This configuration specifies the number of buckets to use with the
  |TS| SSL session cache implementation. The TS implementation
  is a fixed size hash map where each bucket is protected by a mutex.

.. ts:cv:: CONFIG proxy.config.ssl.session_cache.skip_cache_on_bucket_contention INT 0

   This configuration specifies the behavior of the |TS| SSL session
   cache implementation during lock contention on each bucket:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Default. Don't skip session caching when bucket lock is contented.
   ``1`` Disable the SSL session cache for a connection during lock contention.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.ssl.hsts_max_age INT -1
   :overridable:

   This configuration specifies the max-age value that will be used
   when adding the Strict-Transport-Security header.  The value is in seconds.
   A value of ``0`` will set the max-age value to ``0`` and should remove the
   HSTS entry from the client.  A value of ``-1`` will disable this feature and
   not set the header.  This option is only used for HTTPS requests and the
   header will not be set on HTTP requests.

.. ts:cv:: CONFIG proxy.config.ssl.hsts_include_subdomains INT 0
   :overridable:

   Enables (``1``) or disables (``0``) adding the includeSubdomain value
   to the Strict-Transport-Security header.  proxy.config.ssl.hsts_max_age
   needs to be set to a non ``-1`` value for this configuration to take effect.

.. ts:cv:: CONFIG proxy.config.ssl.allow_client_renegotiation INT 0

   This configuration specifies whether the client is able to initiate
   renegotiation of the SSL connection.  The default of ``0``, means
   the client can't initiate renegotiation.

.. ts:cv:: CONFIG proxy.config.ssl.cert.load_elevated INT 0

   Enables (``1``) or disables (``0``) elevation of traffic_server
   privileges during loading of SSL certificates.  By enabling this, SSL
   certificate files' access rights can be restricted to help reduce the
   vulnerability of certificates.

   This feature requires |TS| to be built with POSIX
   capabilities enabled.

.. ts:cv:: CONFIG proxy.config.ssl.handshake_timeout_in INT 0

   When enabled this limits the total duration for the server side SSL
   handshake.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.ssl.wire_trace_enabled INT 0

   When enabled this turns on wire tracing of SSL connections that meet
   the conditions specified by wire_trace_percentage, wire_trace_addr
   and wire_trace_server_name.

.. ts:cv:: CONFIG proxy.config.ssl.wire_trace_percentage INT 0

   This specifies the percentage of traffic meeting the other wire_trace
   conditions to be traced.

.. ts:cv:: CONFIG proxy.config.ssl.wire_trace_addr STRING NULL

   This specifies the client IP for which wire_traces should be printed.

.. ts:cv:: CONFIG proxy.config.ssl.wire_trace_server_name STRING NULL

   This specifies the server name for which wire_traces should be printed.

Client-Related Configuration
----------------------------

.. ts:cv:: CONFIG proxy.config.ssl.client.verify.server.policy STRING DISABLED
   :reloadable:

   Configures |TS| to verify the origin server certificate
   with the Certificate Authority (CA). This configuration takes a value of :code:`DISABLED`, :code:`PERMISSIVE`, or :code:`ENFORCED`

   You can override this global setting on a per domain basis in the ssl_servername.yaml file using the :ref:`verify_server_policy attribute<override-verify-server-policy>`.

:code:`DISABLED` 
   Server Certificate will not be verified
:code:`PERMISSIVE` 
   Certificate will be verified and the connection will not be established if verification fails.
:code:`ENFORCED` 
   The provided certificate will be verified and the connection will be established irrespective of the verification result. If verification fails the name of the server will be logged.

.. ts:cv:: CONFIG proxy.config.ssl.client.verify.server.properties STRING ALL
   :reloadable:

   Configures |TS| for what the default verify callback should check during origin server verification.

   You can override this global setting on a per domain basis in the ssl_servername.yaml file using the :ref:`verify_server_properties attribute<override-verify-server-properties>`.

:code:`NONE`
   Check nothing in the standard callback.  Rely entirely on plugins to check the certificate.
:code:`SIGNATURE`
   Check only for a valid signature.
:code:`NAME`
   Check only that the SNI name is in the certificate.
:code:`ALL`
   Check both the signature and the name.

.. ts:cv:: CONFIG proxy.config.ssl.client.verify.server INT 0
   :reloadable:
   :deprecated:

   This setting has been deprecated and :ts:cv:`proxy.config.ssl.client.verify.server.policy` and
   :ts:cv:`proxy.config.ssl.client.verify.server.properties` should be used instead.

   Configures |TS| to verify the origin server certificate
   with the Certificate Authority (CA). This configuration takes a value between 0 to 2.

   You can override this global setting on a per domain basis in the ssl_servername.yaml file using the :ref:`verify_origin_server attribute<override-verify-origin-server>`.

   :0: Server Certificate will not be verified
   :1: Certificate will be verified and the connection will not be established if verification fail
   :2: The provided certificate will be verified and the connection will be established 

.. ts:cv:: CONFIG proxy.config.ssl.client.cert.filename STRING NULL
   :overridable:

   The filename of SSL client certificate installed on |TS|.

.. ts:cv:: CONFIG proxy.config.ssl.client.cert.path STRING /config

   The location of the SSL client certificate installed on Traffic
   Server.

.. ts:cv:: CONFIG proxy.config.ssl.client.private_key.filename STRING NULL

   The filename of the |TS| private key. Change this variable
   only if the private key is not located in the |TS| SSL
   client certificate file.

.. ts:cv:: CONFIG proxy.config.ssl.client.private_key.path STRING NULL

   The location of the |TS| private key. Change this variable
   only if the private key is not located in the SSL client certificate
   file.

.. ts:cv:: CONFIG proxy.config.ssl.client.CA.cert.filename STRING NULL

   The filename of the certificate authority against which the origin
   server will be verified.

.. ts:cv:: CONFIG proxy.config.ssl.client.CA.cert.path STRING NULL

   Specifies the location of the certificate authority file against
   which the origin server will be verified.

.. ts:cv:: CONFIG proxy.config.ssl.client.SSLv3 INT 0

   Enables (``1``) or disables (``0``) SSLv3 in the ATS client context. Disabled by default

.. ts:cv:: CONFIG proxy.config.ssl.client.TLSv1 INT 1

   Enables (``1``) or disables (``0``) TLSv1 in the ATS client context. If not specified, enabled by default

.. ts:cv:: CONFIG proxy.config.ssl.client.TLSv1_1 INT 1

   Enables (``1``) or disables (``0``) TLSv1_1 in the ATS client context. If not specified, enabled by default

.. ts:cv:: CONFIG proxy.config.ssl.client.TLSv1_2 INT 1

   Enables (``1``) or disables (``0``) TLSv1_2 in the ATS client context. If not specified, enabled by default

.. ts:cv:: CONFIG proxy.config.ssl.async.handshake.enabled INT 0

   Enables the use of openssl async job during the TLS handshake.  Traffic
   Server must be build against openssl 1.1 or greater or this to take affect.
   Can be useful if using a crypto engine that communicates off chip.  The
   thread will be rescheduled for other work until the crypto engine operation
   completes. A test crypto engine that inserts a 5 second delay on private key
   operations can be found at :ts:git:`contrib/openssl/async_engine.c`.

.. ts:cv:: CONFIG proxy.config.ssl.engine.conf_file STRING NULL

   Specify the location of the openssl config file used to load dynamic crypto
   engines. This setting assumes an absolute path.  An example config file is at
   :ts:git:`contrib/openssl/load_engine.cnf`.

OCSP Stapling Configuration
===========================

.. ts:cv:: CONFIG proxy.config.ssl.ocsp.enabled INT 0

   Enable OCSP stapling.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disables OCSP Stapling.
   ``1`` Allows |TS| to request SSL certificate revocation status from an OCSP
         responder.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.ssl.ocsp.cache_timeout INT 3600

   Number of seconds before an OCSP response expires in the stapling cache.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.ssl.ocsp.request_timeout INT 10

   Timeout (in seconds) for queries to OCSP responders.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.ssl.ocsp.update_period INT 60

   Update period (in seconds) for stapling caches.

HTTP/2 Configuration
====================


.. ts:cv:: CONFIG proxy.config.http2.max_concurrent_streams_in INT 100
   :reloadable:

   The maximum number of concurrent streams per inbound connection.

.. note::

   Reloading this value affects only new HTTP/2 connections, not the
   ones already established.

.. ts:cv:: CONFIG proxy.config.http2.min_concurrent_streams_in INT 10
   :reloadable:

   The minimum number of concurrent streams per inbound connection.
   This is used when :ts:cv:`proxy.config.http2.max_active_streams_in` is set
   larger than ``0``.

.. ts:cv:: CONFIG proxy.config.http2.max_active_streams_in INT 0
   :reloadable:

   Limits the maximum number of connection wide active streams.
   When connection wide active streams are larger than this value,
   SETTINGS_MAX_CONCURRENT_STREAMS will be reduced to
   :ts:cv:`proxy.config.http2.min_concurrent_streams_in`.
   To disable, set to zero (``0``).

.. ts:cv:: CONFIG proxy.config.http2.initial_window_size_in INT 1048576
   :reloadable:

   The initial window size for inbound connections.

.. ts:cv:: CONFIG proxy.config.http2.max_frame_size INT 16384
   :reloadable:

   Indicates the size of the largest frame payload that the sender is willing
   to receive.

.. ts:cv:: CONFIG proxy.config.http2.header_table_size INT 4096
   :reloadable:

   The maximum size of the header compression table used to decode header
   blocks.

.. ts:cv:: CONFIG proxy.config.http2.max_header_list_size INT 4294967295
   :reloadable:

   This advisory setting informs a peer of the maximum size of header list
   that the sender is prepared to accept blocks. The default value, which is
   the unsigned int maximum value in |TS|, implies unlimited size.

.. ts:cv:: CONFIG proxy.config.http2.stream_priority_enabled INT 0
   :reloadable:

   Enable the experimental HTTP/2 Stream Priority feature.

.. ts:cv:: CONFIG proxy.config.http2.active_timeout_in INT 0
   :reloadable:

   This is the active timeout of the http2 connection. It is set when the connection is opened
   and keeps ticking regardless of activity level.

   The value of ``0`` specifies that there is no timeout.

.. ts:cv:: CONFIG proxy.config.http2.accept_no_activity_timeout INT 120
   :reloadable:
   :overridable:

   Specifies how long |TS| keeps connections to clients open if no
   activity is received on the connection. Lowering this timeout can ease
   pressure on the proxy if misconfigured or misbehaving clients are opening
   a large number of connections without submitting requests.

.. ts:cv:: CONFIG proxy.config.http2.no_activity_timeout_in INT 120
   :reloadable:
   :overridable:

   Specifies how long |TS| keeps connections to clients open if a
   transaction stalls. Lowering this timeout can ease pressure on the proxy if
   misconfigured or misbehaving clients are opening a large number of
   connections without submitting requests.

.. ts:cv:: CONFIG proxy.config.http2.zombie_debug_timeout_in INT 0
   :reloadable:

   This timeout enables the zombie debugging feature.  If it is non-zero, it sets a zombie event to go off that
   many seconds in the future when the HTTP2 session reaches one but not both of the terminating events, i.e received
   a close event (via client goaway or timeout) and the number of active streams has gone to zero.  If the event is executed,
   the |TS| process will assert.  This mechanism is useful to debug potential leaks in the HTTP2 Stream and Session
   processing.

.. ts:cv:: CONFIG proxy.config.http2.push_diary_size INT 256
   :reloadable:

   Indicates the maximum number of HTTP/2 server pushes that are remembered per
   HTTP/2 connection to avoid duplicate pushes on the same connection. If the
   maximum number is reached, new entries are not remembered.

Plug-in Configuration
=====================

.. ts:cv:: CONFIG proxy.config.plugin.plugin_dir STRING config/plugins

   Specifies the location of |TS| plugins.

.. ts:cv:: CONFIG proxy.config.remap.num_remap_threads INT 0

   When this variable is set to ``0``, plugin remap callbacks are
   executed in line on network threads. If remap processing takes
   significant time, this can be cause additional request latency.
   Setting this variable to causes remap processing to take place
   on a dedicated thread pool, freeing the network threads to service
   additional requests.

SOCKS Processor
===============

.. ts:cv::  CONFIG proxy.config.socks.socks_needed INT 0

   Enables (``1``) or disables (``0``) the SOCKS processor

.. ts:cv::  CONFIG proxy.config.socks.socks_version INT 4

   Specifies the SOCKS version (``4``) or (``5``)

.. ts:cv::  CONFIG proxy.config.socks.socks_config_file STRING socks.config

   The socks_onfig file allows you to specify ranges of IP addresses
   that will not be relayed to the SOCKS server. It can also be used
   to configure AUTH information for SOCKSv5 servers.

.. ts:cv::  CONFIG proxy.config.socks.socks_timeout INT 100

   The activity timeout value (in seconds) for SOCKS server connections.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv::  CONFIG proxy.config.socks.server_connect_timeout INT 10

   The timeout value (in seconds) for SOCKS server connection attempts.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv::  CONFIG proxy.config.socks.per_server_connection_attempts INT 1

    The total number of connection attempts allowed per SOCKS server,
    if multiple servers are used.

.. ts:cv::  CONFIG proxy.config.socks.connection_attempts INT 4

   The total number of connection attempts allowed to a SOCKS server
   |TS| bypasses the server or fails the request

.. ts:cv::  CONFIG proxy.config.socks.server_retry_timeout INT 300

   The timeout value (in seconds) for SOCKS server connection retry attempts.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv::  CONFIG proxy.config.socks.default_servers STRING

   Default list of SOCKS servers and their ports.

.. ts:cv::  CONFIG proxy.config.socks.server_retry_time INT 300

   The amount of time allowed between connection retries to a SOCKS
   server that is unavailable.

.. ts:cv::  CONFIG proxy.config.socks.server_fail_threshold INT 2

   The number of times the connection to the SOCKS server can fail
   before |TS| considers the server unavailable.

.. ts:cv::  CONFIG proxy.config.socks.accept_enabled INT 0

   Enables (1) or disables (0) the SOCKS proxy option. As a SOCKS
   proxy, |TS| receives SOCKS traffic (usually on port
   1080) and forwards all requests directly to the SOCKS server.

.. ts:cv::  CONFIG proxy.config.socks.accept_port INT 1080

   Specifies the port on which |TS| accepts SOCKS traffic.

.. ts:cv::  CONFIG proxy.config.socks.http_port INT 80

   Specifies the port on which |TS| accepts HTTP proxy requests
   over SOCKS connections..

Sockets
=======

.. ts:cv:: CONFIG proxy.config.net.defer_accept INT 1

   default: ``1`` meaning ``on`` all Platforms except Linux: ``45`` seconds

   This directive enables operating system specific optimizations for a listening socket. ``defer_accept`` holds a call to ``accept(2)``
   back until data has arrived. In Linux' special case this is up to a maximum of 45 seconds.

.. ts:cv:: CONFIG proxy.config.net.listen_backlog INT -1
   :reloadable:

  This directive sets the maximum number of pending connections.
  If it is set to -1, |TS| will automatically set this
  to a platform-specific maximum.

.. ts:cv:: CONFIG  proxy.config.net.tcp_congestion_control_in STRING ""

   This directive will override the congestion control algorithm for incoming
   connections (accept sockets). On linux the allowed values are typically
   specified in a space separated list in /proc/sys/net/ipv4/tcp_allowed_congestion_control

.. ts:cv:: CONFIG  proxy.config.net.tcp_congestion_control_out STRING ""

   This directive will override the congestion control algorithm for outgoing
   connections (connect sockets). On linux the allowed values are typically
   specified in a space separated list in /proc/sys/net/ipv4/tcp_allowed_congestion_control

.. ts:cv:: CONFIG proxy.config.net.sock_send_buffer_size_in INT 0

   Sets the send buffer size for connections from the client to |TS|.

.. ts:cv:: CONFIG proxy.config.net.sock_recv_buffer_size_in INT 0

   Sets the receive buffer size for connections from the client to |TS|.

.. ts:cv:: CONFIG proxy.config.net.sock_option_flag_in INT 0x5

   Turns different options "on" for the socket handling client connections:::

        TCP_NODELAY  (1)
        SO_KEEPALIVE (2)
        SO_LINGER (4) - with a timeout of 0 seconds
        TCP_FASTOPEN (8)

.. note::

   This is a bitmask and you need to decide what bits to set.  Therefore,
   you must set the value to ``3`` if you want to enable nodelay and
   keepalive options above.

.. note::

   To allow TCP Fast Open for client sockets on Linux, bit 2 of
   the ``net.ipv4.tcp_fastopen`` sysctl must be set.

.. ts:cv:: CONFIG proxy.config.net.sock_send_buffer_size_out INT 0
   :overridable:

   Sets the send buffer size for connections from |TS| to the origin server.

.. ts:cv:: CONFIG proxy.config.net.sock_recv_buffer_size_out INT 0
   :overridable:

   Sets the receive buffer size for connections from |TS| to
   the origin server.

.. ts:cv:: CONFIG proxy.config.net.sock_option_flag_out INT 0x1
   :overridable:

   Turns different options "on" for the origin server socket:::

        TCP_NODELAY  (1)
        SO_KEEPALIVE (2)
        SO_LINGER (4) - with a timeout of 0 seconds
        TCP_FASTOPEN (8)

.. note::

   This is a bitmask and you need to decide what bits to set.  Therefore,
   you must set the value to ``3`` if you want to enable nodelay and
   keepalive options above.

   When SO_LINGER is enabled, the linger timeout time is set
   to 0. This is useful when |TS| and the origin server
   are co-located and large numbers of sockets are retained
   in the TIME_WAIT state.

.. note::

   To allow TCP Fast Open for server sockets on Linux, bit 1 of
   the ``net.ipv4.tcp_fastopen`` sysctl must be set.

.. ts:cv:: CONFIG proxy.config.net.sock_mss_in INT 0

   Same as the command line option ``--accept_mss`` that sets the MSS for all incoming requests.

.. ts:cv:: CONFIG proxy.config.net.sock_packet_mark_in INT 0x0

   Set the packet mark on traffic destined for the client
   (the packets that make up a client response).

   .. seealso:: `Traffic Shaping`_

.. ts:cv:: CONFIG proxy.config.net.sock_packet_mark_out INT 0x0
   :overridable:

   Set the packet mark on traffic destined for the origin
   (the packets that make up an origin request).

   .. seealso:: `Traffic Shaping`_

.. ts:cv:: CONFIG proxy.config.net.sock_packet_tos_in INT 0x0

   Set the ToS/DiffServ Field on packets sent to the client
   (the packets that make up a client response).

   .. seealso:: `Traffic Shaping`_

.. ts:cv:: CONFIG proxy.config.net.sock_packet_tos_out INT 0x0
   :overridable:

   Set the ToS/DiffServ Field on packets sent to the origin
   (the packets that make up an origin request).

   .. seealso:: `Traffic Shaping`_

.. ts:cv:: CONFIG proxy.config.net.poll_timeout INT 10 (or 30 on Solaris)

   Same as the command line option ``--poll_timeout``, or ``-t``, which
   specifies the timeout used for the polling mechanism used. This timeout is
   always in milliseconds (ms). This is the timeout to ``epoll_wait()`` on
   Linux platforms, and to ``kevent()`` on BSD type OSs. The default value is
   ``10`` on all platforms.

   Changing this configuration can reduce CPU usage on an idle system, since
   periodic tasks gets processed at these intervals. On busy servers, this
   overhead is diminished, since polled events triggers morefrequently.
   However, increasing the setting can also introduce additional latency for
   certain operations, and timed events. It's recommended not to touch this
   setting unless your CPU usage is unacceptable at idle workload. Some
   alternatives to this could be::

        Reduce the number of worker threads (net-threads)
        Reduce the number of disk (AIO) threads
	Make sure accept threads are enabled

   The relevant configurations for this are::

       CONFIG proxy.config.exec_thread.autoconfig INT 0
       CONFIG proxy.config.exec_thread.limit INT 2
       CONFIG proxy.config.accept_threads INT 1
       CONFIG proxy.config.cache.threads_per_disk INT 8

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.task_threads INT 2

   Specifies the number of task threads to run. These threads are used for
   various tasks that should be off-loaded from the normal network
   threads. You must have at least one task thread available.

.. ts:cv:: CONFIG proxy.config.allocator.thread_freelist_size INT 512

   Sets the maximum number of elements that can be contained in a ProxyAllocator (per-thread)
   before returning the objects to the global pool

.. ts:cv:: CONFIG proxy.config.allocator.thread_freelist_low_watermark INT 32

   Sets the minimum number of items a ProxyAllocator (per-thread) will guarantee to be
   holding at any one time.

.. ts:cv:: CONFIG proxy.config.allocator.hugepages INT 0

   Enable (1) the use of huge pages on supported platforms. (Currently only Linux)

   You must also enable hugepages at the OS level. In a modern linux Kernel
   this can be done by setting ``/proc/sys/vm/nr_overcommit_hugepages`` to a
   sufficiently large value. It is reasonable to use (system
   memory/hugepage size) because these pages are only created on demand.

   For more information on the implications of enabling huge pages, see
   `Wikipedia <http://en.wikipedia.org/wiki/Page_%28computer_memory%29#Page_size_trade-off>_`.

.. ts:cv:: CONFIG proxy.config.allocator.dontdump_iobuffers INT 1

  Enable (1) the exclusion of IO buffers from core files when ATS crashes on supported
  platforms.  (Currently only linux).  IO buffers are allocated with the MADV_DONTDUMP
  with madvise() on linux platforms that support MADV_DONTDUMP.  Enabled by default.

.. ts:cv:: CONFIG proxy.config.http.enabled INT 1

   Turn on or off support for HTTP proxying. This is rarely used, the one
   exception being if you run |TS| with a protocol plugin, and would
   like for it to not support HTTP requests at all.

.. ts:cv:: CONFIG proxy.config.http.allow_half_open INT 1
   :reloadable:
   :overridable:

   Turn on or off support for connection half open for client side. Default is on, so
   after client sends FIN, the connection is still there.

.. ts:cv:: CONFIG proxy.config.http.wait_for_cache INT 0

   Accepting inbound connections and starting the cache are independent
   operations in |TS|. This variable controls the relative timing of these
   operations and |TS| dependency on cache because if cache is required then
   inbound connection accepts should be deferred until the validity of the
   cache requirement is determined. Cache initialization failure will be logged
   in :file:`diags.log`.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Decouple inbound connections and cache initialization. Connections
         will be accepted as soon as possible and |TS| will run regardless of
         the results of cache initialization.
   ``1`` Do not accept inbound connections until cache initialization has
         finished. |TS| will run regardless of the results of cache
         initialization.
   ``2`` Do not accept inbound connections until cache initialization has
         finished and been sufficiently successful that cache is enabled. This
         means at least one cache span is usable. If there are no spans in
         :file:`storage.config` or none of the spans can be successfully parsed
         and initialized then |TS| will shut down.
   ``3`` Do not accept inbound connections until cache initialization has
         finished and been completely successful. This requires at least one
         cache span in :file:`storage.config` and that every span specified is
         valid and successfully initialized. Any error will cause |TS| to shut
         down.
   ===== ======================================================================

.. _Traffic Shaping:
                 https://cwiki.apache.org/confluence/display/TS/Traffic+Shaping
