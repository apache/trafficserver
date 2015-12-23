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

==============
records.config
==============

.. configfile:: records.config

The :file:`records.config` file (by default, located in
``/usr/local/etc/trafficserver/``) is a list of configurable variables used by
the Traffic Server software. Many of the variables in the
:file:`records.config` file are set automatically when you set configuration
options in Traffic Line. After you modify the
:file:`records.config` file,
run the command :option:`traffic_ctl config reload` to apply the changes.
When you apply changes to one node in a cluster, Traffic Server
automatically applies the changes to all other nodes in the cluster.

Format
======

Each variable has the following format::

   SCOPE variable_name DATATYPE variable_value

where

``SCOPE`` is related to clustering and is either ``CONFIG`` (all members of
the cluster) or ``LOCAL`` (only the local machine)

``DATATYPE`` is one of ``INT`` (integer), ``STRING`` (string), ``FLOAT``
(floating point).
:
A variable marked as ``Deprecated`` is still functional but should be avoided
as it may be removed in a future release without warning.

A variable marked as ``Reloadable`` can be updated via the command::

   traffic_ctl config reload

A variable marked as ``Overridable`` can be changed on a per-remap basis using plugins
(like the :ref:`conf-remap-plugin`).

``INT`` type configurations are expressed as any normal integer,
e.g. *32768*. They can also be expressed using more human readable values
using standard prefixes, e.g. *32K*. The following prefixes are supported
for all ``INT`` type configurations

   - ``K`` Kilobytes (1024 bytes)
   - ``M`` Megabytes (1024^2 or 1,048,576 bytes)
   - ``G`` Gigabytes (1024^3 or 1,073,741,824 bytes)
   - ``T`` Terabytes (1024^4 or 1,099,511,627,776 bytes)

.. note::

    Traffic Server currently writes back configurations to disk periodically,
    and when doing so, will not preserve the prefixes.

Examples
========

In the following example, the variable `proxy.config.proxy_name`_ is
a ``STRING`` datatype with the value ``my_server``. This means that the
name of the Traffic Server proxy is ``my_server``. ::

   CONFIG proxy.config.proxy_name STRING my_server

If the server name should be ``that_server`` the line would be ::

   CONFIG proxy.config.proxy_name STRING that_server

In the following example, the variable ``proxy.config.arm.enabled`` is
a yes/no flag. A value of ``0`` (zero) disables the option; a value of
``1`` enables the option. ::

   CONFIG proxy.config.arm.enabled INT 0

In the following example, the variable sets the cluster startup timeout
to 10 seconds. ::

   CONFIG proxy.config.cluster.startup_timeout INT 10

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

   $ PROXY_CONFIG_PRODUCT_COMPANY=example traffic_cop &
   $ traffic_ctl config get proxy.config.product_company

.. _configuration-variables:

Configuration Variables
=======================

The following list describes the configuration variables available in
the :file:`records.config` file.

System Variables
----------------

.. ts:cv:: CONFIG proxy.config.product_company STRING Apache Software Foundation

   The name of the organization developing Traffic Server.

.. ts:cv:: CONFIG proxy.config.product_vendor STRING Apache

   The name of the vendor providing Traffic Server.

.. ts:cv:: CONFIG proxy.config.product_name STRING Traffic Server

   The name of the product.

.. ts:cv:: CONFIG proxy.config.proxy_name STRING build_machine
   :reloadable:

   The name of the Traffic Server node.

.. ts:cv:: CONFIG proxy.config.bin_path STRING bin

   The location of the Traffic Server ``bin`` directory.

.. ts:cv:: CONFIG proxy.config.proxy_binary STRING traffic_server

   The name of the executable that runs the :program:`traffic_server` process.

.. ts:cv:: CONFIG proxy.config.proxy_binary_opts STRING -M

   The command-line options for starting Traffic Server.

.. ts:cv:: CONFIG proxy.config.manager_binary STRING traffic_manager

   The name of the executable that runs the :program:`traffic_manager` process.

.. ts:cv:: CONFIG proxy.config.env_prep STRING

   The script executed before the :program:`traffic_manager` process spawns
   the :program:`traffic_server` process.

.. ts:cv:: CONFIG proxy.config.config_dir STRING etc/trafficserver

   The directory that contains Traffic Server configuration files.
   This is a read-only configuration option that contains the
   ``SYSCONFDIR`` value specified at build time relative to the
   installation prefix. The ``$TS_ROOT`` environment variable can
   be used alter the installation prefix at run time.

.. ts:cv:: CONFIG proxy.config.syslog_facility STRING LOG_DAEMON

   The facility used to record system log files. Refer to :ref:`understanding-traffic-server-log-files`.

.. ts:cv:: CONFIG proxy.config.cop.core_signal INT 0

   The signal sent to :program:`traffic_cop`'s managed processes to stop them.

A value of ``0`` means no signal will be sent.

.. ts:cv:: CONFIG proxy.config.cop.linux_min_memfree_kb INT 0

   The minimum amount of free memory space allowed before Traffic Server stops
   the :program:`traffic_server` and :program:`traffic_manager` processes to
   prevent the system from hanging.

.. ts:cv:: CONFIG proxy.config.cop.linux_min_swapfree_kb INT 0

   The minimum amount of free swap space allowed before Traffic Server stops
   the :program:`traffic_server` and :program:`traffic_manager` processes to
   prevent the system from hanging. This configuration variable applies if
   swap is enabled in Linux 2.2 only.

.. ts:cv:: CONFIG proxy.config.cop.init_sleep_time INT 0

   The minimum amount of addtional duration allowed before Traffic Server detects
   that the :program:`traffic_server` is not responsive and attempts a restart
   during startup. This configuration variable allows Traffic Server a longer init
   time to load potentially large configuration files such as remap config. Note that
   this applies only during startup of Traffic Server and does not apply to the run
   time heartbeat checking.

.. ts:cv:: CONFIG proxy.config.output.logfile  STRING traffic.out

   The name and location of the file that contains warnings, status messages, and error messages produced by the Traffic Server
   processes. If no path is specified, then Traffic Server creates the file in its logging directory.

.. ts:cv:: CONFIG proxy.config.output.logfile.rolling_enabled INT 0
   :reloadable:

   Specifies how the output log is rolled. You can specify the following values:

   -  ``0`` = disables output log rolling
   -  ``1`` = enables output log rolling at specific intervals (specified with the
       :ts:cv:`proxy.config.output.logfile.rolling_interval_sec` variable). The "clock" starts ticking on Traffic Server boot
   -  ``2`` = enables output log rolling when the output log reaches a specific size (specified with the :ts:cv:`proxy.config.output.logfile.rolling_size_mb` variable)

.. ts:cv:: CONFIG proxy.config.output.logfile.rolling_interval_sec INT 3600
   :reloadable:
   :metric: seconds

   Specifies how often the output log is rolled, in seconds. The timer starts on Traffic Server bootup.

.. ts:cv:: CONFIG proxy.config.output.logfile.rolling_size_mb INT 100
   :reloadable:
   :metric: megabytes

   Specifies at what size to roll the output log at.

.. ts:cv:: CONFIG proxy.config.snapshot_dir STRING snapshots

   The directory in which Traffic Server stores configuration
   snapshots on the local system. Unless you specify an absolute
   path, this directory is located in the Traffic Server ``SYSCONFDIR``
   directory.

.. ts:cv:: CONFIG proxy.config.exec_thread.autoconfig INT 1

   When enabled (the default, ``1``), Traffic Server scales threads according to the available CPU cores. See the config option below.

.. ts:cv:: CONFIG proxy.config.exec_thread.autoconfig.scale FLOAT 1.5

   Factor by which Traffic Server scales the number of threads. The multiplier is usually the number of available CPU cores. By default
   this is scaling factor is ``1.5``.

.. ts:cv:: CONFIG proxy.config.exec_thread.limit INT 2

   The number of threads Traffic Server will create if `proxy.config.exec_thread.autoconfig` is set to `0`, otherwise this option is ignored.

.. ts:cv:: CONFIG proxy.config.accept_threads INT 1

   The number of accept threads Traffic Server. If disabled (``0``), then accepts will be done in each of the worker threads.

.. ts:cv:: CONFIG proxy.config.thread.default.stacksize  INT 1048576

   The new default thread stack size, for all threads. The original default is set at 1 MB.

.. ts:cv:: CONFIG proxy.config.exec_thread.affinity INT 1

   Bind threads to specific processing units.

===== ====================
Value Effect
===== ====================
0     assign threads to machine
1     assign threads to NUMA nodes [default]
2     assign threads to sockets
3     assign threads to cores
4     assign threads to processing units
===== ====================

.. note::

   This option only has an affect when Traffic Server has been compiled with ``--enable-hwloc``.

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

.. ts:cv:: CONFIG proxy.config.restart.active_client_threshold INT 0
   :reloadable:

  This setting specifies the number of active client connections
  for use by :option:`traffic_ctl server restart --drain`.

Network
=======

.. ts:cv:: CONFIG proxy.config.net.connections_throttle INT 30000

   The total number of client and origin server connections that the server
   can handle simultaneously. This is in fact the max number of file
   descriptors that the :program:`traffic_server` process can have open at any
   given time. Roughly 10% of these connections are reserved for origin server
   connections, i.e. from the default, only ~9,000 client connections can be
   handled. This should be tuned according to your memory size, and expected
   work load.

.. ts:cv:: CONFIG proxy.config.net.default_inactivity_timeout INT 86400
   :reloadable:

   The connection inactivity timeout (in seconds) to apply when
   Traffic Server detects that no inactivity timeout has been applied
   by the HTTP state machine. When this timeout is applied, the
   `proxy.process.net.default_inactivity_timeout_applied` metric
   is incremented.

.. ts:cv:: CONFIG proxy.config.net.inactivity_check_frequency INT 1

   How frequent (in seconds) to check for inactive connections. If you deal
   with a lot of concurrent connections, increasing this setting can reduce
   pressure on the system.

.. ts:cv:: LOCAL proxy.local.incoming_ip_to_bind STRING 0.0.0.0 [::]

   Controls the global default IP addresses to which to bind proxy server ports. The value is a space separated list of IP addresses, one per supported IP address family (currently IPv4 and IPv6).

Unless explicitly specified in `proxy.config.http.server_ports`_ the server port will be bound to one of these addresses, selected by IP address family. The built in default is any address. This is used if no address for a family is specified. This setting is useful if most or all server ports should be bound to the same address.

.. note::

   This is ignored for inbound transparent server ports because they must be able to accept connections on arbitrary IP addresses.

.. topic:: Example

   Set the global default for IPv4 to ``192.168.101.18`` and leave the global default for IPv6 as any address.::

      LOCAL proxy.local.incoming_ip_to_bind STRING 192.168.101.18

.. topic:: Example

   Set the global default for IPv4 to ``191.68.101.18`` and the global default for IPv6 to ``fc07:192:168:101::17``.::

      LOCAL proxy.local.incoming_ip_to_bind STRING 192.168.101.18 [fc07:192:168:101::17]

.. ts:cv:: LOCAL proxy.local.outgoing_ip_to_bind STRING 0.0.0.0 [::]

   This controls the global default for the local IP address for outbound connections to origin servers. The value is a list of space separated IP addresses, one per supported IP address family (currently IPv4 and IPv6).

   Unless explicitly specified in `proxy.config.http.server_ports`_ one of these addresses, selected by IP address family, will be used as the local address for outbound connections. This setting is useful if most or all of the server ports should use the same outbound IP addresses.

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

Cluster
=======

.. ts:cv:: LOCAL proxy.local.cluster.type INT 3

   Sets the clustering mode:

===== ====================
Value Effect
===== ====================
1     full-clustering mode
2     management-only mode
3     no clustering
===== ====================

.. ts:cv:: CONFIG proxy.config.cluster.ethernet_interface INT eth0

The network interface to be used for cluster communication. This has to be
identical on all members of a clsuter. ToDo: Is that reasonable ?? Should
this be local"

.. ts:cv:: CONFIG proxy.config.cluster.rsport INT 8088

   The reliable service port. The reliable service port is used to send configuration information between the nodes in a cluster. All nodes
   in a cluster must use the same reliable service port.

.. ts:cv:: CONFIG proxy.config.cluster.threads INT 1

   The number of threads for cluster communication. On heavy cluster, the number should be adjusted. It is recommend that take the thread
   CPU usage as a reference when adjusting.

.. ts:cv:: CONFIG proxy.config.clustger.ethernet_interface STRING

   Set the interface to use for cluster communications.

.. ts:cv:: CONFIG proxy.config.http.cache.cluster_cache_local INT 0
   :overridable:

   This turns on the local caching of objects in cluster mode. The point of
   this is to allow for popular or **hot** content to be cached on all nodes
   in a cluster. Be aware that the primary way to configure this behavior is
   via the :file:`cache.config` configuration file using
   ``action=cluster-cache-local`` directives.

   This particular :file:`records.config` configuration can be controlled per
   transaction or per remap rule. As such, it augments the
   :file:`cache.config` directives, since you can turn on the local caching
   feature without complex regular expression matching.

   This implies that turning this on in your global :file:`records.config` is
   almost never what you want; instead, you want to use this either via
   e.g. ``conf_remap.so`` overrides for a certain remap rule, or through a
   custom plugin using the appropriate APIs.

Local Manager
=============

.. ts:cv:: CONFIG proxy.config.admin.synthetic_port INT 8083

   The synthetic healthcheck port.

.. ts:cv:: CONFIG proxy.config.admin.number_config_bak INT 3

   The maximum number of copies of rolled configuration files to keep.

.. ts:cv:: CONFIG proxy.config.admin.user_id STRING nobody

   Option used to specify who to run the :program:`traffic_server` process as; also used to specify ownership of config and log files.

The nonprivileged user account designated to Traffic Server.

As of version 2.1.1 if the user_id is prefixed with pound character (#) the remaining of the string is considered to be
a `numeric user identifier <http://en.wikipedia.org/wiki/User_identifier>`_. If the value is set to ``#-1`` Traffic
Server will not change the user during startup.

Setting ``user_id`` to ``root`` or ``#0`` is now forbidden to
increase security. Trying to do so, will cause the
:program:`traffic_server` fatal failure. However there are two ways to
bypass that restriction

* Specify ``-DBIG_SECURITY_HOLE`` in ``CXXFLAGS`` during compilation.
* Set the ``user_id=#-1`` and start trafficserver as root.

.. ts:cv:: CONFIG proxy.config.admin.api.restricted INT 0

This setting specifies whether the management API should be restricted
to root processes. If this is set to ``0``, then on platforms that
support passing process credentials, non-root processes will be
allowed to make read-only management API calls. Any management API
calls that modify server state (eg. setting a configuration variable)
will still be restricted to root processes.

This setting is not reloadable, since it is must be applied when
program:`traffic_manager` initializes.

Process Manager
===============

.. ts:cv:: CONFIG proxy.config.process_manager.mgmt_port  INT 8084

   The port used for internal communication between the :program:`traffic_manager` and :program:`traffic_server` processes.

Alarm Configuration
===================

.. ts:cv:: CONFIG proxy.config.alarm_email STRING
   :reloadable:

   The address to which the alarm script should send email.

.. ts:cv:: CONFIG proxy.config.alarm.bin STRING example_alarm_bin.sh
   :reloadable:

   Name of the script file that can execute certain actions when
   an alarm is signaled. The script is invoked with up to 4 arguments:

       - the alarm message
       - the value of :ts:cv:`proxy.config.product_name`
       - the value of :ts:cv:`proxy.config.admin.user_id`
       - the value of :ts:cv:`proxy.config.alarm_email`

.. ts:cv:: CONFIG proxy.config.alarm.abs_path STRING NULL
   :reloadable:

   The absolute path to the directory containing the alarm script.
   If this is not set, the script will be located relative to
   :ts:cv:`proxy.config.bin_path`.

.. ts:cv:: CONFIG proxy.config.alarm.script_runtime INT 5
   :reloadable:

   The number of seconds that Traffic Server allows the alarm script
   to run before aborting it.

HTTP Engine
===========

.. ts:cv:: CONFIG proxy.config.http.server_ports STRING 8080

   Ports used for proxying HTTP traffic.

This is a list, separated by space or comma, of :index:`port descriptors`. Each descriptor is a sequence of keywords and values separated by colons. Not all keywords have values, those that do are specifically noted. Keywords with values can have an optional '=' character separating the keyword and value. The case of keywords is ignored. The order of keywords is irrelevant but unspecified results may occur if incompatible options are used (noted below). Options without values are idempotent. Options with values use the last (right most) value specified, except for ``ip-out`` as detailed later.

Quick reference chart.

=========== =============== ========================================
Name        Note            Definition
=========== =============== ========================================
*number*    **Required**    The local port.
blind                       Blind (``CONNECT``) port.
compress    **N/I**         Compressed. Not implemented.
ipv4        **Default**     Bind to IPv4 address family.
ipv6                        Bind to IPv6 address family.
ip-in       **Value**       Local inbound IP address.
ip-out      **Value**       Local outbound IP address.
ip-resolve  **Value**       IP address resolution style.
proto       **Value**       List of supported session protocols.
ssl                         SSL terminated.
tr-full                     Fully transparent (inbound and outbound)
tr-in                       Inbound transparent.
tr-out                      Outbound transparent.
tr-pass                     Pass through enabled.
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
   only. SPDY can be enabled on non-TLS proxy ports but that must be done explicitly.

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

   Listen on port 9090 for TSL enabled SPDY or HTTP connections, accept no other session protocols.::

      9090:proto=spdy;http:ssl

.. ts:cv:: CONFIG proxy.config.http.connect_ports STRING 443 563

   The range of origin server ports that can be used for tunneling via ``CONNECT``.

Traffic Server allows tunnels only to the specified ports.
Supports both wildcards ('\*') and ranges ("0-1023").

.. note::

   These are the ports on the *origin server*, not Traffic Server :ts:cv:`proxy ports <proxy.config.http.server_ports>`.

.. ts:cv:: CONFIG proxy.config.http.insert_request_via_str INT 1
   :reloadable:
   :overridable:

   Set how the ``Via`` field is handled on a request to the origin server.

===== ============================================
Value Effect
===== ============================================
0     Do not modify / set this via header
1     Update the via, with normal verbosity
2     Update the via, with higher verbosity
3     Update the via, with highest verbosity
===== ============================================

.. note::

   The ``Via`` header string can be decoded with the `Via Decoder Ring <http://trafficserver.apache.org/tools/via>`_.

.. ts:cv:: CONFIG proxy.config.http.insert_response_via_str INT 0
   :reloadable:
   :overridable:

   Set how the ``Via`` field is handled on the response to the client.

===== ============================================
Value Effect
===== ============================================
0     Do not modify / set this via header
1     Update the via, with normal verbosity
2     Update the via, with higher verbosity
3     Update the via, with highest verbosity
===== ============================================

.. note::

   The ``Via`` header string can be decoded with the `Via Decoder Ring <http://trafficserver.apache.org/tools/via>`_.

.. ts:cv:: CONFIG proxy.config.http.send_100_continue_response INT 0
   :reloadable:

   You can specify one of the following:

   -  ``0`` ATS buffer the request until the post body has been recieved and then send the request to origin.
   -  ``1`` immediately return a 100 Continue from ATS without waiting for the post body

.. ts:cv:: CONFIG proxy.config.http.response_server_enabled INT 1
   :reloadable:
   :overridable:

   You can specify one of the following:

   -  ``0`` no Server: header is added to the response.
   -  ``1`` the Server: header is added (see string below).
   -  ``2`` the Server: header is added only if the response from origin does not have one already.

.. ts:cv:: CONFIG proxy.config.http.insert_age_in_response INT 1
   :reloadable:
   :overridable:

   This option specifies whether Traffic Server should insert an ``Age`` header in the response. The Age field value is the cache's
   estimate of the amount of time since the response was generated or revalidated by the origin server.

   -  ``0`` no ``Age`` header is added
   -  ``1`` the ``Age`` header is added

.. ts:cv:: CONFIG proxy.config.http.response_server_str STRING ATS/
   :reloadable:
   :overridable:

   The Server: string that ATS will insert in a response header (if requested, see above). Note that the current version number is
   always appended to this string.

.. ts:cv:: CONFIG proxy.config.http.enable_url_expandomatic INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) ``.com`` domain expansion. This configures the Traffic Server to resolve unqualified hostnames by
   prepending with ``www.`` and appending with ``.com`` before redirecting to the expanded address. For example: if a client makes
   a request to ``host``, then Traffic Server redirects the request to ``www.host.com``.

.. ts:cv:: CONFIG proxy.config.http.chunking_enabled INT 1
   :reloadable:
   :overridable:

   Specifies whether Traffic Sever can generate a chunked response:

   -  ``0`` Never
   -  ``1`` Always
   -  ``2`` Generate a chunked response if the server has returned HTTP/1.1 before
   -  ``3`` = Generate a chunked response if the client request is HTTP/1.1 and the origin server has returned HTTP/1.1 before

   .. note::

       If HTTP/1.1 is used, then Traffic Server can use
       keep-alive connections with pipelining to origin servers. If
       HTTP/0.9 is used, then Traffic Server does not use ``keep-alive``
       connections to origin servers. If HTTP/1.0 is used, then Traffic
       Server can use ``keep-alive`` connections without pipelining to
       origin servers.

.. ts:cv:: CONFIG proxy.config.http.send_http11_requests INT 1
   :reloadable:
   :overridable:

   Specifies when and how Traffic Sever uses HTTP/1.1 to communicate with the origin server

   -  ``0`` Never
   -  ``1`` Always
   -  ``2`` If the server has returned HTTP/1.1 before
   -  ``3`` If the client request is HTTP/1.1 and the server has returned HTTP/1.1 before

   .. note::

       If :ts:cv:`proxy.config.http.use_client_target_addr` is set to 1, options 2 and 3 cause the proxy to use
       the client HTTP version for upstream requests.

.. ts:cv:: CONFIG proxy.config.http.auth_server_session_private INT 1

   If enabled (``1``) anytime a request contains a (``Authorization``), (``Proxy-Authorization``)
   or (``Www-Authenticate``) header the connection will be closed and not reused. This marks
   the connection as private. When disabled (``0``) the connection will be available for reuse.

.. ts:cv:: CONFIG proxy.config.http.server_session_sharing.match STRING both

   Enable and set the ability to re-use server connections across client connections. The valid values are

   none
      Do not match, do not re-use server sessions.

   ip
      Re-use server sessions, check only that the IP address and port of the origin server matches.

   host
      Re-use server sessions, check only that the fully qualified domain name matches.

   both
      Re-use server sessions, but only if the IP address and fully qualified domain name match.

   It is strongly recommended to use either *none* or *both* for this value unless you have a specific need to use *ip*
   or *host*. The most common reason is virtual hosts that share an IP address in which case performance can be enhanced
   if those sessions can be re-used. However, not all web servers support requests for different virtual hosts on the
   same connection so use with caution.

   .. note: Server sessions to different ports never match even if the FQDN and IP address match.

.. ts:cv:: CONFIG proxy.config.http.server_session_sharing.pool STRING thread

   Control the scope of server session re-use if it is enabled by :ts:cv:`proxy.config.http.server_session_sharing.match`. The valid values are

   global
      Re-use sessions from a global pool of all server sessions.

   thread
      Re-use sessions from a per-thread pool.

.. ts:cv:: CONFIG proxy.config.http.attach_server_session_to_client INT 0

   Control the re-use of an server session by a user agent (client) session.

   If a user agent performs more than one HTTP transaction on its connection to Traffic Server a server session must be
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

.. ts:cv:: CONFIG proxy.config.http.record_heartbeat INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) :program:`traffic_cop` heartbeat logging.

.. ts:cv:: CONFIG proxy.config.http.use_client_target_addr  INT 0

   For fully transparent ports use the same origin server address as the client.

This option causes Traffic Server to avoid where possible doing DNS
lookups in forward transparent proxy mode. The option is only
effective if the following three conditions are true -

*  Traffic Server is in forward proxy mode.
*  The proxy port is inbound transparent.
*  The target URL has not been modified by either remapping or a plugin.

If any of these conditions are not true, then normal DNS processing
is done for the connection.

There are three valid values.
*  0 - Disables the feature.
*  1 - Enables the feature with address verification.  The Proxy does the
regular DNS processing.  If the client-specified origin address is not in the
set of addresses found by the Proxy, the request continues to the client
specified address, but the result is not cached.
*  2 - Enables the feature with no address verification.  No DNS processing
is performed.  The result is cached (if allowed otherwise).  This option is
vulnerable to cache poisoning if an incorrect Host header is specified, so
this option should be used with extreme caution.  See bug TS-2954 for
details.

If all of these conditions are met, then the origin server IP
address is retrieved from the original client connection, rather
than through HostDB or DNS lookup. In effect, client DNS resolution
is used instead of Traffic Server DNS.

This can be used to be a little more efficient (looking up the
target once by the client rather than by both the client and Traffic
Server) but the primary use is when client DNS resolution can differ
from that of Traffic Server. Two known uses cases are:

#. Embedded IP addresses in a protocol with DNS load sharing. In
   this case, even though Traffic Server and the client both make
   the same request to the same DNS resolver chain, they may get
   different origin server addresses. If the address is embedded in
   the protocol then the overall exchange will fail. One current
   example is Microsoft Windows update, which presumably embeds the
   address as a security measure.

#. The client has access to local DNS zone information which is not
   available to Traffic Server. There are corporate nets with local
   DNS information for internal servers which, by design, is not
   propagated outside the core corporate network. Depending a
   network topology it can be the case that Traffic Server can
   access the servers by IP address but cannot resolve such
   addresses by name. In such as case the client supplied target
   address must be used.

This solution must be considered interim. In the longer term, it
should be possible to arrange for much finer grained control of DNS
lookup so that wildcard domain can be set to use Traffic Server or
client resolution. In both known use cases, marking specific domains
as client determined (rather than a single global switch) would
suffice. It is possible to do this crudely with this flag by
enabling it and then use identity URL mappings to re-disable it for
specific domains.

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

   Controls wether new POST requests re-use keep-alive sessions (``1``) or
   create new connections per request (``0``).

.. ts:cv:: CONFIG proxy.config.http.disallow_post_100_continue INT 0

   Allows you to return a 405 Method Not Supported with Posts also
   containing an Expect: 100-continue.

   When a Post w/ Expect: 100-continue is blocked the stat
   proxy.process.http.disallowed_post_100_continue will be incremented.

Parent Proxy Configuration
==========================

.. ts:cv:: CONFIG proxy.config.http.parent_proxy_routing_enable INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) the parent caching option. Refer to :ref:`admin-hierarchical-caching`.

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.retry_time INT 300
   :reloadable:

   The amount of time allowed between connection retries to a parent cache that is unavailable.

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.fail_threshold INT 10
   :reloadable:

   The number of times the connection to the parent cache can fail before Traffic Server considers the parent unavailable.

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.total_connect_attempts INT 4
   :reloadable:

   The total number of connection attempts allowed to a parent cache before Traffic Server bypasses the parent or fails the request
   (depending on the ``go_direct`` option in the :file:`parent.config` file).

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.per_parent_connect_attempts INT 2
   :reloadable:

   The total number of connection attempts allowed per parent, if multiple parents are used.

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.connect_attempts_timeout INT 30
   :reloadable:

   The timeout value (in seconds) for parent cache connection attempts.

.. ts:cv:: CONFIG proxy.config.http.forward.proxy_auth_to_parent INT 0
   :reloadable:
   :overridable:

   Configures Traffic Server to send proxy authentication headers on to the parent cache.

.. ts:cv:: CONFIG proxy.config.http.no_dns_just_forward_to_parent INT 0
   :reloadable:

   Don't try to resolve DNS, forward all DNS requests to the parent. This is off (``0``) by default.

HTTP Connection Timeouts
========================

.. ts:cv:: CONFIG proxy.config.http.keep_alive_no_activity_timeout_in INT 115
   :reloadable:
   :overridable:

   Specifies how long Traffic Server keeps connections to clients open for a
   subsequent request after a transaction ends. A value of ``0`` will disable
   the no activity timeout.

.. ts:cv:: CONFIG proxy.config.http.keep_alive_no_activity_timeout_out INT 120
   :reloadable:
   :overridable:

   Specifies how long Traffic Server keeps connections to origin servers open
   for a subsequent transfer of data after a transaction ends. A value of
   ``0`` will disable the no activity timeout.

.. ts:cv:: CONFIG proxy.config.http.transaction_no_activity_timeout_in INT 30
   :reloadable:
   :overridable:

   Specifies how long Traffic Server keeps connections to clients open if a transaction stalls.

.. ts:cv:: CONFIG proxy.config.http.transaction_no_activity_timeout_out INT 30
   :reloadable:
   :overridable:

   Specifies how long Traffic Server keeps connections to origin servers open if the transaction stalls.

.. ts:cv:: CONFIG proxy.config.http.transaction_active_timeout_in INT 900
   :reloadable:

   The maximum amount of time Traffic Server can remain connected to a client. If the transfer to the client is not complete before this
   timeout expires, then Traffic Server closes the connection.

The value of ``0`` specifies that there is no timeout.

.. ts:cv:: CONFIG proxy.config.http.transaction_active_timeout_out INT 0
   :reloadable:
   :overridable:

   The maximum amount of time Traffic Server waits for fulfillment of a connection request to an origin server. If Traffic Server does not
   complete the transfer to the origin server before this timeout expires, then Traffic Server terminates the connection request.

The default value of ``0`` specifies that there is no timeout.

.. ts:cv:: CONFIG proxy.config.http.accept_no_activity_timeout INT 120
   :reloadable:

   The timeout interval in seconds before Traffic Server closes a connection that has no activity.

.. ts:cv:: CONFIG proxy.config.http.background_fill_active_timeout INT 0
   :reloadable:
   :overridable:

   Specifies how long Traffic Server continues a background fill before giving up and dropping the origin server connection.

.. ts:cv:: CONFIG proxy.config.http.background_fill_completed_threshold FLOAT 0.0
   :reloadable:
   :overridable:

   The proportion of total document size already transferred when a client aborts at which the proxy continues fetching the document
   from the origin server to get it into the cache (a **background fill**).

HTTP Redirection
================

.. ts:cv:: CONFIG proxy.config.http.redirection_enabled INT 0

   This setting indicates whether Trafficserver does a redirect follow location on receiving a 3XX Redirect response from the Origin
   server. The redirection attempt is transparent to the client and the client is served the final response from the redirected-to
   location.

.. ts:cv:: CONFIG proxy.config.http.number_of_redirections INT 1

   This setting determines the maximum number of times Trafficserver does a redirect follow location on receiving a 3XX Redirect response
   for a given client request.

.. ts:cv:: CONFIG proxy.config.http.redirect_host_no_port INT 1

   This setting enables Trafficserver to not include the port in the Host header in the redirect follow request for default/standard ports
   (e.g. 80 for HTTP and 443 for HTTPS). Note that the port is still included in the Host header if it's non-default.

.. ts:cv:: CONFIG proxy.config.http.redirect_use_orig_cache_key INT 0

   This setting enables Trafficserver to allow using original request cache key (for example, set using a TS API) during a 3xx redirect follow.
   The default behavior (0) is to use the URL specified by Location header in the 3xx response as the cache key.

Origin Server Connect Attempts
==============================

.. ts:cv:: CONFIG proxy.config.http.connect_attempts_max_retries INT 3
   :reloadable:
   :overridable:

   The maximum number of connection retries Traffic Server can make when the origin server is not responding.
   Each retry attempt lasts for `proxy.config.http.connect_attempts_timeout`_ seconds.  Once the maximum number of retries is
   reached, the origin is marked dead.  After this, the setting  `proxy.config.http.connect_attempts_max_retries_dead_server`_
   is used to limit the number of retry attempts to the known dead origin.

.. ts:cv:: CONFIG proxy.config.http.connect_attempts_max_retries_dead_server INT 1
   :reloadable:
   :overridable:

   Maximum number of connection retries Traffic Server can make while an origin is marked dead.  Typically this value is smaller than
   `proxy.config.http.connect_attempts_max_retries`_ so an error is returned to the client faster and also to reduce the load on the dead origin.
   The timeout interval `proxy.config.http.connect_attempts_timeout`_ in seconds is used with this setting.

.. ts:cv:: CONFIG proxy.config.http.server_max_connections INT 0
   :reloadable:

   Limits the number of socket connections across all origin servers to the value specified. To disable, set to zero (``0``).

   .. note::
        This value is used in determining when and if to prune active origin sessions. Without this value set connections
        to origins can consume all the way up to ts:cv:`proxy.config.net.connections_throttle` connections, which in turn can
        starve incoming requests from available connections.

.. ts:cv:: CONFIG proxy.config.http.origin_max_connections INT 0
   :reloadable:
   :overridable:

   Limits the number of socket connections per origin server to the value specified. To enable, set to one (``1``).

.. ts:cv:: CONFIG proxy.config.http.origin_min_keep_alive_connections INT 0
   :reloadable:

   As connection to an origin server are opened, keep at least 'n' number of connections open to that origin, even if
   the connection isn't used for a long time period. Useful when the origin supports keep-alive, removing the time
   needed to set up a new connection from
   the next request at the expense of added (inactive) connections. To enable, set to one (``1``).

.. ts:cv:: CONFIG proxy.config.http.connect_attempts_rr_retries INT 3
   :reloadable:
   :overridable:

   The maximum number of failed connection attempts allowed before a round-robin entry is marked as 'down' if a server
   has round-robin DNS entries.

.. ts:cv:: CONFIG proxy.config.http.connect_attempts_timeout INT 30
   :reloadable:
   :overridable:

   The timeout value (in seconds) for **time to first byte** for an origin server connection.

.. ts:cv:: CONFIG proxy.config.http.post_connect_attempts_timeout INT 1800
   :reloadable:
   :overridable:

   The timeout value (in seconds) for an origin server connection when the client request is a ``POST`` or ``PUT``
   request.

.. ts:cv:: CONFIG proxy.config.http.down_server.cache_time INT 60
   :reloadable:
   :overridable:

   Specifies how long (in seconds) Traffic Server remembers that an origin server was unreachable.

.. ts:cv:: CONFIG proxy.config.http.down_server.abort_threshold INT 10
   :reloadable:
   :overridable:

   The number of seconds before Traffic Server marks an origin server as unavailable after a client abandons a request
   because the origin server was too slow in sending the response header.

.. ts:cv:: CONFIG proxy.config.http.uncacheable_requests_bypass_parent INT 1

   When enabled (1), Traffic Server bypasses the parent proxy for a request that is not cacheable.

Congestion Control
==================

.. ts:cv:: CONFIG proxy.config.http.congestion_control.enabled INT 0

   Enables (``1``) or disables (``0``) the Congestion Control option, which configures Traffic Server to stop forwarding
   HTTP requests to origin servers when they become congested. Traffic Server sends the client a message to retry the
   congested origin server later. Refer to :ref:`using-congestion-control`.

.. ts:cv:: CONFIG proxy.config.http.flow_control.enabled INT 0
   :overridable:

   Transaction buffering / flow control is enabled if this is set to a non-zero value. Otherwise no flow control is done.

.. ts:cv:: CONFIG proxy.config.http.flow_control.high_water INT 0
   :metric: bytes
   :overridable:

   The high water mark for transaction buffer control. External source I/O is halted when the total buffer space in use
   by the transaction exceeds this value.

.. ts:cv:: CONFIG proxy.config.http.flow_control.low_water INT 0
   :metric: bytes
   :overridable:

   The low water mark for transaction buffer control. External source I/O is resumed when the total buffer space in use
   by the transaction is no more than this value.

.. ts:cv:: CONFIG proxy.config.http.websocket.max_number_of_connections INT -1
   :reloadable:

   When enabled >= (``0``), Traffic Server will enforce a maximum number of simultaneous websocket connections.

Negative Response Caching
=========================

.. ts:cv:: CONFIG proxy.config.http.negative_caching_enabled INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), Traffic Server caches negative responses (such as ``404 Not Found``) when a requested page does
   not exist. The next time a client requests the same page, Traffic Server serves the negative response directly from
   cache. When disabled (``0``) Traffic Server will only cache the response if the response has ``Cache-Control`` headers.

   .. note::

      The following negative responses are cached by Traffic Server:::

         204  No Content
         305  Use Proxy
         400  Bad Request
         403  Forbidden
         404  Not Found
         405  Method Not Allowed
         500  Internal Server Error
         501  Not Implemented
         502  Bad Gateway
         503  Service Unavailable
         504  Gateway Timeout

   The cache lifetime for objects cached from this setting is controlled via
   :ts:cv:`proxy.config.http.negative_caching_lifetime`.

.. ts:cv:: CONFIG proxy.config.http.negative_caching_lifetime INT 1800
   :overridable:

   How long (in seconds) Traffic Server keeps the negative responses  valid in cache. This value only affects negative
   responses that do NOT have explicit ``Expires:`` or ``Cache-Control:`` lifetimes set by the server.

Proxy User Variables
====================

.. ts:cv:: CONFIG proxy.config.http.anonymize_remove_from INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), Traffic Server removes the ``From`` header to protect the privacy of your users.

.. ts:cv:: CONFIG proxy.config.http.anonymize_remove_referer INT 0
   :reloadable:

   When enabled (``1``), Traffic Server removes the ``Referrer`` header to protect the privacy of your site and users.

.. ts:cv:: CONFIG proxy.config.http.anonymize_remove_user_agent INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), Traffic Server removes the ``User-agent`` header to protect the privacy of your site and users.

.. ts:cv:: CONFIG proxy.config.http.anonymize_remove_cookie INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), Traffic Server removes the ``Cookie`` header to protect the privacy of your site and users.

.. ts:cv:: CONFIG proxy.config.http.anonymize_remove_client_ip INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), Traffic Server removes ``Client-IP`` headers for more privacy.

.. ts:cv:: CONFIG proxy.config.http.anonymize_insert_client_ip INT 1
   :reloadable:
   :overridable:

   When enabled (``1``), Traffic Server inserts ``Client-IP`` headers to retain the client IP address.

.. ts:cv:: CONFIG proxy.config.http.anonymize_other_header_list STRING NULL
   :reloadable:

   Comma separated list of headers Traffic Server should remove from outgoing requests.

.. ts:cv:: CONFIG proxy.config.http.insert_squid_x_forwarded_for INT 1
   :reloadable:
   :overridable:

   When enabled (``1``), Traffic Server adds the client IP address to the ``X-Forwarded-For`` header.

.. ts:cv:: CONFIG proxy.config.http.normalize_ae_gzip INT 1
   :reloadable:
   :overridable:

   Enable (``1``) to normalize all ``Accept-Encoding:`` headers to one of the following:

   -  ``Accept-Encoding: gzip`` (if the header has ``gzip`` or ``x-gzip`` with any ``q``) **OR**
   -  *blank* (for any header that does not include ``gzip``)

   This is useful for minimizing cached alternates of documents (e.g. ``gzip, deflate`` vs. ``deflate, gzip``). Enabling this option is
   recommended if your origin servers use no encodings other than ``gzip``.

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

Cache Control
=============

.. ts:cv:: CONFIG proxy.config.cache.enable_read_while_writer INT 1
   :reloadable:

   Specifies when to enable the ability to read a cached object while another
   connection is completing the write to cache for that same object. The goal
   here is to avoid multiple origin connections for the same cacheable object
   upon a cache miss. The possible values of this config are:

   -  ``0`` = never read while writing
   -  ``1`` = always read while writing
   -  ``2`` = always read while writing, but allow non-cached Range requests through to the origin

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
   Traffic Server will do the right thing. Misaligned partitions on Linux are auto-detected.

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

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_client_no_cache INT 1
   :reloadable:
   :overridable:

   When enabled (``1``), Traffic Server ignores client requests to bypass the cache.

.. ts:cv:: CONFIG proxy.config.http.cache.ims_on_client_no_cache INT 1
   :reloadable:
   :overridable:

   When enabled (``1``), Traffic Server issues a conditional request to the origin server if an incoming request has a ``No-Cache`` header.

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_server_no_cache INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), Traffic Server ignores origin server requests to bypass the cache.

.. ts:cv:: CONFIG proxy.config.http.cache.cache_responses_to_cookies INT 1
   :reloadable:
   :overridable:

   Specifies how cookies are cached:

   -  ``0`` = do not cache any responses to cookies
   -  ``1`` = cache for any content-type
   -  ``2`` = cache only for image types
   -  ``3`` = cache for all but text content-types

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_authentication INT 0
   :overridable:

   When enabled (``1``), Traffic Server ignores ``WWW-Authentication`` headers in responses ``WWW-Authentication`` headers are removed and
   not cached.

.. ts:cv:: CONFIG proxy.config.http.cache.cache_urls_that_look_dynamic INT 1
   :reloadable:
   :overridable:

   Enables (``1``) or disables (``0``) caching of URLs that look dynamic, i.e.: URLs that end in ``.asp`` or contain a question
   mark (``?``), a semicolon (``;``), or ``cgi``. For a full list, please refer to
   `HttpTransact::url_looks_dynamic </link/to/doxygen>`_

.. ts:cv:: CONFIG proxy.config.http.cache.enable_default_vary_headers INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) caching of alternate versions of HTTP objects that do not contain the ``Vary`` header.

.. ts:cv:: CONFIG proxy.config.http.cache.when_to_revalidate INT 0
   :reloadable:
   :overridable:

   Specifies when to revalidate content:

   -  ``0`` = use cache directives or heuristic (the default value)
   -  ``1`` = stale if heuristic
   -  ``2`` = always stale (always revalidate)
   -  ``3`` = never stale
   -  ``4`` = use cache directives or heuristic (0) unless the request
       has an ``If-Modified-Since`` header

   If the request contains the ``If-Modified-Since`` header, then
   Traffic Server always revalidates the cached content and uses the
   client's ``If-Modified-Since`` header for the proxy request.

.. ts:cv:: CONFIG proxy.config.http.cache.required_headers INT 2
   :reloadable:
   :overridable:

   The type of headers required in a request for the request to be cacheable.

   -  ``0`` = no headers required to make document cacheable
   -  ``1`` = either the ``Last-Modified`` header, or an explicit lifetime header, ``Expires`` or ``Cache-Control: max-age``, is required
   -  ``2`` = explicit lifetime is required, ``Expires`` or ``Cache-Control: max-age``

.. ts:cv:: CONFIG proxy.config.http.cache.max_stale_age INT 604800
   :reloadable:
   :overridable:

   The maximum age allowed for a stale response before it cannot be cached.

.. ts:cv:: CONFIG proxy.config.http.cache.range.lookup INT 1
   :overridable:

   When enabled (``1``), Traffic Server looks up range requests in the cache.

.. ts:cv:: CONFIG proxy.config.http.cache.range.write INT 0
   :overridable:

   When enabled (``1``), Traffic Server will attempt to write (lock) the URL
   to cache. This is rarely useful (at the moment), since it'll only be able
   to write to cache if the origin has ignored the ``Range:`` header. For a use
   case where you know the origin will respond with a full (``200``) response,
   you can turn this on to allow it to be cached.

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_accept_mismatch INT 2
   :reloadable:

   When enabled with a value of ``1``, Traffic Server serves documents from cache with a
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

   When enabled with a value of ``1``, Traffic Server serves documents from cache with a
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

   When enabled with a value of ``1``, Traffic Server serves documents from cache with a
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

   When enabled with a value of ``1``, Traffic Server serves documents from cache with a
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

   When enabled (``1``), Traffic Server ignores any ``Cache-Control:
   max-age`` headers from the client. This technically violates the HTTP RFC,
   but avoids a problem where a client can forcefully invalidate a cached object.

.. ts:cv:: CONFIG proxy.config.cache.max_doc_size INT 0

   Specifies the maximum object size that will be cached. ``0`` is unlimited.

.. ts:cv:: CONFIG proxy.config.cache.permit.pinning INT 0
   :reloadable:

   When enabled (``1``), Traffic Server will keep certain HTTP objects in the cache for a certain time as specified in cache.config.

.. ts:cv:: CONFIG proxy.config.cache.hit_evacuate_percent INT 0

   The size of the region (as a percentage of the total content storage in a :term:`cache stripe`) in front of the
   :term:`write cursor` that constitutes a recent access hit for evacutating the accessed object.

   When an object is accessed it can be marked for evacuation, that is to be copied over the write cursor and
   thereby preserved from being overwritten. This is done if it is no more than a specific number of bytes in front of
   the write cursor. The number of bytes is a percentage of the total number of bytes of content storage in the cache
   stripe where the object is stored and that percentage is set by this variable.

   By default, the feature is off (set to 0).

.. ts:cv:: CONFIG proxy.config.cache.hit_evacuate_size_limit INT 0
   :metric: bytes

   Limit the size of objects that are hit evacuated.

   Objects larger than the limit are not hit evacuated. A value of 0 disables the limit.

.. ts:cv:: CONFIG proxy.config.cache.limits.http.max_alts INT 5

   The maximum number of alternates that are allowed for any given URL.
   Disable by setting to 0.

.. ts:cv:: CONFIG proxy.config.cache.target_fragment_size INT 1048576

   Sets the target size of a contiguous fragment of a file in the disk cache.
   When setting this, consider that larger numbers could waste memory on slow
   connections, but smaller numbers could increase (waste) seeks.

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

.. ts:cv:: CONFIG proxy.config.cache.ram_cache.algorithm INT 0

   Two distinct RAM caches are supported, the default (0) being the **CLFUS**
   (*Clocked Least Frequently Used by Size*). As an alternative, a simpler
   **LRU** (*Least Recently Used*) cache is also available, by changing this
   configuration to 1.

.. ts:cv:: CONFIG proxy.config.cache.ram_cache.use_seen_filter INT 0

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

   - ``0`` = no compression
   - ``1`` = fastlz (extremely fast, relatively low compression)
   - ``2`` = libz (moderate speed, reasonable compression)
   - ``3`` = liblzma (very slow, high compression)

   .. note::

      Compression runs on task threads.  To use more cores for RAM cache compression, increase :ts:cv:`proxy.config.task_threads`.

Heuristic Expiration
====================

.. ts:cv:: CONFIG proxy.config.http.cache.heuristic_min_lifetime INT 3600
   :reloadable:
   :overridable:

   The minimum amount of time an HTTP object without an expiration date can remain fresh in the cache before is
   considered to be stale.

.. ts:cv:: CONFIG proxy.config.http.cache.heuristic_max_lifetime INT 86400
   :reloadable:
   :overridable:

   The maximum amount of time an HTTP object without an expiration date can remain fresh in the cache before is
   considered to be stale.

.. ts:cv:: CONFIG proxy.config.http.cache.heuristic_lm_factor FLOAT 0.10
   :reloadable:
   :overridable:

   The aging factor for freshness computations. Traffic Server stores an object for this percentage of the time that
   elapsed since it last changed.

.. ts:cv:: CONFIG proxy.config.http.cache.fuzz.time INT 240
   :reloadable:
   :overridable:

   How often Traffic Server checks for an early refresh, during the period before the document stale time. The interval
   specified must be in seconds. See :ref:`fuzzy-revalidation`

.. ts:cv:: CONFIG proxy.config.http.cache.fuzz.probability FLOAT 0.005
   :reloadable:
   :overridable:

   The probability that a refresh is made on a document during the specified fuzz time.

.. ts:cv:: CONFIG proxy.config.http.cache.fuzz.min_time INT 0
   :reloadable:
   :overridable:

   Handles requests with a TTL less than fuzz.time – it allows for different times to evaluate the probability of revalidation for small TTLs and big TTLs. Objects with small TTLs will start "rolling the revalidation dice" near the fuzz.min_time, while objects with large TTLs would start at fuzz.time. A logarithmic like function between determines the revalidation evaluation start time (which will be between fuzz.min_time and fuzz.time). As the object gets closer to expiring, the window start becomes more likely. By default this setting is not enabled, but should be enabled anytime you have objects with small TTLs. The default value is ``0``.

Dynamic Content & Content Negotiation
=====================================

.. ts:cv:: CONFIG proxy.config.http.cache.vary_default_text STRING NULL
   :reloadable:

   The header on which Traffic Server varies for text documents.

For example: if you specify ``User-agent``, then Traffic Server caches
all the different user-agent versions of documents it encounters.

.. ts:cv:: CONFIG proxy.config.http.cache.vary_default_images STRING NULL
   :reloadable:

   The header on which Traffic Server varies for images.

.. ts:cv:: CONFIG proxy.config.http.cache.vary_default_other STRING NULL
   :reloadable:

   The header on which Traffic Server varies for anything other than text and images.

.. ts:cv:: CONFIG proxy.config.http.cache.open_read_retry_time INT 10
   :reloadable:

    The number of milliseconds a cacheable request will wait before requesting the object from cache if an equivalent request is in flight.

.. ts:cv:: CONFIG proxy.config.http.cache.max_open_read_retries INT -1
   :reloadable:

    The number of times to attempt fetching an object from cache if there was an equivalent request in flight.

.. ts:cv:: CONFIG proxy.config.http.cache.max_open_write_retries INT 1
   :reloadable:

    The number of times to attempt a cache open write upon failure to get a write lock.

.. ts:cv:: CONFIG proxy.config.http.cache.open_write_fail_action INT 0
   :reloadable:

    This setting indicates the action taken on failing to obtain the cache open write lock on either a cache miss or a cache
    hit stale. This typically happens when there is more than one request to the same cache object simultaneously. During such
    a scenario, all but one (which goes to the origin) request is served either a stale copy or an error depending on this
    setting.

   -  ``0`` = default, disable cache and goto origin server
   -  ``1`` = return a 502 error on a cache miss
   -  ``2`` = serve stale if object's age is under :ts:cv:`proxy.config.http.cache.max_stale_age`, else, goto origin server
   -  ``3`` = return a 502 error on a cache miss or serve stale on a cache revalidate
              if object's age is under :ts:cv:`proxy.config.http.cache.max_stale_age`, else, goto origin server
   -  ``4`` = return a 502 error on either a cache miss or on a revalidate

Customizable User Response Pages
================================

.. ts:cv:: CONFIG proxy.config.body_factory.enable_customizations INT 1

   Specifies whether customizable response pages are language specific
   or not:

   -  ``1`` = enable customizable user response pages in the default directory only
   -  ``2`` = enable language-targeted user response pages
   -  ``3`` = enable host-targeted user response pages

.. ts:cv:: CONFIG proxy.config.body_factory.enable_logging INT 0

   Enables (``1``) or disables (``0``) logging for customizable response pages. When enabled, Traffic Server records a message in
   the error log each time a customized response page is used or modified.

.. ts:cv:: CONFIG proxy.config.body_factory.template_sets_dir STRING etc/trafficserver/body_factory

   The customizable response page default directory. If this is a
   relative path, Traffic Server resolves it relative to the
   ``PREFIX`` directory.

.. ts:cv:: CONFIG proxy.config.body_factory.template_base STRING ""
    :reloadable:
    :overridable:

    A prefix for the file name to use to find an error template file. If set (not the empty string)
    this value and an underscore are predended to the file name to find in the template sets
    directory. See :ref:`body-factory`.

.. ts:cv:: CONFIG proxy.config.body_factory.response_suppression_mode INT 0

   Specifies when Traffic Server suppresses generated response pages:

   -  ``0`` = never suppress generated response pages
   -  ``1`` = always suppress generated response pages
   -  ``2`` = suppress response pages only for intercepted traffic

.. ts:cv:: CONFIG proxy.config.http_ui_enabled INT 0

   Specifies which http UI endpoints to allow within :file:`remap.config`:

   -  ``0`` = disable all http UI endpoints
   -  ``1`` = enable only cache endpoints
   -  ``2`` = enable only stats endpoints
   -  ``3`` = enable all http UI endpoints

   To enable any enpoint there needs to be an entry in :file:`remap.config` which
   specifically enables it. Such a line would look like: ::

        map / http://{stat}

   The following are the cache endpoints:

   - ``cache`` = UI to interact with the cache

   The following are the stats endpoints:

   - ``cache-internal`` = statistics about cache evacuation and volumes
   - ``hostdb`` = lookups against the hostdb
   - ``http`` = HTTPSM details, this endpoint is also gated by `proxy.config.http.enable_http_info`
   - ``net`` = lookup and listing of open connections
   - ``stat`` = list of all records.config options and metrics
   - ``test`` = test callback page

.. ts:cv:: CONFIG proxy.config.http.enable_http_info INT 0

   Enables (``1``) or disables (``0``) access to an endpoint within `proxy.config.http_ui_enabled`
   which shows details about inflight transactions (HttpSM).

DNS
===

.. ts:cv:: CONFIG proxy.config.dns.search_default_domains INT 0
   :Reloadable:

   - ``0`` = disables local domain expansion
   - ``1`` = enable local domain expansion
   - ``2`` = enable local domain expansion, but restrain splitting local domain name

Traffic Server can attempt to resolve unqualified hostnames by
expanding to the local domain. For example if a client makes a
request to an unqualified host (``host_x``) and the Traffic Server
local domain is ``y.com`` , then Traffic Server will expand the
hostname to ``host_x.y.com``.

.. ts:cv:: CONFIG proxy.config.dns.splitDNS.enabled INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) DNS server selection. When enabled, Traffic Server refers to the :file:`splitdns.config` file for
   the selection specification. Refer to :ref:`Configuring DNS Server Selection <admin-split-dns>`.

.. ts:cv:: CONFIG proxy.config.dns.url_expansions STRING NULL

   Specifies a list of hostname extensions that are automatically added to the hostname after a failed lookup. For example: if you want
   Traffic Server to add the hostname extension .org, then specify ``org`` as the value for this variable (Traffic Server automatically
   adds the dot (.)).

.. note::

   If the variable :ts:cv:`proxy.config.http.enable_url_expandomatic` is set to ``1`` (the default value), then you do not have to
   add ``www.`` and ``.com`` to this list because Traffic Server automatically tries www. and .com after trying the values
   you've specified.

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

HostDB
======

.. ts:cv:: CONFIG proxy.config.hostdb.lookup_timeout INT 30
   :metric: seconds
   :reloadable:

   Time to wait for a DNS response in seconds.

.. ts:cv:: CONFIG proxy.config.hostdb.serve_stale_for INT
   :metric: seconds
   :reloadable:

   The number of seconds for which to use a stale NS record while initiating a
   background fetch for the new data.

   If not set then stale records are not served.

.. ts:cv:: CONFIG proxy.config.hostdb.storage_size INT 33554432
   :metric: bytes

   The amount of space (in bytes) used to store ``hostdb``.
   The value of this variable must be increased if you increase the size of the
   `proxy.config.hostdb.size`_ variable.

.. ts:cv:: CONFIG proxy.config.hostdb.size INT 120000

   The maximum number of entries that can be stored in the database.

.. note::

   For values above ``200000``, you must increase :ts:cv:`proxy.config.hostdb.storage_size` by at least 44 bytes per entry.

.. ts:cv:: CONFIG proxy.config.hostdb.ttl_mode INT 0
   :reloadable:

   A host entry will eventually time out and be discarded. This variable controls how that time is calculated. A DNS
   request will return a TTL value and an internal value can be set with :ts:cv:`proxy.config.hostdb.timeout`. This
   variable determines which value will be used.

   =====    ===
   Value    TTL
   =====    ===
   0        The TTL from the DNS response.
   1        The internal timeout value.
   2        The smaller of the DNS and internal TTL values. The internal timeout value becomes a maximum TTL.
   3        The larger of the DNS and internal TTL values. The internal timeout value become a minimum TTL.
   =====    ===

.. ts:cv:: CONFIG proxy.config.hostdb.timeout INT 1440
   :metric: minutes
   :reloadable:

   Internal time to live value for host DB entries, **in minutes**.

   See :ts:cv:`proxy.config.hostdb.ttl_mode` for when this value is used.

.. ts:cv:: CONFIG proxy.config.hostdb.strict_round_robin INT 0
   :reloadable:

   Set host resolution to use strict round robin.

When this and :ts:cv:`proxy.config.hostdb.timed_round_robin` are both disabled (set to ``0``), Traffic Server always
uses the same origin server for the same client, for as long as the origin server is available. Otherwise if this is
set then IP address is rotated on every request. This setting takes precedence over
:ts:cv:`proxy.config.hostdb.timed_round_robin`.

.. ts:cv:: CONFIG proxy.config.hostdb.timed_round_robin INT 0
   :reloadable:

   Set host resolution to use timed round robin.

When this and :ts:cv:`proxy.config.hostdb.strict_round_robin` are both disabled (set to ``0``), Traffic Server always
uses the same origin server for the same client, for as long as the origin server is available. Otherwise if this is
set to :arg:`N` the IP address is rotated if more than :arg:`N` seconds have past since the first time the
current address was used.

.. ts:cv:: CONFIG proxy.config.hostdb.host_file.path STRING NULL

   Set the file path for an external host file.

If this is set (non-empty) then the file is presumed to be a hosts file in the standard `host file format <http://tools.ietf.org/html/rfc1123#page-13>`_. It is read and the entries there added to the HostDB. The file is periodically checked for a more recent modification date in which case it is reloaded. The interval is set by the value :ts:cv:`proxy.config.hostdb.host_file.interval`.

While not technically reloadable, the value is read every time the file is to be checked so that if changed the new
value will be used on the next check and the file will be treated as modified.

.. ts:cv:: CONFIG proxy.config.hostdb.host_file.interval INT 86400
   :metric: seconds
   :reloadable:

   Set the file changed check timer for :ts:cv:`proxy.config.hostdb.host_file.path`.

The file is checked every this many seconds to see if it has changed. If so the HostDB is updated with the new values in the file.

.. ts:cv:: CONFIG proxy.config.hostdb.ip_resolve STRING NULL

   Set the host resolution style.

This is an ordered list of keywords separated by semicolons that specify how a host name is to be resolved to an IP address. The keywords are case
insensitive.

=======  =======
Keyword  Meaning
=======  =======
ipv4     Resolve to an IPv4 address.
ipv6     Resolve to an IPv6 address.
client   Resolve to the same family as the client IP address.
none     Stop resolving.
=======  =======

The order of the keywords is critical. When a host name needs to be resolved it is resolved in same order as the
keywords. If a resolution fails, the next option in the list is tried. The keyword ``none`` means to give up resolution
entirely. The keyword list has a maximum length of three keywords, more are never needed. By default there is an
implicit ``ipv4;ipv6`` attached to the end of the string unless the keyword ``none`` appears.

.. topic:: Example

   Use the incoming client family, then try IPv4 and IPv6. ::

      client;ipv4;ipv6

   Because of the implicit resolution this can also be expressed as just ::

      client

.. topic:: Example

   Resolve only to IPv4. ::

      ipv4;none

.. topic:: Example

   Resolve only to the same family as the client (do not permit cross family transactions). ::

      client;none

This value is a global default that can be overridden by :ts:cv:`proxy.config.http.server_ports`.

.. note::

   This style is used as a convenience for the administrator. During a resolution the *resolution order* will be
   one family, then possibly the other. This is determined by changing ``client`` to ``ipv4`` or ``ipv6`` based on the
   client IP address and then removing duplicates.

.. important::

   This option has no effect on outbound transparent connections The local IP address used in the connection to the
   origin server is determined by the client, which forces the IP address family of the address used for the origin
   server. In effect, outbound transparent connections always use a resolution style of "``client``".

Logging Configuration
=====================

.. ts:cv:: CONFIG proxy.config.log.logging_enabled INT 3
   :reloadable:

   Enables and disables event logging:

   -  ``0`` = logging disabled
   -  ``1`` = log errors only
   -  ``2`` = log transactions only
   -  ``3`` = full logging (errors + transactions)

   Refer to :ref:`working-with-log-files`.

.. ts:cv:: CONFIG proxy.config.log.max_secs_per_buffer INT 5
   :reloadable:

   The maximum amount of time before data in the buffer is flushed to disk.

.. ts:cv:: CONFIG proxy.config.log.max_space_mb_for_logs INT 25000
   :metric: megabytes
   :reloadable:

   The amount of space allocated to the logging directory (in MB).

.. note::

   All files in the logging directory contribute to the space used, even if they are not log files. In collation client
   mode, if there is no local disk logging, or :ts:cv:`proxy.config.log.max_space_mb_for_orphan_logs` is set to a higher
   value than :ts:cv:`proxy.config.log.max_space_mb_for_logs`, TS will take
   :ts:cv:`proxy.config.log.max_space_mb_for_orphan_logs` for maximum allowed log space.

.. ts:cv:: CONFIG proxy.config.log.max_space_mb_for_orphan_logs INT 25
   :metric: megabytes
   :reloadable:

   The amount of space allocated to the logging directory (in MB) if this node is acting as a collation client.

.. note::

   When max_space_mb_for_orphan_logs is take as the maximum allowed log space in the logging system, the same rule apply
   to proxy.config.log.max_space_mb_for_logs also apply to proxy.config.log.max_space_mb_for_orphan_logs, ie: All files
   in the logging directory contribute to the space used, even if they are not log files. you may need to consider this
   when you enable full remote logging, and bump to the same size as proxy.config.log.max_space_mb_for_logs.

.. ts:cv:: CONFIG proxy.config.log.max_space_mb_headroom INT 1000
   :metric: megabytes
   :reloadable:

   The tolerance for the log space limit (in megabytes). If the variable :ts:cv:`proxy.config.log.auto_delete_rolled_files` is set to ``1``
   (enabled), then autodeletion of log files is triggered when the amount of free space available in the logging directory is less than
   the value specified here.

.. ts:cv:: CONFIG proxy.config.log.hostname STRING localhost
   :reloadable:

   The hostname of the machine running Traffic Server.

.. ts:cv:: CONFIG proxy.config.log.logfile_dir STRING var/log/trafficserver
   :reloadable:

   The path to the logging directory. This can be an absolute path
   or a path relative to the ``PREFIX`` directory in which Traffic
   Server is installed.

   .. note:: The directory you specify must already exist.

.. ts:cv:: CONFIG proxy.config.log.logfile_perm STRING rw-r--r--
   :reloadable:

   The log file permissions. The standard UNIX file permissions are used (owner, group, other). Permissible values are:

   ``-`` no permission ``r`` read permission ``w`` write permission ``x`` execute permission

   Permissions are subject to the umask settings for the Traffic Server process. This means that a umask setting of\ ``002`` will not allow
   write permission for others, even if specified in the configuration file. Permissions for existing log files are not changed when the
   configuration is changed.

.. ts:cv:: CONFIG proxy.config.log.custom_logs_enabled INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) custom logging.

.. note::
    The following defaults (with the format``CONFIG proxy.config.log.[format].[default]`` have been removed from ``records.config``.
    They can be accessed by editing :file:`logs_xml.config`.

    - ``log_enabled`` INT 1
        Enables (``1``) or disables (``0``) the file format.

    - ``log_is_ascii`` INT 0
        The log file type:
           - ``1`` = ASCII
           - ``0`` = binary

    - ``log_name`` STRING [format]
        The filename (ex. :ref:`squid log <log-formats-squid-format>`).

    - ``log_header`` STRING NULL
        The file header text (ex. :ref:`squid log <log-formats-squid-format>`).

    The format can be either ``squid`` (Squid Format), ``common`` (Netscape Common),  ``extended`` (Netscape Extended),
    or  ``extended2`` (Netscape Extended-2).

.. ts:cv:: LOCAL proxy.local.log.collation_mode INT 0
   :reloadable:

   Set the log collation mode.

===== ======
Value Effect
===== ======
0     collation is disabled
1     this host is a log collation server
2     this host is a collation client and sends entries using standard formats to the collation server
3     this host is a collation client and sends entries using the traditional custom formats to the collation server
4     this host is a collation client and sends entries that use both the standard and traditional custom formats to the collation server
===== ======

For information on sending XML-based custom formats to the collation
server, refer to :file:`logs_xml.config`.

.. note:: Although Traffic Server supports traditional custom logging, you should use the more versatile XML-based custom formats.

.. ts:cv:: CONFIG proxy.config.log.collation_host STRING NULL

   The hostname of the log collation server.

.. ts:cv:: CONFIG proxy.config.log.collation_port INT 8085
   :reloadable:

   The port used for communication between the collation server and client.

.. ts:cv:: CONFIG proxy.config.log.collation_secret STRING foobar
   :reloadable:

   The password used to validate logging data and prevent the exchange of unauthorized information when a collation server is being used.

.. ts:cv:: CONFIG proxy.config.log.collation_host_tagged INT 0
   :reloadable:

   When enabled (``1``), configures Traffic Server to include the hostname of the collation client that generated the log entry in each entry.

.. ts:cv:: CONFIG proxy.config.log.collation_retry_sec INT 5
   :reloadable:

   The number of seconds between collation server connection retries.

.. ts:cv:: CONFIG proxy.config.log.rolling_enabled INT 1
   :reloadable:

   Specifies how log files are rolled. You can specify the following values:

   -  ``0`` = disables log file rolling
   -  ``1`` = enables log file rolling at specific intervals during the day (specified with the
       `proxy.config.log.rolling_interval_sec`_ and `proxy.config.log.rolling_offset_hr`_ variables)
   -  ``2`` = enables log file rolling when log files reach a specific size (specified with the `proxy.config.log.rolling_size_mb`_ variable)
   -  ``3`` = enables log file rolling at specific intervals during the day or when log files reach a specific size (whichever occurs first)
   -  ``4`` = enables log file rolling at specific intervals during the day when log files reach a specific size (i.e., at a specified
       time if the file is of the specified size)

.. ts:cv:: CONFIG proxy.config.log.rolling_interval_sec INT 86400
   :reloadable:

   The log file rolling interval, in seconds. The minimum value is ``60`` (1 minute). The maximum, and default, value is 86400 seconds (one day).

   .. note:: If you start Traffic Server within a few minutes of the next rolling time, then rolling might not occur until the next rolling time.

.. ts:cv:: CONFIG proxy.config.log.rolling_offset_hr INT 0
   :reloadable:

   The file rolling offset hour. The hour of the day that starts the log rolling period.

.. ts:cv:: CONFIG proxy.config.log.rolling_size_mb INT 10
   :reloadable:

   The size that log files must reach before rolling takes place.

.. ts:cv:: CONFIG proxy.config.log.auto_delete_rolled_files INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) automatic deletion of rolled files.

.. ts:cv:: CONFIG proxy.config.log.sampling_frequency INT 1
   :reloadable:

   Configures Traffic Server to log only a sample of transactions rather than every transaction. You can specify the following values:

   -  ``1`` = log every transaction
   -  ``2`` = log every second transaction
   -  ``3`` = log every third transaction and so on...

.. ts:cv:: CONFIG proxy.config.log.periodic_tasks_interval INT 5
   :reloadable:
   :metric: seconds

   How often Traffic Server executes log related periodic tasks, in seconds

.. ts:cv:: CONFIG proxy.config.http.slow.log.threshold INT 0
   :reloadable:
   :metric: milliseconds

   If set to a non-zero value :arg:`N` then any connection that takes longer than :arg:`N` milliseconds from accept to
   completion will cause its timing stats to be written to the :ts:cv:`debugging log file
   <proxy.config.output.logfile>`. This is identifying data about the transaction and all of the :c:type:`transaction milestones <TSMilestonesType>`.

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

   * 'O' = Log to standard output
   * 'E' = Log to standard error
   * 'S' = Log to syslog
   * 'L' = Log to diags.log

.. topic:: Example

   To log debug diagnostics to both syslog and `diags.log`::

        CONFIG proxy.config.diags.output.debug STRING SL

.. ts:cv:: CONFIG proxy.config.diags.show_location INT 0

   Annotates diagnostic messages with the source code location.

.. ts:cv:: CONFIG proxy.config.diags.debug.enabled INT 0

   When set to 1, enables logging for diagnostic messages whose log level is `diag` or `debug`.

   When set to 2, interprets the :ts:cv:`proxy.config.diags.debug.client_ip` setting determine whether diagnostic messages are logged.

.. ts:cv:: CONFIG proxy.config.diags.debug.client_ip STRING NULL

   if :ts:cv:`proxy.config.diags.debug.enabled` is set to 2, this value is tested against the source IP of the incoming connection.  If there is a match, all the diagnostic messages for that connection and the related outgoing connection will be logged.

.. ts:cv:: CONFIG proxy.config.diags.debug.tags STRING http.*|dns.*

   Each Traffic Server `diag` and `debug` level message is annotated
   with a subsytem tag. This configuration contains a regular
   expression that filters the messages based on the tag. Some
   commonly used debug tags are:

============  =====================================================
Tag           Subsytem usage
============  =====================================================
dns           DNS query resolution
http_hdrs     Logs the headers for HTTP requests and responses
privileges    Privilege elevation
ssl           TLS termination and certificate processing
============  =====================================================

  Traffic Server plugins will typically log debug messages using
  the :c:func:`TSDebug` API, passing the plugin name as the debug
  tag.

.. ts:cv:: CONFIG proxy.config.diags.logfile.rolling_enabled INT 0
   :reloadable:

   Specifies how the diagnostics log is rolled. You can specify the following values:

   -  ``0`` = disables diagnostics log rolling
   -  ``1`` = enables diagnostics log rolling at specific intervals (specified with the
       :ts:cv:`proxy.config.diags.logfile.rolling_interval_sec` variable). The "clock" starts ticking on Traffic Server boot
   -  ``2`` = enables diagnostics log rolling when the diagnostics log reaches a specific size (specified with the :ts:cv:`proxy.config.diags.logfile.rolling_size_mb` variable)

.. ts:cv:: CONFIG proxy.config.diags.logfile.rolling_interval_sec INT 3600
   :reloadable:
   :metric: seconds

   Specifies how often the diagnostics log is rolled, in seconds. The timer starts on Traffic Server bootup.

.. ts:cv:: CONFIG proxy.config.diags.logfile.rolling_size_mb INT 100
   :reloadable:
   :metric: megabytes

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

   Set this variable to ``1`` if you want Traffic Server to serve
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

.. ts:cv:: CONFIG proxy.config.ssl.SSLv2 INT 0

   Enables (``1``) or disables (``0``) SSLv2. Please don't enable it.

.. ts:cv:: CONFIG proxy.config.ssl.SSLv3 INT 0

   Enables (``1``) or disables (``0``) SSLv3.

.. ts:cv:: CONFIG proxy.config.ssl.TLSv1 INT 1

   Enables (``1``) or disables (``0``) TLSv1.

.. ts:cv:: CONFIG proxy.config.ssl.TLSv1_1 INT 1

   Enables (``1``) or disables (``0``) TLS v1.1.  If not specified, enabled by default.  [Requires OpenSSL v1.0.1 and higher]

.. ts:cv:: CONFIG proxy.config.ssl.TLSv1_2 INT 1

   Enables (``1``) or disables (``0``) TLS v1.2.  If not specified, DISABLED by default.  [Requires OpenSSL v1.0.1 and higher]

.. ts:cv:: CONFIG proxy.config.ssl.client.certification_level INT 0

   Sets the client certification level:

   -  ``0`` = no client certificates are required. Traffic Server does
       not verify client certificates during the SSL handshake. Access
       to Traffic Server depends on Traffic Server configuration options
       (such as access control lists).

   -  ``1`` = client certificates are optional. If a client has a
       certificate, then the certificate is validated. If the client
       does not have a certificate, then the client is still allowed
       access to Traffic Server unless access is denied through other
       Traffic Server configuration options.

   -  ``2`` = client certificates are required. The client must be
       authenticated during the SSL handshake. Clients without a
       certificate are not allowed to access Traffic Server.

.. ts:cv:: CONFIG proxy.config.ssl.number.threads INT -1

   Sets the number of SSL threads to use, this defaults to 0 (autoconfigure).

   -  ``0`` = autoconfigure, this will allow Traffic Server to determine
      the appropriate number of threads

   -  ``-1`` = disable, this makes ET_NET threads behave like ET_SSL threads
      Note: this does not disable SSL, it simply allows another thread pool
      to assist in SSL tasks without dedicated SSL threads.

   -  ``>0`` = Use a non-zero number of SSL threads

.. ts:cv:: CONFIG proxy.config.ssl.server.multicert.filename STRING ssl_multicert.config

   The location of the :file:`ssl_multicert.config` file, relative
   to the Traffic Server configuration directory. In the following
   example, if the Traffic Server configuration directory is
   `/etc/trafficserver`, the Traffic Server SSL configuration file
   and the corresponding certificates are located in
   `/etc/trafficserver/ssl`::

      CONFIG proxy.config.ssl.server.multicert.filename STRING ssl/ssl_multicert.config
      CONFIG proxy.config.ssl.server.cert.path STRING etc/trafficserver/ssl
      CONFIG proxy.config.ssl.server.private_key.path STRING etc/trafficserver/ssl

.. ts:cv:: CONFIG proxy.config.ssl.server.cert.path STRING /config

   The location of the SSL certificates and chains used for accepting
   and validation new SSL sessions. If this is a relative path,
   it is appended to the Traffic Server installation PREFIX. All
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

   The location of the :file:`ssl_ticket.key` file, relative to the
   :ts:cv:`proxy.config.ssl.server.cert.path` directory.

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

	Enables the SSL Session Cache:
	- ``0`` = Disables the session cache entirely

	- ``1`` = Enables the session cache using OpenSSLs implementation.

	- ``2`` = (default) Enables the session cache using Traffic Server's implementation.
			This implentation should perform much better than the OpenSSL
			implementation.

.. ts:cv:: CONFIG proxy.config.ssl.session_cache.timeout INT 0

  This configuration specifies the lifetime of SSL session cache
  entries in seconds. If it is ``0``, then the SSL library will use
  a default value, typically 300 seconds. Note: This option has no affect
  when using the Traffic Server session cache (option ``2`` in
  ``proxy.config.ssl.session_cache``)

.. ts:cv:: CONFIG proxy.config.ssl.session_cache.auto_clear INT 1

  This will set the OpenSSL auto clear flag. Auto clear is enabled by
  default with ``1`` it can be disabled by changing this setting to ``0``.

.. ts:cv:: CONFIG proxy.config.ssl.session_cache.size INT 102400

  This configuration specifies the maximum number of entries
  the SSL session cache may contain.

.. ts:cv:: CONFIG proxy.config.ssl.session_cache.num_buckets INT 256

  This configuration specifies the number of buckets to use with the
  Traffic Server SSL session cache implementation. The TS implementation
  is a fixed size hash map where each bucket is protected by a mutex.

.. ts:cv:: CONFIG proxy.config.ssl.session_cache.skip_cache_on_bucket_contention INT 0

	This configuration specifies the behavior of the Traffic Server SSL session
	cache implementation during lock contention on each bucket:

	- ``0`` = (default) Don't skip session caching when bucket lock is contented.

	- ``1`` = Don't use the SSL session cache for this connection during lock contention.

.. ts:cv:: CONFIG proxy.config.ssl.hsts_max_age INT -1

  This configuration specifies the max-age value that will be used
  when adding the Strict-Transport-Security header.  The value is in seconds.
  A value of ``0`` will set the max-age value to ``0`` and should remove the
  HSTS entry from the client.  A value of ``-1`` will disable this feature and
  not set the header.  This option is only used for HTTPS requests and the
  header will not be set on HTTP requests.

.. ts:cv:: CONFIG proxy.config.ssl.hsts_include_subdomains INT 0

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

  This feature requires Traffic Server to be built with POSIX
  capabilities enabled.

.. ts:cv:: CONFIG proxy.config.ssl.handshake_timeout_in INT 0

  When enabled this limits the total duration for the server side SSL
  handshake.

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

  This specifies the server name for which wire_traces should be
  printed. This only works if traffic_server is built with
  TS_USE_TLS_SNI flag set to true.

Client-Related Configuration
----------------------------

.. ts:cv:: CONFIG proxy.config.ssl.client.verify.server INT 0

   Configures Traffic Server to verify the origin server certificate
   with the Certificate Authority (CA).

.. ts:cv:: CONFIG proxy.config.ssl.client.cert.filename STRING NULL

   The filename of SSL client certificate installed on Traffic Server.

.. ts:cv:: CONFIG proxy.config.ssl.client.cert.path STRING /config

   The location of the SSL client certificate installed on Traffic
   Server.

.. ts:cv:: CONFIG proxy.config.ssl.client.private_key.filename STRING NULL

   The filename of the Traffic Server private key. Change this variable
   only if the private key is not located in the Traffic Server SSL
   client certificate file.

.. ts:cv:: CONFIG proxy.config.ssl.client.private_key.path STRING NULL

   The location of the Traffic Server private key. Change this variable
   only if the private key is not located in the SSL client certificate
   file.

.. ts:cv:: CONFIG proxy.config.ssl.client.CA.cert.filename STRING NULL

   The filename of the certificate authority against which the origin
   server will be verified.

.. ts:cv:: CONFIG proxy.config.ssl.client.CA.cert.path STRING NULL

   Specifies the location of the certificate authority file against
   which the origin server will be verified.

OCSP Stapling Configuration
===========================

.. ts:cv:: CONFIG proxy.config.ssl.ocsp.enabled INT 0

   Enable OCSP stapling.

   -  ``0`` = disables OCSP Stapling
   -  ``1`` = allows Traffic Server to request SSL certificate revocation status from an OCSP responder.

.. ts:cv:: CONFIG proxy.config.ssl.ocsp.cache_timeout INT 3600

   Number of seconds before an OCSP response expires in the stapling cache.

.. ts:cv:: CONFIG proxy.config.ssl.ocsp.request_timeout INT 10

   Timeout (in seconds) for queries to OCSP responders.

.. ts:cv:: CONFIG proxy.config.ssl.ocsp.update_period INT 60

   Update period (in seconds) for stapling caches.

ICP Configuration
=================

.. ts:cv:: CONFIG proxy.config.icp.enabled INT 0

   Sets ICP mode for hierarchical caching:

   -  ``0`` = disables ICP
   -  ``1`` = allows Traffic Server to receive ICP queries only
   -  ``2`` = allows Traffic Server to send and receive ICP queries

   Refer to :ref:`admin-icp-peering`.

.. ts:cv:: CONFIG proxy.config.icp.icp_interface STRING NULL

   Specifies the network interface used for ICP traffic.

   .. note::

       The Traffic Server installation script detects your
       network interface and sets this variable appropriately. If your
       system has multiple network interfaces, check that this variable
       specifies the correct interface.

.. ts:cv:: CONFIG proxy.config.icp.icp_port INT 3130
   :reloadable:

   Specifies the UDP port that you want to use for ICP messages.

.. ts:cv:: CONFIG proxy.config.icp.query_timeout INT 2
   :reloadable:

   Specifies the timeout used for ICP queries.

HTTP/2 Configuration
====================


.. ts:cv:: CONFIG proxy.config.http2.enabled INT 0

   Enable the experimental HTTP/2 feature. This implements most of the
   specifications, with the one big exception being server PUSH.

   .. note:: This configuration will be eliminated for v6.0.0, where HTTP/2 is
	     enabled by default and controlled via the ports configuration.

.. ts:cv:: CONFIG proxy.config.http2.max_concurrent_streams_in INT 100
   :reloadable:

   The maximum number of concurrent streams per inbound connection.

   .. note:: Reloading this value affects only new HTTP/2 connections, not the
	     ones already established.

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
   the unsigned int maximum value in Traffic Server, implies unlimited size.

SPDY Configuration
==================

.. ts:cv:: CONFIG proxy.config.spdy.accept_no_activity_timeout INT 120
   :reloadable:

   How long a SPDY connection will be kept open after an accept without any streams created.

.. ts:cv:: CONFIG proxy.config.spdy.no_activity_timeout_in INT 115
   :reloadable:

   How long a stream is kept open without activity.

.. ts:cv:: CONFIG proxy.config.spdy.initial_window_size_in INT 1048576
   :reloadable:

   The initial window size for inbound connections.

.. ts:cv:: CONFIG proxy.config.spdy.max_concurrent_streams_in INT 100
   :reloadable:

   The maximum number of concurrent streams per inbound connection.

   .. note:: Reloading this value affects only new SPDY connections, not the
	     ones already established..

Plug-in Configuration
=====================

.. ts:cv:: CONFIG proxy.config.plugin.plugin_dir STRING config/plugins

   Specifies the location of Traffic Server plugins.

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

.. ts:cv::  CONFIG proxy.config.socks.server_connect_timeout INT 10

   The timeout value (in seconds) for SOCKS server connection attempts.

.. ts:cv::  CONFIG proxy.config.socks.per_server_connection_attempts INT 1

    The total number of connection attempts allowed per SOCKS server,
    if multiple servers are used.

.. ts:cv::  CONFIG proxy.config.socks.connection_attempts INT 4

   The total number of connection attempts allowed to a SOCKS server
   Traffic Server bypasses the server or fails the request

.. ts:cv::  CONFIG proxy.config.socks.server_retry_timeout INT 300

   The timeout value (in seconds) for SOCKS server connection retry attempts.

.. ts:cv::  CONFIG proxy.config.socks.default_servers STRING

   Default list of SOCKS servers and their ports.

.. ts:cv::  CONFIG proxy.config.socks.server_retry_time INT 300

   The amount of time allowed between connection retries to a SOCKS
   server that is unavailable.

.. ts:cv::  CONFIG proxy.config.socks.server_fail_threshold INT 2

   The number of times the connection to the SOCKS server can fail
   before Traffic Server considers the server unavailable.

.. ts:cv::  CONFIG proxy.config.socks.accept_enabled INT 0

   Enables (1) or disables (0) the SOCKS proxy option. As a SOCKS
   proxy, Traffic Server receives SOCKS traffic (usually on port
   1080) and forwards all requests directly to the SOCKS server.

.. ts:cv::  CONFIG proxy.config.socks.accept_port INT 1080

   Specifies the port on which Traffic Server accepts SOCKS traffic.

.. ts:cv::  CONFIG proxy.config.socks.http_port INT 80

   Specifies the port on which Traffic Server accepts HTTP proxy requests
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
  If it is set to -1, Traffic Server will automatically set this
  to a platform-specific maximum.

.. ts:cv:: CONFIG proxy.config.net.sock_send_buffer_size_in INT 0

   Sets the send buffer size for connections from the client to Traffic Server.

.. ts:cv:: CONFIG proxy.config.net.sock_recv_buffer_size_in INT 0

   Sets the receive buffer size for connections from the client to Traffic Server.

.. ts:cv:: CONFIG proxy.config.net.sock_option_flag_in INT 0x5

   Turns different options "on" for the socket handling client connections:::

        TCP_NODELAY  (1)
        SO_KEEPALIVE (2)
        SO_LINGER (4) - with a timeout of 0 seconds

   .. note::

       This is a bitmask and you need to decide what bits to set.  Therefore,
       you must set the value to ``3`` if you want to enable nodelay and
       keepalive options above.

.. ts:cv:: CONFIG proxy.config.net.sock_send_buffer_size_out INT 0
   :overridable:

   Sets the send buffer size for connections from Traffic Server to the origin server.

.. ts:cv:: CONFIG proxy.config.net.sock_recv_buffer_size_out INT 0
   :overridable:

   Sets the receive buffer size for connections from Traffic Server to
   the origin server.

.. ts:cv:: CONFIG proxy.config.net.sock_option_flag_out INT 0x1
   :overridable:

   Turns different options "on" for the origin server socket:::

        TCP_NODELAY  (1)
        SO_KEEPALIVE (2)
        SO_LINGER (4) - with a timeout of 0 seconds

   .. note::

       This is a bitmask and you need to decide what bits to set.  Therefore,
       you must set the value to ``3`` if you want to enable nodelay and
       keepalive options above.

        When SO_LINGER is enabled, the linger timeout time is set
        to 0. This is useful when Traffic Server and the origin server
        are co-located and large numbers of sockets are retained
        in the TIME_WAIT state.

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

.. ts:cv:: CONFIG proxy.config.task_threads INT 2

   Specifies the number of task threads to run. These threads are used for
   various tasks that should be off-loaded from the normal network threads.

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

.. ts:cv:: CONFIG proxy.config.http.enabled INT 1

   Turn on or off support for HTTP proxying. This is rarely used, the one
   exception being if you run Traffic Server with a protocol plugin, and would
   like for it to not support HTTP requests at all.

.. ts:cv:: CONFIG proxy.config.http.wait_for_cache INT 0

   Accepting inbound connections and starting the cache are independent operations in Traffic
   Server. This variable controls the relative timing of these operations and Traffic Server
   dependency on cache because if cache is required then inbound connection accepts should be
   deferred until the validity of the cache requirement is determined. Cache initialization failure
   will be logged in :file:`diags.log`.

===== ====================
Value Effect
===== ====================
0     Decouple inbound connections and cache initialization. Connections will be accepted as soon as possible and Traffic Server will run regardless of the results of cache initialization.

1     Do not accept inbound connections until cache initialization has finished. Traffic server will run
      regardless of the results of cache initialization.

2     Do not accept inbound connections until cache initialization has finished and been sufficiently
      successful that cache is enabled. This means at least one cache span is usable. If there are no
      spans in :file:`storage.config` or none of the spans can be successfully parsed and
      initialized then Traffic Server will shut down.

3     Do not accept inbound connections until cache initialization has finished and been completely
      successful. This requires at least one cache span in :file:`storage.config` and that every
      span specified is valid and successfully initialized. Any error will cause Traffic Server to shut
      down.
===== ====================

.. _Traffic Shaping:
                 https://cwiki.apache.org/confluence/display/TS/Traffic+Shaping
