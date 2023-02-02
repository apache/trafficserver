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

.. configfile:: records.yaml

records.yaml
************

.. important::

   From ATS 10 we have moved from the old ``records.config`` format style in flavour of the new
   YAML style. Please check :ref:`rec-config-to-yaml` for information about migrating from the
   old style to the new YAML base config.


The :file:`records.yaml` file (by default, located in
``/usr/local/etc/trafficserver/``) is a YAML base configuration file used by
the |TS| software. Many of the fields in :file:`records.yaml` are set
automatically when you set configuration options with :option:`traffic_ctl config set`. After you
modify :file:`records.yaml`, run the command :option:`traffic_ctl config reload`
to apply the changes.

.. note::

   The configuration directory, containing the ``SYSCONFDIR`` value specified at build time
   relative to the installation prefix, contains |TS| configuration files.
   The ``$TS_ROOT`` environment variable can be used alter the installation prefix at run time.
   The directory must allow read/write access for configuration reloads.


YAML structure
==============

All fields are located inside the ``ts`` root node.


.. code-block:: yaml
   :linenos:

   ts:
     diags:
      debug:
         enabled: 0
         tags: http|dns
    # ...
    # rest of the fields.
    # ...


.. important::

   Internally, ATS uses record names as :ref:`configuration-variables`.


Data Type
---------

There is no need to manually set the record type, it can be done if desired.
The types accepted are:

========== ====================================================================
Type       Description
========== ====================================================================
``float``  Floating point, expressed as a decimal number without units or
           exponents.
``int``    Integers, expressed with or without unit prefixes (as described
           below).
``str``    String of characters up to the first newline. No quoting necessary.
========== ====================================================================

Non core records
~~~~~~~~~~~~~~~~

Records that aren't part of the core ATS needs to set the field type, this for now
is the only way to know the field type.

We expect non core records to set the type (!!int, !!float, etc).

.. code-block:: yaml

   ts:
      plugin_x:
         my_field_1: !!int '1'
         my_field_2: !!float '1.2'
         my_field_3: 'my string'

Values
------

The *field_value* must conform to the variable's type. For ``str``, this
is simply any character data until the first newline.

For integer (``int``) fields, values are expressed as any normal integer,
e.g. ``32768``. They can also be expressed using more human readable values
using standard unit prefixes, e.g. ``32K``. The following prefixes are
supported for all ``int`` type configurations:

====== ============ ===========================================================
Prefix Description  Equivalent in Bytes
====== ============ ===========================================================
``K``  Kilobytes    1,024 bytes
``M``  Megabytes    1,048,576 bytes (1024\ :sup:`2`)
``G``  Gigabytes    1,073,741,824 bytes (1024\ :sup:`3`)
``T``  Terabytes    1,099,511,627,776 bytes (1024\ :sup:`4`)
====== ============ ===========================================================

Floating point variables (``float``) must be expressed as a regular decimal
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
the current transaction only. Remap config files still uses the legacy ``records.config``
style.

Examples
========

In the following example, the field `proxy_name` is a ``str`` datatype with the
value ``my_server``. This means that the name of the |TS| proxy is ``my_server``.

   .. code-block:: yaml
      :linenos:

      ts:
         proxy_name: my_server


If the server name should be ``that_server`` the line would be:

   .. code-block:: yaml
      :linenos:

      ts:
         proxy_name: that_server


In the following example, the field is a yes/no flag. A value of ``0`` (zero)
disables the option; a value of ``1`` enables the option.

   .. code-block:: yaml
      :linenos:

      ts:
         arm:
            enabled: 0

In the following example, the field sets the time to wait for a
DNS response to 10 seconds.

   .. code-block:: yaml
      :linenos:

      ts:
         hostdb:
            lookup_timeout: 10

In the following example the field sets the field with a ``float`` value.

   .. code-block:: yaml
      :linenos:

      ts:
         exec_thread:
            autoconfig:
               scale: 1.0


The last examples configures a 64GB RAM cache, using a human readable
prefix.

   .. code-block:: yaml
      :linenos:

      ts:
         cache:
            ram_cache:
               size: 64G

Environment Overrides
=====================

Every :file:`records.yaml` configuration variable can be overridden
by a corresponding environment variable. This can be useful in
situations where you need a static :file:`records.yaml` but still
want to tweak one or two settings. The override variable is formed
by converting the :file:`records.yaml` variable name to upper
case, and replacing any dot separators with an underscore.

Overriding a variable from the environment is permanent and will
not be affected by future configuration changes made in
:file:`records.yaml` or applied with :program:`traffic_ctl`.

For example, we could override the `proxy.config.product_company`_ variable
like this::

   $ PROXY_CONFIG_PRODUCT_COMPANY=example traffic_server &
   $ traffic_ctl config get proxy.config.product_company

.. _configuration-variables:

Configuration Variables
=======================

The following list describes the configuration variables available in
the :file:`records.yaml` file.

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

 .. important::

      This is now deprecated. traffic_manager is no longer supported.

.. ts:cv:: CONFIG proxy.config.proxy_binary_opts STRING -M

   .. important::

      This is now deprecated

.. ts:cv:: CONFIG proxy.config.manager_binary STRING traffic_manager

   .. important::

      This is now deprecated. traffic_manager is no longer supported.

.. ts:cv:: CONFIG proxy.config.memory.max_usage INT 0
   :units: bytes

   Throttle incoming connections if resident memory usage exceeds this value.
   Setting the option to 0 disables the feature.

.. ts:cv:: CONFIG proxy.config.env_prep STRING

   .. important::

      This is now deprecated. traffic_manager is no longer supported.

.. ts:cv:: CONFIG proxy.config.syslog_facility STRING LOG_DAEMON

   The facility used to record system log files. Refer to
   :ref:`admin-logging-understanding` for more in-depth discussion
   of the contents and interpretations of log files.


.. ts:cv:: CONFIG proxy.config.output.logfile  STRING traffic.out

   This is used for log rolling configuration so |TS| knows the path of the
   output file that should be rolled. This configuration takes the name of the
   file receiving :program:`traffic_server`
   process output that is set via the ``--bind_stdout`` and ``--bind_stderr``
   :ref:`command-line options <traffic_server>`.
   :ts:cv:`proxy.config.output.logfile` is used only to identify the name of
   the output file for log rolling purposes and does not override the values
   set via ``--bind_stdout`` and ``--bind_stderr``.

   If a filename is passed to this option, then it will be interpreted relative
   to :ts:cv:`proxy.config.log.logfile_dir`. If a different location is desired,
   then pass an absolute path to this configuration.

.. ts:cv:: CONFIG proxy.config.output.logfile_perm STRING rw-r--r--

   The log file permissions for the file receiving |TS| output, the path of
   which is configured via the ``--bind_stdout`` and ``--bind_stderr``
   :ref:`command-line options <traffic_server>`.  The standard UNIX file
   permissions are used (owner, group, other). Permissible values are:

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

   Specifies how often the output log is rolled, in seconds. The timer starts on |TS| startup.

.. ts:cv:: CONFIG proxy.config.output.logfile.rolling_size_mb INT 100
   :reloadable:
   :units: megabytes

   Specifies at what size to roll the output log at.

.. ts:cv:: CONFIG proxy.config.output.logfile.rolling_min_count INT 0
   :reloadable:

   Specifies the minimum count of rolled output logs to keep. This value will be used to decide the
   order of auto-deletion (if enabled). A default value of 0 means auto-deletion will try to keep
   output logs as much as possible. See :doc:`../logging/rotation.en` for guidance.


Thread Variables
----------------

.. ts:cv:: CONFIG proxy.config.exec_thread.autoconfig INT 1

   When enabled (the default, ``1``), |TS| scales threads according to the
   available CPU cores. See the config option below.

.. ts:cv:: CONFIG proxy.config.exec_thread.autoconfig.scale FLOAT 1.0

   Factor by which |TS| scales the number of threads. The multiplier is usually
   the number of available CPU cores. By default this is scaling factor is
   ``1.0``.

.. ts:cv:: CONFIG proxy.config.exec_thread.limit INT 2

   The number of threads |TS| will create if `proxy.config.exec_thread.autoconfig`
   is set to ``0``, otherwise this option is ignored.

.. ts:cv:: CONFIG proxy.config.exec_thread.listen INT 0

   If enabled (``1``) all the exec_threads listen for incoming connections. `proxy.config.accept_threads`
   should be disabled to enable this variable.

.. ts:cv:: CONFIG proxy.config.accept_threads INT 1

   The number of accept threads. If disabled (``0``), then accepts will be done
   in each of the worker threads.

   ==================== ====================== =====================
     accept_threads      exec_thread.listen         Effect
   ==================== ====================== =====================
   ``0``                 ``0``                  All worker threads accept new connections and share listen fd.
   ``1``                 ``0``                  New connections are accepted on a dedicated accept thread and distributed to worker threads in round robin fashion.
   ``0``                 ``1``                  All worker threads listen on the same port using SO_REUSEPORT. Each thread has its own listen fd and new connections are accepted on all the threads.
   ==================== ====================== =====================

   By default, `proxy.config.accept_threads` is set to 1 and `proxy.config.exec_thread.listen` is set to 0.
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

   Set the maximum number of file handles for the traffic_server process as a percentage of the fs.file-max proc value in Linux. The default is 90%.

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

   .. important::

      Deprecated. traffic_manager is no longer supported.

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
   Server, in which ATS can initiate graceful shutdowns. In order
   to effect graceful shutdown, the value specified should be greater
   than 0. Value of 0 will not effect an abrupt shutdown. Abrupt
   shutdowns can be achieved with out specifying --drain;
   (traffic_ctl server stop /restart). Stopping |TS| here means sending
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
   connections, i.e. from the default, only ~27,000 client connections can be
   handled. This should be tuned according to your memory size, and expected
   work load.  If this is set to 0, the throttling logic is disabled.

.. ts:cv:: CONFIG proxy.config.net.max_connections_in INT 30000

   The total number of client requests that |TS| can handle simultaneously.
   This should be tuned according to your memory size, and expected work load
   (network, cpu etc). This limit includes both idle (keep alive) connections
   and active requests that |TS| can handle at any given instant. The delta
   between `proxy.config.net.max_connections_in` and `proxy.config.net.max_requests_in`
   is the amount of maximum idle (keepalive) connections |TS| will maintain.

.. ts:cv:: CONFIG proxy.config.net.max_requests_in INT 0

   The total number of concurrent requests or active client connections
   that the |TS| can handle simultaneously. This should be tuned according
   to your memory size, and expected work load (network, cpu etc). When
   set to 0, active request tracking is disabled and max requests has no
   separate limit and the total connections follow `proxy.config.net.connections_throttle`

.. ts:cv:: CONFIG proxy.config.net.default_inactivity_timeout INT 86400
   :reloadable:
   :overridable:

   The connection inactivity timeout (in seconds) to apply when
   |TS| detects that no inactivity timeout has been applied
   by the HTTP state machine. When this timeout is applied, the
   `proxy.process.net.default_inactivity_timeout_applied` metric
   is incremented.

   Note that this configuration is overridable. While most overridable
   configurations conceptually apply to specific transactions,
   ``default_inactivity_timeout`` is a connection level concept. This is not
   necessarily a problem, but it does mean that care must be taken when
   applying the override to consider that all transactions in the connection
   which has this timeout overriden will be impacted by the override. For
   instance, if the default inactivity timeout is being overridden via a
   :ref:`admin-plugins-conf-remap` rule in :file:`remap.config`, then all
   transactions for that connection will be impacted by the override, not just
   the ones matching that ``remap.config`` rule.

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

Management
==========

.. ts:cv:: CONFIG proxy.node.config.manager_log_filename STRING manager.log

   .. important::

      This is now deprecated. traffic_manager is no longer supported.

.. ts:cv:: CONFIG proxy.config.admin.user_id STRING nobody

   Designates the non-privileged account to run the :program:`traffic_server`
   process as, which also has the effect of setting ownership of configuration
   and log files.

   If the user_id is prefixed with pound character (``#``),
   the remainder of the string is considered to be a
   `numeric user identifier <http://en.wikipedia.org/wiki/User_identifier>`_.
   If the value is set to ``#-1``, |TS| will not change the user during startup.

   .. important::

      Attempting to set this option to ``root`` or ``#0`` is now forbidden, as
      a measure to increase security. Doing so will cause a fatal failure upon
      startup in :program:`traffic_server`. However, there are two ways to
      bypass this restriction:

      * Specify ``-DBIG_SECURITY_HOLE`` in ``CXXFLAGS`` during compilation.

      * Set the ``user_id=#-1`` and start trafficserver as root.

.. ts:cv:: CONFIG proxy.config.admin.api.restricted INT 0

   This is now deprecated, please refer to :ref:`admin-jsonrpc-configuration` to find
   out about the new admin API mechanism.

Alarm Configuration
===================

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
   pp                          Enable Proxy Protocol.
   ssl                         SSL terminated.
   quic                        QUIC terminated.
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

   Not compatible with: ``tr-in``, ``ssl`` and ``quic``.

compress
   Compress the connection. Retained only by inertia, should be considered "not implemented".

ipv4
   Use IPv4. This is the default and is included primarily for completeness. This forced if the ``ip-in`` option is used with an IPv4 address.

ipv6
   Use IPv6. This is forced if the ``ip-in`` option is used with an IPv6 address.

ssl
   Require SSL termination for inbound connections. SSL :ref:`must be configured <admin-ssl-termination>` for this option to provide a functional server port.

   Not compatible with: ``blind`` and ``quic``.

quic
   Require QUIC termination for inbound connections. SSL :ref:`must be configured <admin-ssl-termination>` for this option to provide a functional server port.
   **THIS IS EXPERIMENTAL SUPPORT AND NOT READY FOR PRODUCTION USE.**

   Not compatible with: ``blind`` and ``ssl``.

proto
   Specify the :ref:`session level protocols <session-protocol>` supported. These should be
   separated by semi-colons. For TLS proxy ports the default value is
   all available protocols. For non-TLS proxy ports the default is HTTP
   only. HTTP/3 is only available on QUIC ports.

pp
   Enables Proxy Protocol on the port.  If Proxy Protocol is enabled on the
   port, all incoming requests must be prefaced with the PROXY header.  See
   :ref:`Proxy Protocol <proxy-protocol>` for more details on how to configure
   this option properly.

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

.. topic:: Example

   Listen on port 4433 for QUIC connections.::

      4433:quic

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

   The ``Via`` transaction codes can be decoded with the `Via Decoder Ring <https://trafficserver.apache.org/tools/via>`_.

.. ts:cv:: CONFIG proxy.config.http.request_via_str STRING ApacheTrafficServer/${PACKAGE_VERSION}
   :reloadable:
   :overridable:

   Set the server and version string in the ``Via`` request header to the origin server which is inserted when the value of :ts:cv:`proxy.config.http.insert_request_via_str` is not ``0``.  Note that the actual default value is defined with ``"ApacheTrafficServer/" PACKAGE_VERSION`` in a C++ source code, and you must write such as ``ApacheTrafficServer/6.0.0`` if you really set a value with the version in :file:`records.yaml` file. If you want to hide the version, you can set this value to ``ApacheTrafficServer``.

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

   The ``Via`` transaction code can be decoded with the `Via Decoder Ring <https://trafficserver.apache.org/tools/via>`_.

.. ts:cv:: CONFIG proxy.config.http.response_via_str STRING ApacheTrafficServer/${PACKAGE_VERSION}
   :reloadable:
   :overridable:

   Set the server and version string in the ``Via`` response header to the client which is inserted when the value of :ts:cv:`proxy.config.http.insert_response_via_str` is not ``0``.  Note that the actual default value is defined with ``"ApacheTrafficServer/" PACKAGE_VERSION`` in a C++ source code, and you must write such as ``ApacheTrafficServer/6.0.0`` if you really set a value with the version in :file:`records.yaml` file. If you want to hide the version, you can set this value to ``ApacheTrafficServer``.

.. ts:cv:: CONFIG proxy.config.http.send_100_continue_response INT 0
   :reloadable:

   You can specify one of the following:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` |TS| will buffer the request until the post body has been received and
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
   :file:`records.yaml`. If you want to hide the version, you can set this
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

.. ts:cv:: CONFIG proxy.config.http.auth_server_session_private INT 1
   :overridable:

   If enabled (``1``) anytime a request contains a ``Authorization``,
   ``Proxy-Authorization``, or ``Www-Authenticate`` header the connection will
   be closed and not reused. This marks the connection as private. When disabled
   (``0``) the connection will be available for reuse.

.. ts:cv:: CONFIG proxy.config.http.server_session_sharing.match STRING both
   :overridable:

   Enable and set the ability to re-use server connections across client
   connections. Multiple values can be specified when separated by commas with no white spaces. Valid values are:

   ============= ===================================================================
   Value         Description
   ============= ===================================================================
   ``none``      Do not match and do not re-use server sessions.
   ``ip``        Re-use server sessions, checking only that the IP address and port
                 of the origin server matches.
   ``host``      Re-use server sessions, checking that the fully qualified
                 domain name matches. In addition, if the session uses TLS, it also
                 checks that the current transaction's host header value matches the session's SNI.
   ``both``      Equivalent to ``host,ip``.
   ``hostonly``  Check that the fully qualified domain name matches.
   ``sni``       Check that the SNI of the session matches the SNI that would be used to
                 create a new session.  Only applicable for TLS sessions.
   ``cert``      Check that the certificate file name used for the server session matches the
                 certificate file name that would be used for the new server session.  Only
                 applicable for TLS sessions.
   ============= ===================================================================

   The setting must contain at least one of ``ip``, ``host``, ``hostonly`` or ``both``
   for session reuse to operate.  The other values may be used for greater control
   with TLS session reuse.

.. note::

   Server sessions to different upstream ports never match even if the FQDN and IP
   address match.

.. note::

   :ts:cv:`Upstream session tracking <proxy.config.http.per_server.connection.max>` uses a similar
   set of options for matching sessions, but is :ts:cv:`set independently
   <proxy.config.http.per_server.connection.match>` from session sharing.

.. ts:cv:: CONFIG proxy.config.http.server_session_sharing.pool STRING thread

   Control the scope of server session re-use if it is enabled by
   :ts:cv:`proxy.config.http.server_session_sharing.match`. Valid values are:

   ========== =================================================================
   Value      Description
   ========== =================================================================
   ``global`` Re-use sessions from a global pool of all server sessions.
   ``thread`` Re-use sessions from a per-thread pool.
   ``hybrid`` Try to work as a global pool, but release server sessions to the
              per-thread pool if there is lock contention on the global pool.
   ========== =================================================================


   Setting :ts:cv:`proxy.config.http.server_session_sharing.pool` to global can reduce
   the number of connections to origin for some traffic loads.  However, if many
   execute threads are active, the thread contention on the global pool can reduce the
   lifetime of connections to origin and reduce effective origin connection reuse.

   For a hybrid pool, the operation starts as the global pool, but sessons are returned
   to the local thread pool if the global pool lock is not acquired rather than just
   closing the origin connection as is the case in standard global mode.

.. ts:cv:: CONFIG proxy.config.http.attach_server_session_to_client INT 0
   :overridable:

   Control the re-use of an server session by a user agent (client) session. Currently only applies to user
   agents using HTTP/1.0 or HTTP/1.1. For other HTTP versions, the origin connection is always returned to the
   session sharing pool or closed.

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

.. ts:cv:: CONFIG proxy.config.http.max_proxy_cycles INT 0
   :overridable:

   Control the proxy cycle detection function in the following manner --

   If this setting is ``0``, then next hop is self IP address and port detection is active.

   In addition, the proxy cycle detection using the Via string will declare a cycle if the current cache
   appears one or more times in the Via string, i.e, > 0.

   If this setting is ``1`` or more (N), then next hop is self IP address and port detection is inactive.

   In addition, the proxy cycle detection using the Via string will declare a cycle if the current cache
   appears more than N times in the Via string, i.e., > N.

   Examples:

   If the setting is ``0``, then the second time a request enters a cache it will have its own machine
   identifier in the Via string once, so a cycle will be detected. So no cycles are allowed.

   If the setting is ``1``, then the third time a request enters a cache it will have its own machine
   identifier in the Via string twice, so a cycle will be detected. So one cycle is allowed.
   The first cycle with two visits to the cache and one instance in the Via string is allowed.
   The second cycle with three visits to the cache and two instances in the Via string is not allowed.

   This setting allows an edge cache peering arrangement where an edge cache may forward a request to a
   peer edge cache (possibly itself) a limited of times (usually once). Infinite loops are still detected
   when the cycle allowance is exceeded.

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
         performed. The result is cached (if allowed otherwise).
         This option is vulnerable to cache poisoning if an incorrect ``Host`` header is
         specified, so this option should be used with extreme caution if HTTP caching is
         enabled.  See bug TS-2954 for details.
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
   :reloadable:
   :overridable:

   Configures the default buffer size, in bytes, to allocate for incoming
   request bodies which lack a ``Content-length`` header.

.. ts:cv:: CONFIG proxy.config.http.default_buffer_water_mark INT 32768
   :reloadable:
   :overridable:

   Number of bytes |TS| is allowed to read ahead of the client from the origin. Note that when
   :ref:`Read While Write <admin-configuration-reducing-origin-requests>` settings are in place,
   this setting will apply to the first client to request the object, regardless if subsequent,
   simultaneous clients of that object can read faster. The buffered bytes will consume memory
   while waiting for the client to consume them.

   While this setting is reloadable, dramatic changes can cause bigger memory usage than expected
   and is thus not recommended.

.. ts:cv:: CONFIG proxy.config.http.request_buffer_enabled INT 0
   :overridable:

   This enables buffering the content for incoming ``POST`` requests. If enabled no outbound
   connection is made until the entire ``POST`` request has been buffered.
   If enabled, `proxy.config.http.post_copy_size` needs to be set to the maximum of the post body
   size allowed, otherwise, the post would fail.

.. ts:cv:: CONFIG proxy.config.http.request_line_max_size INT 65535
   :reloadable:

   Controls the maximum size, in bytes, of an HTTP Request Line in requests. Requests
   with a request line exceeding this size will be treated as invalid and
   rejected by the proxy. Note that the HTTP request line typically includes HTTP method,
   request target and HTTP version string except when the request is made using absolute
   URI in which case the request line may also include the request scheme and domain name.

.. ts:cv:: CONFIG proxy.config.http.header_field_max_size INT 131070
   :reloadable:

   Controls the maximum size, in bytes, of an HTTP header field in requests. Headers
   in a request with the sum of their name and value that exceed this size will cause the
   entire request to be treated as invalid and rejected by the proxy.

.. ts:cv:: CONFIG proxy.config.http.request_header_max_size INT 131072
   :overridable:
   :reloadable:

   Controls the maximum size, in bytes, of an HTTP header in requests. Headers
   in a request which exceed this size will cause the entire request to be
   treated as invalid and rejected by the proxy.

.. ts:cv:: CONFIG proxy.config.http.response_header_max_size INT 131072
   :overridable:
   :reloadable:

   Controls the maximum size, in bytes, of headers in HTTP responses from the
   proxy. Any responses with a header exceeding this limit will be treated as
   invalid and a client error will be returned instead.

.. ts:cv:: CONFIG proxy.config.http.global_user_agent_header STRING null
   :overridable:

   An arbitrary string value that, if set, will be used to replace any request
   ``User-Agent`` header.

.. ts:cv:: CONFIG proxy.config.http.strict_uri_parsing INT 2

   Takes a value between 0 and 2.  ``0`` disables strict_uri_parsing.  Any character can appears
   in the URI.  ``1`` causes |TS| to return 400 Bad Request
   if client's request URI includes character which is not RFC 3986 compliant. ``2`` directs |TS|
   to reject the clients request if it contains whitespace or non-printable characters.

.. ts:cv:: CONFIG proxy.config.http.errors.log_error_pages INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) the logging of responses to bad requests
   to the error logging destination. Disabling this option prevents error
   responses (such as ``403``\ s) from appearing in the error logs. Any HTTP
   response status codes equal to, or higher, than the minimum code defined by
   :c:data:`TS_HTTP_STATUS_BAD_REQUEST` are affected by this setting.

Parent Proxy Configuration
==========================

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.retry_time INT 300
   :reloadable:
   :overridable:

   The amount of time allowed between connection retries to a parent cache that is unavailable.

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.max_trans_retries INT 2

   Limits the number of simultaneous transactions that may retry a parent once the parents
   ``retry_time`` has expired.

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

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.enable_parent_timeout_markdowns INT 0
   :reloadable:
   :overridable:

   Enables (``1``) or disables (``0``) parent proxy mark downs due to inactivity
   timeouts.  By default parent proxies are not marked down due to inactivity
   timeouts, the transaction will retry using another parent instead.  The
   default for this configuration keeps this behavior and is disabled (``0``).
   This setting is overridable using one of the two plugins ``header_rewrite``
   or ``conf_remap`` to enable inactivity timeout markdowns and should be done
   so rather than enabling this globally. This setting should not be used in
   conjunction with ``proxy.config.http.parent_proxy.disable_parent_markdowns``

.. ts:cv:: CONFIG proxy.config.http.parent_proxy.disable_parent_markdowns INT 0
   :reloadable:
   :overridable:

   Enables (``1``) or disables (``0``) parent proxy markdowns.  This is useful
   if parent entries in a parent.config line are VIP's and one doesn't wish
   to mark down a VIP which may have several origin or parent proxies behind
   the load balancer.  This setting is overridable using one of the
   ``header_rewrite`` or the ``conf_remap`` plugins to override the default
   setting and this method should be used rather than disabling markdowns
   globally.  This setting should not be used in conjunction with
   ``proxy.config.http.parent_proxy.enable_parent_timeout_markdowns``

HTTP Connection Timeouts
========================

.. ts:cv:: CONFIG proxy.config.http.keep_alive_no_activity_timeout_in INT 120
   :reloadable:
   :overridable:

   Specifies how long |TS| keeps connections to clients open for a
   subsequent request after a transaction ends. A value of ``0`` will set
   `proxy.config.net.default_inactivity_timeout` as the timeout.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.keep_alive_no_activity_timeout_out INT 120
   :reloadable:
   :overridable:

   Specifies how long |TS| keeps connections to origin servers open
   for a subsequent transfer of data after a transaction ends. A value of ``0`` will
   set `proxy.config.net.default_inactivity_timeout` as the timeout.

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

   .. note:: In previous versions proxy.config.http.redirection_enabled had to be set to 1 before this setting was evaluated.  Now setting :ts:cv:`proxy.config.http.number_of_redirections` to a value greater than zero is sufficient to cause |TS| to follow redirects.

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
   reached, the origin is marked dead (as controlled by `proxy.config.http.connect.dead.policy`_.  After this, the setting
   `proxy.config.http.connect_attempts_max_retries_dead_server`_ is used to limit the number of retry attempts to the known dead origin.

.. ts:cv:: CONFIG proxy.config.http.connect_attempts_max_retries_dead_server INT 1
   :reloadable:
   :overridable:

   Maximum number of connection attempts |TS| can make while an origin is marked dead per request.  Typically this value is smaller than
   `proxy.config.http.connect_attempts_max_retries`_ so an error is returned to the client faster and also to reduce the load on the dead origin.
   The timeout interval `proxy.config.http.connect_attempts_timeout`_ in seconds is used with this setting.

.. ts:cv:: CONFIG proxy.config.http.connect.dead.policy INT 2
   :overridable:

   Controls what origin server connection failures contribute to marking a server dead. When set to 2, any connection failure during the TCP and TLS
   handshakes will contribute to marking the server dead. When set to 1, only TCP handshake failures will contribute to marking a server dead.
   When set to 0, no connection failures will be used towards marking a server dead.

.. ts:cv:: CONFIG proxy.config.http.server_max_connections INT 0
   :reloadable:

   Limits the number of socket connections across all origin servers to the
   value specified. To disable, set to zero (``0``).

   This value is used in determining when and if to prune active origin
   sessions. Without this value set, connections to origins can consume all the
   way up to :ts:cv:`proxy.config.net.connections_throttle` connections, which
   in turn can starve incoming requests from available connections.

.. ts:cv:: CONFIG proxy.config.http.per_server.connection.max INT 0
   :reloadable:
   :overridable:

   Set a limit for the number of concurrent connections to an upstream server group. A value of
   ``0`` disables checking. If a transaction attempts to connect to a group which already has the
   maximum number of concurrent connections a 503
   (``HTTP_STATUS_SERVICE_UNAVAILABLE``) error response is sent to the user agent. To configure

   Upstream server group definition
      See :ts:cv:`proxy.config.http.per_server.connection.match`.

   Frequency of alerts
      See :ts:cv:`proxy.config.http.per_server.connection.alert_delay`.

.. ts:cv:: CONFIG proxy.config.http.per_server.connection.match STRING both
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

.. note::

   This setting is independent of the :ts:cv:`setting for upstream session sharing matching
   <proxy.config.http.server_session_sharing.match>`.

.. ts:cv:: CONFIG proxy.config.http.per_server.connection.alert_delay INT 60
   :reloadable:
   :units: seconds

   Throttle alerts per upstream server group to be no more often than this many seconds. Summary
   data is provided per alert to allow log scrubbing to generate accurate data.

.. ts:cv:: CONFIG proxy.config.http.per_server.connection.min INT 0
   :reloadable:
   :overridable:

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

   The timeout value (in seconds) for time to set up a connection to the origin. After the connection is established the value of
   ``proxy.config.http.transaction_no_activity_timeout_out`` is used to established timeouts on the data over the connection.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.http.post.check.content_length.enabled INT 1

    Enables (``1``) or disables (``0``) checking the Content-Length: Header for a POST request.

.. ts:cv:: CONFIG proxy.config.http.down_server.cache_time INT 60
   :reloadable:
   :overridable:

   Specifies how long (in seconds) |TS| remembers that an origin server was unreachable.

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
   ``403``                Forbidden
   ``404``                Not Found
   ``414``                URI Too Long
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

.. ts:cv:: CONFIG proxy.config.http.negative_caching_list STRING 204 305 403 404 414 500 501 502 503 504
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
   When considering replying with a stale response in these negative revalidating circumstances,
   |TS| will respect the :ts:cv:`proxy.config.http.cache.max_stale_age` configuration and will not
   use a cached response older than ``max_stale_age`` seconds.

   A value of ``0`` disables serving stale content and a value of ``1`` enables keeping and serving stale content if revalidation fails.

.. ts:cv:: CONFIG proxy.config.http.negative_revalidating_lifetime INT 1800

   When replying with a stale cached response in negative revalidating circumstances (see
   :ts:cv:`proxy.config.http.negative_revalidating_enabled`), |TS| includes an ``Expires:`` HTTP
   header field in the cached response with a future time so that upstream caches will not try to
   revalidate their respective stale objects. This configuration specifies how many seconds in the
   future |TS| will calculate the value of this inserted ``Expires:`` header field.

   There is a limitation to this method to be aware of: per specification (see IETF RFC 7234,
   section 4.2.1), ``Cache-Control:`` response directives take precedence over the ``Expires:``
   header field when determining object freshness. Thus if the cached response contains either a
   ``max-age`` or an ``s-maxage`` ``Cache-Control:`` response directive, then these directives would
   take precedence for the upstream caches over the inserted ``Expires:`` field, rendering the
   ``Expires:`` header ineffective in specifying the configured freshness lifetime.

   Finally, be aware that the only way this configuration is used is as input into calculating the
   value of these inserted ``Expires:`` header fields. This configuration does not direct |TS|
   behavior with regard to whether it considers a stale object to be fresh enough to serve out of
   cache when revalidation fails. As mentioned above in
   :ts:cv:`proxy.config.http.negative_revalidating_enabled`,
   :ts:cv:`proxy.config.http.cache.max_stale_age` is used for that determination.

   This configuration defaults to 1,800 seconds (30 minutes).

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

   Each parameter in the list must be separated by ``|`` or ``:``.  For example, ``for|by=uuid|proto`` is
   a valid value for this variable.  Note that the ``connection`` parameter is a non-standard extension to
   RFC 7239.  Also note that, while |TS| allows multiple ``by`` parameters for the same proxy, this
   is prohibited by RFC 7239. Currently, for the ``host`` parameter to provide the original host from the
   incoming client request, `proxy.config.url_remap.pristine_host_hdr`_ must be enabled.

.. ts:cv:: CONFIG proxy.config.http.proxy_protocol_allowlist STRING ```<ip list>```

   This defines a allowlist of server IPs that are trusted to provide
   connections with Proxy Protocol information.  This is a comma delimited list
   of IP addresses.  Addressed may be listed individually, in a range separated
   by a dash or by using CIDR notation.

   ======================= ===========================================================
   Example  Effect
   ======================= ===========================================================
   ``10.0.2.123``          A single IP Address.
   ``10.0.3.1-10.0.3.254`` A range of IP address.
   ``10.0.4.0/24``         A range of IP address specified by CIDR notation.
   ======================= ===========================================================

   .. important::

       If Proxy Protocol is enabled on the port, but this directive is not
       defined any server may initiate a connection with Proxy Protocol
       information.
       See :ts:cv:`proxy.config.http.server_ports` for information on how to enable Proxy Protocol on a port.

   See :ref:`proxy-protocol` for more discussion on how |TS| transforms the `Forwarded: header`.

.. ts:cv:: CONFIG proxy.config.http.proxy_protocol_out INT -1
   :reloadable:
   :overridable:

   Set the behavior of outbound PROXY Protocol.

   =========== ======================================================================
   Value       Description
   =========== ======================================================================
   ``-1``      Disable (default)
   ``0``       Forward received PROXY protocol to the next hop
   ``1``       Send client information in PROXY protocol version 1
   ``2``       Send client information in PROXY protocol version 2
   =========== ======================================================================

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
   ``3`` ``Accept-Encoding: br, gzip`` (if the header has ``br`` and ``gzip`` (with any ``q`` for either) then ``br, gzip``) **ELSE**
         normalize as for value ``2``
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
       a filtering rule in the ip_allow.yaml file to allow only certain
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

.. ts:cv:: CONFIG proxy.config.http.host_sni_policy INT 2

   This option controls how host header and SNI name mismatches are handled.  Mismatches
   may result in SNI-based policies defined in :file:`sni.yaml` being avoided.  For example, ``foo.com``
   may be the fqdn value in :file:`sni.yaml` which defines that client certificates are required.
   The user could specify ``bar.com`` as the SNI to avoid the policy requiring the client certificate
   but specify ``foo.com`` as the HTTP host header to still access the same object.

   Therefore, if a host header would have triggered a SNI policy, it is possible that the user is
   trying to bypass a SNI policy if the host header and SNI values do not match.

   If this setting is 0, no checking is performed.  If this setting is 1 or 2, the host header and SNI values
   are compared if the host header value would have triggered a SNI policy.  If there is a mismatch and the value
   is 1, a warning is generated but the transaction is allowed to proceed.  If the value is 2 and there is a
   mismatch, a warning is generated and a status 403 is returned.

   Note that SNI and hostname consistency checking is not performed on all connections indiscriminately, even if this
   global ``proxy.config.http.host_sni_policy`` is set to a value of 1 or 2. It is only performed for connections to
   hosts specifying ``verify_client`` and/or ``ip_allow`` policies in :file:`sni.yaml`. That is, the SNI and hostname
   mismatch check is only performed if a relevant security policy for the SNI is set in :file:`sni.yaml`. The
   ``proxy.config.http.host_sni_policy`` :file:`records.yaml` value is used as the default value if either of these
   policies is set in the corresponding :file:`sni.yaml` file entry and the :file:`sni.yaml` entry does not override
   this value via a :ref:`host_sni_policy<override-host-sni-policy>` attribute.


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

   SSDs and "advanced format" drives claim a sector size of 512; however, it is safe to force a higher
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

.. ts:cv:: CONFIG proxy.config.http.cache.post_method INT 0
   :reloadable:
   :overridable:

   Enables (``1``) or disables (``0``) caching of HTTP POST requests.

.. ts:cv:: CONFIG proxy.config.http.cache.generation INT -1
   :reloadable:
   :overridable:

   If set to a value other than ``-1``, the value if this configuration
   option is combined with the cache key at cache lookup time.
   Changing this value has the effect of an instantaneous, zero-cost
   cache purge since it will cause all subsequent cache keys to
   change. Since this is an overridable configuration, it can be
   used to purge the entire cache, or just a specific :file:`remap.config`
   rule.

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

   When enabled (``1``), |TS| ignores client requests to bypass the cache. Specifically, ``Pragma: no-cache``, ``Cache-Control: no-cache`` and ``Cache-Control: no-store`` in requests are ignored.

.. ts:cv:: CONFIG proxy.config.http.cache.ims_on_client_no_cache INT 1
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| issues a conditional request to the origin server if an incoming request has a ``No-Cache`` header.

.. ts:cv:: CONFIG proxy.config.http.cache.ignore_server_no_cache INT 0
   :reloadable:
   :overridable:

   When enabled (``1``), |TS| ignores origin server requests to bypass the cache. Specifically, ``Pragma: no-cache``, ``Cache-Control: no-cache`` and ``Cache-Control: no-store`` in responses are ignored.

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

   When enabled (``1``), |TS| ignores ``WWW-Authentication`` headers in
   responses and the responses are cached.

.. ts:cv:: CONFIG proxy.config.http.cache.cache_urls_that_look_dynamic INT 1
   :reloadable:
   :overridable:

   Enables (``1``) or disables (``0``) caching of URLs that look dynamic, i.e.: URLs that end in ``.asp`` or contain a question
   mark (``?``), a semicolon (``;``), or ``cgi``. For a full list, please refer to
   `HttpTransact::url_looks_dynamic </link/to/doxygen>`_

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

   The maximum age in seconds allowed for a stale response before it cannot be cached.

.. ts:cv:: CONFIG proxy.config.http.cache.guaranteed_min_lifetime INT 0
   :reloadable:
   :overridable:

   Establishes a guaranteed minimum lifetime boundary for object freshness.
   Setting this to ``0`` (default) disables the feature.

.. ts:cv:: CONFIG proxy.config.http.cache.guaranteed_max_lifetime INT 31536000
   :reloadable:
   :overridable:

   Establishes a guaranteed maximum lifetime boundary for object freshness.
   Setting this to ``0`` disables the feature.

.. ts:cv:: CONFIG proxy.config.http.cache.range.lookup INT 1
   :overridable:

   When enabled (``1``), |TS| looks up range requests in the cache.

.. ts:cv:: CONFIG proxy.config.http.cache.range.write INT 0
   :overridable:

   When enabled (``1``), |TS| will attempt to write (lock) the URL
   to cache for a request specifying a range. This is useful when the origin server
   might ignore a range request and respond with a full (``200``) response.
   Additionally, this setting will attempt to transform a 200 response from the origin
   server to a partial (``206``) response, honoring the requested range, while
   caching the full response.

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
   :term:`write cursor` that constitutes a recent access hit for evacuating the accessed object.

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

.. ts:cv:: CONFIG proxy.config.cache.log.alternate.eviction INT 0

   When enabled (``1``), |TS| will emit a Status level log entry every time an
   alternate for an object is evicted due to the number of its alternates
   exceeding the value of :ts:cv:`proxy.config.cache.limits.http.max_alts`. The
   URI for the evicted alternate is included in the log. This logging may be
   useful to determine whether :ts:cv:`proxy.config.cache.limits.http.max_alts`
   is tuned correctly for a given environment. It also provides visibility into
   alternate eviction for individual objects, which can be helpful for
   diagnosing unexpected `Vary:` header behavior from particular origins.

   For further details concerning the caching of alternates, see :ref:`Caching
   HTTP Alternates <CachingHttpAlternates>`.

   By default, alternate eviction logging is disabled (set to ``0``).

.. ts:cv:: CONFIG proxy.config.cache.target_fragment_size INT 1048576

   Sets the target size of a contiguous fragment of a file in the disk cache.
   When setting this, consider that larger numbers could waste memory on slow
   connections, but smaller numbers could increase (waste) seeks.

.. ts:cv:: CONFIG proxy.config.cache.alt_rewrite_max_size INT 4096
   :reloadable:

   Configures the size, in bytes, of an alternate that will be considered
   small enough to trigger a rewrite of the resident alt fragment within a
   write vector. For further details on cache write vectors, refer to the
   developer documentation for :cpp:class:`CacheVC`.

.. ts::cv:: CONFIG proxy.config.cache.mutex_retry_delay INT 2
   :reloadable:
   :units: milliseconds

   The retry delay for missing a lock on a mutex in the cache component. This is used generically
   for most locks, except those that have an explicit configuration for the retry delay. For
   instance, if the cache component is notifying another continuation of a cache event and fails to
   get the lock for that continuation, it will use this as the delay for the retry. This is also
   used from the asynchronous IO threads when IO finishes and the ``CacheVC`` lock or stripe lock is
   required.

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

   Two distinct RAM caches are supported, the default (1) being the simpler
   **LRU** (*Least Recently Used*) cache. As an alternative, the **CLFUS**
   (*Clocked Least Frequently Used by Size*) is also available, by changing this
   configuration to 0.

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

Dynamic Content & Content Negotiation
=====================================

.. ts:cv:: CONFIG proxy.config.http.cache.open_read_retry_time INT 10
   :reloadable:
   :overridable:

    The number of milliseconds a cacheable request will wait before requesting the object from cache if an equivalent request is in flight.

.. ts:cv:: CONFIG proxy.config.http.cache.max_open_read_retries INT -1
   :reloadable:
   :overridable:

    The number of times to attempt fetching an object from cache if there was an equivalent request in flight.

.. ts:cv:: CONFIG proxy.config.http.cache.max_open_write_retries INT 1
   :reloadable:
   :overridable:

    The number of times to attempt a cache open write upon failure to get a write lock.

    This config is ignored when :ts:cv:`proxy.config.http.cache.open_write_fail_action` is
    set to ``5`` or :ts:cv:`proxy.config.http.cache.max_open_write_retry_timeout` is set to gt ``0``.

.. ts:cv:: CONFIG proxy.config.http.cache.max_open_write_retry_timeout INT 0
   :reloadable:
   :overridable:

    A timeout for attempting a cache open write upon failure to get a write lock.

    This config is ignored when :ts:cv:`proxy.config.http.cache.open_write_fail_action` is
    set to ``5``.

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
   ``5`` Retry Cache Read on a Cache Write Lock failure. This option together
         with :ts:cv:`proxy.config.cache.enable_read_while_writer` configuration
         allows to collapse concurrent requests without a need for any plugin.
         Make sure to configure the :ref:`admin-config-read-while-writer` feature
         correctly. Note that this option may result in CACHE_LOOKUP_COMPLETE HOOK
         being called back more than once.
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
    this value and an underscore are prepended to the file name to find in the template sets
    directory. See :ref:`body-factory`.

.. ts:cv:: CONFIG proxy.config.body_factory.response_max_size INT 8192
   :reloadable:

    Maximum size of the error template response page.

.. ts:cv:: CONFIG proxy.config.body_factory.response_suppression_mode INT 0
   :reloadable:
   :overridable:

   Specifies when |TS| suppresses generated response pages:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Never suppress generated response pages.
   ``1`` Always suppress generated response pages.
   ``2`` Suppress response pages only for internal traffic.
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

   To enable any endpoint there needs to be an entry in :file:`remap.config` which
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
   :reloadable:

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

   Allows one to specify which ``resolv.conf`` file to use for finding resolvers. While the format of this file must be the same as the
   standard ``resolv.conf`` file, this option allows an administrator to manage the set of resolvers in an external configuration file,
   without affecting how the rest of the operating system uses DNS. Note that this setting works in conjunction with
   :ts:cv:`proxy.config.dns.nameservers`, with its settings appended to the ``resolv.conf`` contents.

.. ts:cv:: CONFIG proxy.config.dns.round_robin_nameservers INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) DNS server round-robin.

.. ts:cv:: CONFIG proxy.config.dns.nameservers STRING NULL
   :reloadable:

   The DNS servers. Note that this does not override :ts:cv:`proxy.config.dns.resolv_conf`.
   That is, the contents of the file listed in :ts:cv:`proxy.config.dns.resolv_conf` will
   be appended to the list of nameservers specified here. To prevent this, a bogus file
   can be listed there.

.. topic:: Example

   IPv4 DNS server, loopback and port 9999 ::

      CONFIG proxy.config.dns.nameservers STRING 127.0.0.1:9999

.. topic:: Example

   IPv6 DNS server, loopback and port 9999 ::

      CONFIG proxy.config.dns.nameservers STRING [::1]:9999

.. ts:cv:: CONFIG proxy.config.srv_enabled INT 0
   :reloadable:
   :overridable:

   Enables (``1``) or disables (``0``) the use of SRV records for origin server
   lookup. |TS| will use weights found in the SRV record as a weighted round
   robin in origin selection. Note that |TS| will lookup
   ``_$scheme._$internet_protocol.$origin_name``. For instance, if the origin is
   set to ``https://my.example.com``, |TS| would lookup ``_https._tcp.my.example.com``.
   Also note that the port returned in the SRV record MUST match the port being
   used for the origin (e.g. if the origin scheme is http and a default port, there
   should be a SRV record with port 80).

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

.. ts:cv:: CONFIG proxy.config.dns.max_tcp_continuous_failures INT 10

   If DNS connection mode is TCP_RETRY, set the threshold of the continuous TCP
   query failures count for the TCP connection, reset the TCP connection immediately
   if the continuous TCP query failures conut over the threshold. If the threshold
   is 0 (or less than 0) we close this feature.

.. ts:cv:: CONFIG proxy.config.dns.max_dns_in_flight INT 2048

   Maximum inflight DNS queries made by |TS| at any given instant

.. ts:cv:: CONFIG proxy.config.dns.lookup_timeout INT 20

   Time to wait for a DNS response in seconds.

.. ts:cv:: CONFIG proxy.config.dns.retries INT 5

   Maximum number of retries made by |TS| on a given DNS query

.. ts:cv:: CONFIG proxy.config.dns.local_ipv4 STRING NULL

   Local IPV4 address to bind to in order to make DNS requests

.. ts:cv:: CONFIG proxy.config.dns.local_ipv6 STRING NULL

   Local IPV6 address to bind to in order to make DNS requests


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

.. ts:cv:: CONFIG proxy.config.hostdb.round_robin_max_count INT 16

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

.. ts:cv:: CONFIG proxy.config.hostdb.timeout INT 86400
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
   the standard .
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
   :overridable:

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

.. ts:cv:: CONFIG proxy.config.hostdb.filename STRING host.db

   The filename to persist hostdb to on disk.

.. ts:cv:: CONFIG proxy.config.cache.hostdb.sync_frequency INT 0

   Set the frequency (in seconds) to sync hostdb to disk. If set to zero (default as of v9.0.0), we won't
   sync to disk ever.

   Note: hostdb is synced to disk on a per-partition basis (of which there are 64).
   This means that the minimum time to sync all data to disk is :ts:cv:`proxy.config.cache.hostdb.sync_frequency` * 64

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

.. ts:cv:: CONFIG proxy.config.log.log_fast_buffer INT 0
   :reloadable:

   Enables ``fast`` logging mode as the default for all log objects.  This mode
   can log larger transaction rates, but log entries will appear out of order
   in the log output. You can enable ``fast`` mode for individual log objects in
   ``logging.yaml`` file by adding ``fast: true`` to that object's config.

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
   even if they are not log files.

.. ts:cv:: CONFIG proxy.config.log.max_space_mb_headroom INT 1000
   :units: megabytes
   :reloadable:

   The tolerance for the log space limit (in megabytes). If the variable :ts:cv:`proxy.config.log.auto_delete_rolled_files` is set to ``1``
   (enabled), then auto-deletion of log files is triggered when the amount of free space available in the logging directory is less than
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

.. ts:cv:: CONFIG proxy.config.log.rolling_min_count INT 0
   :reloadable:

   Specifies the minimum count of rolled (event) logs to keep. This value will be used to decide the
   order of auto-deletion (if enabled). A default value of 0 means auto-deletion will try to keep
   logs as much as possible. This value can be and should be overridden in logging.yaml. See :doc:`../logging/rotation.en` for guidance.

.. ts:cv:: CONFIG proxy.config.log.rolling_max_count INT 0
   :reloadable:

   Specifies the maximum count of rolled output logs to keep. This value will be used by the
   auto-deletion (if enabled) to trim the number of rolled log files every time the log is rolled.
   A default value of 0 means auto-deletion will not try to limit the number of output logs.
   See :doc:`../logging/rotation.en` for an use-case for this option.

.. ts:cv:: CONFIG proxy.config.log.rolling_allow_empty INT 0
   :reloadable:

   While rolling default behavior is to rename, close and re-open the log file *only* when/if there is
   something to log to the log file. This option opens a new log file right after rolling even if there
   is nothing to log (i.e. nothing to be logged due to lack of requests to the server)
   which may lead to 0-sized log files while rolling. See :doc:`../logging/rotation.en` for an use-case
   for this option.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` No empty log files created and rolled if there was nothing to log
   ``1`` Allow empty log files to be created and  rolled even if there was nothing to log
   ===== ======================================================================

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

.. ts:cv:: CONFIG proxy.config.log.proxy.config.log.throttling_interval_msec INT 60000
   :reloadable:
   :units: milliseconds

   The minimum amount of milliseconds between repeated throttled |TS| log
   events. A value of 0 implies no throttling. Note that for performance
   reasons only certain logs are compiled with throttling applied to them.

   Throttling is applied to all log events for a particular message which is
   emitted within its throttling interval. That is, once a throttled log is
   emitted, none will be emitted until the next log event for that message
   which occurs outside of this configured interval. As mentioned above, this
   message is applied not broadly but rather to potentially noisy log messages,
   such as ones that might occur thousands of times a second under certain
   error conditions. Once the next log event occurs outside of its interval, a
   summary message is printed conveying how many messages of that type were
   throttled since the last time it was emitted.

   It is possible that a log is emitted, followed by more of its type in an
   interval, then none are emitted after that. Be aware this would result in no
   summary log message for that interval until the message is emitted again
   outside of the throttled interval.

.. ts:cv:: CONFIG proxy.config.http.slow.log.threshold INT 0
   :reloadable:
   :units: milliseconds

   If set to a non-zero value :arg:`N` then any connection that takes longer than :arg:`N` milliseconds from accept to
   completion will cause its timing stats to be written to the :ts:cv:`debugging log file
   <proxy.config.output.logfile>`. This is identifying data about the transaction and all of the :c:type:`transaction milestones <TSMilestonesType>`.

.. ts:cv:: CONFIG proxy.config.http2.connection.slow.log.threshold INT 0
   :reloadable:
   :units: milliseconds

   If set to a non-zero value :arg:`N` then any HTTP/2 connection
   that takes longer than :arg:`N` milliseconds from open to close will cause
   its timing stats to be written to the :ts:cv:`debugging log file
   <proxy.config.output.logfile>`. This is identifying data about the
   transaction and all of the :c:type:`transaction milestones <TSMilestonesType>`.

.. ts:cv:: CONFIG proxy.config.http2.stream.slow.log.threshold INT 0
   :reloadable:
   :units: milliseconds

   If set to a non-zero value :arg:`N` then any HTTP/2 stream
   that takes longer than :arg:`N` milliseconds from open to close will cause
   its timing stats to be written to the :ts:cv:`debugging log file
   <proxy.config.output.logfile>`. This is identifying data about the
   transaction and all of the :c:type:`transaction milestones <TSMilestonesType>`.

.. ts:cv:: CONFIG proxy.config.log.config.filename STRING logging.yaml
   :reloadable:
   :deprecated:

   This configuration value specifies the path to the
   :file:`logging.yaml` configuration file. If this is a relative
   path, |TS| loads it relative to the ``SYSCONFDIR`` directory.

.. ts:cv:: CONFIG proxy.config.log.max_line_size INT 9216
   :units: bytes

   This controls the maximum line length for ``ASCII`` formatted log entries.
   This applies to ``ASCII_PIPE`` and ``ASCII`` file logs, *unless*
   :ts:cv:`proxy.config.log.ascii_buffer_size` is also specified and the value
   of ``ascii_buffer_size`` is larger than ``max_line_size``: in that case,
   ``max_line_size`` only applies to ``ASCII_PIPE`` logs while
   ``ascii_buffer_size`` will apply to ``ASCII`` (non-pipe) log files.

.. ts:cv:: CONFIG proxy.config.log.ascii_buffer_size INT 36864
   :units: bytes

   This controls the maximum line length for ``ASCII`` formatted log entries
   that are non-pipe log files. If this value is smaller than
   :ts:cv:`proxy.config.log.max_line_size`, then the latter will be used for
   both ``ASCII`` and ``ASCII_PIPE`` log files. If both ``max_line_size`` and
   ``ascii_buffer_size`` are set, then ``max_line_size`` will be used for
   ``ASCII_PIPE`` logs while ``ascii_buffer_size`` will be used for ``ASCII``
   (non-pipe) log files.  This all might seem complicated, but just keep in
   mind that the intention of ``ascii_buffer_size`` is to simply provide a way
   for the user to configure different ``ASCII`` and ``ASCII_PIPE`` maximum
   line lengths.

.. ts:cv:: CONFIG proxy.config.log.log_buffer_size INT 9216
   :reloadable:
   :units: bytes

   This is an orthogonal mechanism from :ts:cv:`proxy.config.log.max_line_size`
   and :ts:cv:`proxy.config.log.ascii_buffer_size` for limiting line length
   size by constraining the log entry buffer to a particular amount of memory.
   Unlike the above two configurations, ``log_buffer_size`` applies to both
   binary and ``ASCII`` log file entries.  For ``ASCII`` log files, if a maximum
   log size is set via both the above mechanisms and by ``log_buffer_size``,
   then the smaller of the two configurations will be applied to the line
   length.

Diagnostic Logging Configuration
================================

.. _DiagnosticOutputConfigurationVariables:

.. ts:cv:: CONFIG proxy.config.diags.output.diag STRING E
.. ts:cv:: CONFIG proxy.config.diags.output.debug STRING E
.. ts:cv:: CONFIG proxy.config.diags.output.status STRING L
.. ts:cv:: CONFIG proxy.config.diags.output.note STRING L
.. ts:cv:: CONFIG proxy.config.diags.output.warning STRING L
.. ts:cv:: CONFIG proxy.config.diags.output.error STRING SL
.. ts:cv:: CONFIG proxy.config.diags.output.fatal STRING SL
.. ts:cv:: CONFIG proxy.config.diags.output.alert STRING L
.. ts:cv:: CONFIG proxy.config.diags.output.emergency STRING SL

   The diagnostic output configuration variables control where Traffic
   Server should log diagnostic output. Messages at each diagnostic level
   can be directed to any combination of diagnostic destinations.
   Valid diagnostic message destinations are:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``O`` Log to standard output.
   ``E`` Log to standard error.
   ``S`` Log to syslog.
   ``L`` Log to :file:`diags.log` (with the filename configurable via
         :ts:cv:`proxy.config.diags.logfile.filename`).
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

   When set to 3, enables logging for diagnostic messages whose log level is `diag` or `debug`, except those
   output by deprecated functions such as `TSDebug()` and `TSDebugSpecific()`.  Using the value 3 will have less
   of a negative impact on proxy throughput than using the value 1.

.. ts:cv:: CONFIG proxy.config.diags.debug.client_ip STRING NULL

   if :ts:cv:`proxy.config.diags.debug.enabled` is set to 2, this value is tested against the source IP of the incoming connection.  If there is a match, all the diagnostic messages for that connection and the related outgoing connection will be logged.

.. ts:cv:: CONFIG proxy.config.diags.debug.tags STRING http|dns

   Each |TS| `diag` and `debug` level message is annotated with a subsystem tag.  This configuration
   contains an anchored regular expression that filters the messages based on the tag. The
   expressions are prefix matched which creates an implicit ``.*`` at the end. Therefore the default
   value ``http|dns`` will match tags such as ``http``, ``http_hdrs``, ``dns``, and ``dns_recv``.

   Some commonly used debug tags are:

   ============  =====================================================
   Tag           Subsystem usage
   ============  =====================================================
   dns           DNS query resolution
   http_hdrs     Logs the headers for HTTP requests and responses
   privileges    Privilege elevation
   ssl           TLS termination and certificate processing
   ============  =====================================================

   |TS| plugins will typically log debug messages using the :c:func:`TSDbg`
   API, passing the plugin name as the debug tag.

.. ts:cv:: CONFIG proxy.config.diags.debug.throttling_interval_msec INT 0
   :reloadable:
   :units: milliseconds

   The minimum amount of milliseconds between repeated |TS| `diag` and `debug`
   log events. A value of 0 implies no throttling. All diags and debug logs
   are compiled with throttling applied to them.

   For details about how log throttling works, see
   :ts:cv:`log.throttling_interval_msec
   <proxy.config.log.proxy.config.log.throttling_interval_msec>`.

.. ts:cv:: CONFIG proxy.config.diags.logfile.filename STRING diags.log

   The name of the file to which |TS| diagnostic logs will be emitted. For
   information on the diagnostic log file, see :file:`diags.log`. For the
   configurable parameters concerning what log content is emitted to
   :file:`diags.log`, see the :ref:`Diagnostic Output Configuration Variables
   <DiagnosticOutputConfigurationVariables>` above.

   If this is set to ``stdout`` or ``stderr``, then all diagnostic logging will
   go to the stdout or stderr stream, respectively.

.. ts:cv:: CONFIG proxy.config.error.logfile.filename STRING error.log

   The name of the file to which |TS| transaction error logs will be emitted.
   For more information on these log messages, see :file:`error.log`.

   If this is set to ``stdout`` or ``stderr``, then all transaction error
   logging will go to the stdout or stderr stream, respectively.

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

   Specifies how often the diagnostics log is rolled, in seconds. The timer starts on |TS| startup.

.. ts:cv:: CONFIG proxy.config.diags.logfile.rolling_size_mb INT 100
   :reloadable:
   :units: megabytes

   Specifies at what size to roll the diagnostics log at.

.. ts:cv:: CONFIG proxy.config.diags.logfile.rolling_min_count INT 0
   :reloadable:

   Specifies the minimum count of rolled diagnostic logs to keep. This value will be used to decide the
   order of auto-deletion (if enabled). A default value of 0 means auto-deletion will try to keep
   diagnostic logs as much as possible. See :doc:`../logging/rotation.en` for guidance.

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
   :deprecated:

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

.. ts:cv:: CONFIG proxy.config.url_remap.min_rules_required INT 0
   :reloadable:

   The minimum number of rules :file:`remap.config` must have to be considered valid. An otherwise
   valid configuration with fewer than this many rules is considered to be invalid as if it had a syntax
   error. A value of zero allows :file:`remap.config` to be empty or absent.

   This is dynamic to enable different requirements for startup and reloading.

.. _records-config-ssl-termination:

SSL Termination
===============

.. ts:cv:: CONFIG proxy.config.ssl.server.cipher_suite STRING <see notes>

   Configures the set of encryption, digest, authentication, and key exchange
   algorithms provided by OpenSSL which |TS| will use for SSL connections. For
   the list of algorithms and instructions on constructing an appropriately
   formatting cipher_suite string, see
   `OpenSSL Ciphers <https://www.openssl.org/docs/manmaster/man1/ciphers.html>`_.

   The current default is:

   ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES256-CCM:ECDHE-ECDSA-AES128-CCM:ECDHE-ECDSA-AES256-CCM8:ECDHE-ECDSA-AES128-CCM8:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES256-SHA384:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES256-GCM-SHA384:ECDHE-RSA-AES128-GCM-SHA256:ECDHE-RSA-CHACHA20-POLY1305:ECDHE-RSA-AES256-SHA384:ECDHE-RSA-AES128-SHA256:DHE-RSA-AES256-GCM-SHA384:DHE-RSA-AES128-GCM-SHA256:DHE-RSA-AES256-CCM8:DHE-RSA-AES128-CCM8:DHE-RSA-AES256-CCM:DHE-RSA-AES128-CCM:DHE-RSA-AES256-SHA256:DHE-RSA-AES128-SHA256:AES256-GCM-SHA384:AES128-GCM-SHA256:AES256-CCM8:AES128-CCM8:AES256-CCM:AES128-CCM:AES256-SHA256:AES128-SHA2

.. ts:cv:: CONFIG proxy.config.ssl.client.cipher_suite STRING <See notes under proxy.config.ssl.server.cipher_suite.>

   Configures the cipher_suite which |TS| will use for SSL connections to origin or next hop.  This currently defaults to:

   ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:DHE-DSS-AES256-GCM-SHA384:DHE-RSA-AES256-GCM-SHA384:ECDHE-ECDSA-AES256-CCM8:ECDHE-ECDSA-AES256-CCM:DHE-RSA-AES256-CCM8:DHE-RSA-AES256-CCM:ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-RSA-CHACHA20-POLY1305:DHE-RSA-CHACHA20-POLY1305:ECDHE-ECDSA-ARIA256-GCM-SHA384:ECDHE-ARIA256-GCM-SHA384:DHE-DSS-ARIA256-GCM-SHA384:DHE-RSA-ARIA256-GCM-SHA384:ECDHE-ECDSA-AES256-SHA384:ECDHE-RSA-AES256-SHA384:DHE-RSA-AES256-SHA256:DHE-DSS-AES256-SHA256:ECDHE-ECDSA-CAMELLIA256-SHA384:ECDHE-RSA-CAMELLIA256-SHA384:DHE-RSA-CAMELLIA256-SHA256:DHE-DSS-CAMELLIA256-SHA256:RSA-PSK-AES256-GCM-SHA384:RSA-PSK-CHACHA20-POLY1305:RSA-PSK-ARIA256-GCM-SHA384:AES256-GCM-SHA384:AES256-CCM8:AES256-CCM:ARIA256-GCM-SHA384:AES256-SHA256:CAMELLIA256-SHA256:ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256:DHE-DSS-AES128-GCM-SHA256:DHE-RSA-AES128-GCM-SHA256:ECDHE-ECDSA-AES128-CCM8:ECDHE-ECDSA-AES128-CCM:DHE-RSA-AES128-CCM8:DHE-RSA-AES128-CCM:ECDHE-ECDSA-ARIA128-GCM-SHA256:ECDHE-ARIA128-GCM-SHA256:DHE-DSS-ARIA128-GCM-SHA256:DHE-RSA-ARIA128-GCM-SHA256:ECDHE-ECDSA-AES128-SHA256:ECDHE-RSA-AES128-SHA256:DHE-RSA-AES128-SHA256:DHE-DSS-AES128-SHA256:ECDHE-ECDSA-CAMELLIA128-SHA256:ECDHE-RSA-CAMELLIA128-SHA256:DHE-RSA-CAMELLIA128-SHA256:DHE-DSS-CAMELLIA128-SHA256:RSA-PSK-AES128-GCM-SHA256:RSA-PSK-ARIA128-GCM-SHA256:AES128-GCM-SHA256:AES128-CCM8:AES128-CCM:ARIA128-GCM-SHA256:AES128-SHA256:CAMELLIA128-SHA256

.. ts:cv:: CONFIG proxy.config.ssl.server.TLSv1_3.cipher_suites STRING <See notes>

   Configures the pair of the AEAD algorithm and hash algorithm to be
   used with HKDF provided by OpenSSL which |TS| will use for TLSv1.3
   connections. For the list of algorithms and instructions, see
   The ``-ciphersuites`` section of `OpenSSL Ciphers <https://www.openssl.org/docs/manmaster/man1/ciphers.html>`_.

   The current default value is:

   TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_SHA256

   This configuration works with OpenSSL v1.1.1 and above.

.. ts:cv:: CONFIG proxy.config.ssl.server.honor_cipher_order INT 1

   By default (``1``) |TS| will use the server's cipher suites preferences instead of the client preferences.
   By disabling it (``0``) |TS| will use client's cipher suites preferences.

.. ts:cv:: CONFIG proxy.config.ssl.server.prioritize_chacha INT 0

   By enabling it (``1``) |TS| will temporarily reprioritize ChaCha20-Poly1305 ciphers to the top of the
   server cipher list if a ChaCha20-Poly1305 cipher is at the top of the client cipher list.

   This configuration works with OpenSSL v1.1.1 and above.

.. ts:cv:: CONFIG proxy.config.ssl.client.TLSv1_3.cipher_suites STRING <See notes under proxy.config.ssl.server.tls.cipher_suites>

   Configures the cipher_suites which |TS| will use for TLSv1.3
   connections to origin or next hop. This configuration works
   with OpenSSL v1.1.1 and above.

   The current default is:

   TLS_AES_256_GCM_SHA384:TLS_AES_128_GCM_SHA256:TLS_CHACHA20_POLY1305_SHA256

.. ts:cv:: CONFIG proxy.config.ssl.server.groups_list STRING <See notes>

   Configures the list of supported groups provided by OpenSSL which
   |TS| will be used to determine the set of shared groups. The value
   is a colon separated list of group NIDs or names, for example
   "P-521:P-384:P-256". For instructions, see "Groups" section of
   `TLS1.3 - OpenSSLWiki <https://wiki.openssl.org/index.php/TLS1.3#Groups>`_.

   The current default value with OpenSSL is:

   X25519:P-256:X448:P-521:P-384

   This configuration works with OpenSSL v1.0.2 and above.

.. ts:cv:: CONFIG proxy.config.ssl.client.groups_list STRING <See notes under proxy.config.ssl.server.groups_list.>

   Configures the list of supported groups provided by OpenSSL which
   |TS| will use for the "key_share" and "supported groups" extension
   of TLSv1.3 connections. The value is a colon separated list of
   group NIDs or names, for example "P-521:P-384:P-256". For
   instructions, see "Groups" section of `TLS1.3 - OpenSSLWiki <https://wiki.openssl.org/index.php/TLS1.3#Groups>`_.

   This configuration works with OpenSSL v1.0.2 and above.

.. ts:cv:: CONFIG proxy.config.ssl.TLSv1 INT 0

   Enables (``1``) or disables (``0``) TLSv1.0. If not specified, disabled by default.

.. ts:cv:: CONFIG proxy.config.ssl.TLSv1_1 INT 0

   Enables (``1``) or disables (``0``) TLS v1.1.  If not specified, disabled by default.  [Requires OpenSSL v1.0.1 and higher]

.. note::
   In order to enable TLS v1 or v1.1, additional ciphers must be added to proxy.config.ssl.client.cipher_suite. For
   example this list would restore the SHA1 (insecure!) cipher suites suitable for these deprecated TLS versions:

   ECDHE-ECDSA-AES256-SHA:ECDHE-RSA-AES256-SHA:DHE-RSA-AES256-SHA:ECDHE-ECDSA-AES128-SHA:ECDHE-RSA-AES128-SHA:DHE-RSA-AES128-SHA:AES256-SHA:AES128-SHA


.. ts:cv:: CONFIG proxy.config.ssl.TLSv1_2 INT 1

   Enables (``1``) or disables (``0``) TLS v1.2.  If not specified, enabled by default.  [Requires OpenSSL v1.0.1 and higher]

.. ts:cv:: CONFIG proxy.config.ssl.TLSv1_3 INT 1

   Enables (``1``) or disables (``0``) TLS v1.3.  If not specified, enabled by default.  [Requires OpenSSL v1.1.1 and higher]

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
   :deprecated:

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

.. ts:cv:: CONFIG proxy.config.ssl.server.ticket_key.filename STRING NULL

   The filename of the default and global ticket key for SSL sessions. The location is relative to the
   :ts:cv:`proxy.config.ssl.server.cert.path` directory. One way to generate this would be to run
   ``head -c48 /dev/urandom | openssl enc -base64 | head -c48 > file.ticket``. Also
   note that OpenSSL session tickets are sensitive to the version of the ca-certificates. Once the
   file is changed with new tickets, use :option:`traffic_ctl config reload` to begin using them.

.. ts:cv:: CONFIG proxy.config.ssl.servername.filename STRING sni.yaml
   :deprecated:

   The filename of the :file:`sni.yaml` configuration file.
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

.. ts:cv:: CONFIG proxy.config.ssl.origin_session_cache INT 1

   This configuration enables the SSL session cache for the origin server
   when set to ``1``.

   Setting to ``0`` disables SSL session cache for the origin server.

.. ts:cv:: CONFIG proxy.config.ssl.origin_session_cache.size INT 10240

  This configuration specifies the maximum number of entries
  the SSL session cache for the origin server may contain.

  Setting a value less than or equal to ``0`` effectively disables
  SSL session cache for the origin server.

.. ts:cv:: CONFIG proxy.config.ssl.session_cache INT 2

   Enables the SSL session cache:

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disables the session cache entirely.
   ``1`` Enables the session cache using OpenSSL's implementation.
   ``2`` Default. Enables the session cache using |TS|'s implementation. This
         implementation should perform much better than the OpenSSL
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

.. ts:cv:: CONFIG proxy.config.ssl.server.session_ticket.enable INT 1

  Set to 1 to enable Traffic Server to process TLS tickets for TLS session resumption.

.. ts:cv:: CONFIG proxy.config.ssl.server.session_ticket.number INT 2

  This configuration control the number of TLSv1.3 session tickets that are issued.
  Take into account that setting the value to 0 will disable session caching for TLSv1.3
  connections.

  Lowering this setting to ``1`` can be interesting when ``proxy.config.ssl.session_cache`` is enabled because
  otherwise for every new TLSv1.3 connection two session IDs will be inserted in the session cache.
  On the other hand, if ``proxy.config.ssl.session_cache``  is disabled, using the default value is recommended.
  In those scenarios, increasing the number of tickets could be potentially beneficial for clients performing
  multiple requests over concurrent TLS connections as per RFC 8446 clients SHOULDN'T reuse TLS Tickets.

  For more information see https://www.openssl.org/docs/man1.1.1/man3/SSL_CTX_set_num_tickets.html
  [Requires OpenSSL v1.1.1 and higher]

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

.. ts:cv:: CONFIG proxy.config.ssl.handshake_timeout_in INT 30

   When enabled this limits the total duration for the incoming side SSL
   handshake.

   See :ref:`admin-performance-timeouts` for more discussion on |TS| timeouts.

.. ts:cv:: CONFIG proxy.config.ssl.keylog_file STRING NULL
   :reloadable:

   If configured, TLS session keys for TLS connections will be logged to the
   specified file. This file is formatted in such a way that it can be
   conveniently imported into tools such as Wireshark to decrypt packet
   captures.  This should only be used for debugging purposes since the data in
   the keylog file can be used to decrypt the otherwise encrypted traffic. A
   NULL value for this disables the feature.

   This feature is disabled by default.

.. ts:cv:: CONFIG proxy.config.ssl.ktls.enabled INT 0

   Enables the use of Kernel TLS. This configuration requires OpenSSL v3.0 and
   above, and it must have been compiled with support for Kernel TLS.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disables the use of Kernel TLS.
   ``1`` Enables the use of Kernel TLS..
   ===== ======================================================================

Client-Related Configuration
----------------------------

.. ts:cv:: CONFIG proxy.config.ssl.client.verify.server.policy STRING ENFORCED
   :reloadable:
   :overridable:

   Configures |TS| to verify the origin server certificate
   with the Certificate Authority (CA). This configuration takes a value of :code:`DISABLED`, :code:`PERMISSIVE`, or :code:`ENFORCED`

   You can override this global setting on a per domain basis in the :file:`sni.yaml` file using the :ref:`verify_server_policy<override-verify-server-policy>` attribute.

   You can also override via the conf_remap plugin. Those changes will take precedence over the changes in :file:`sni.yaml`.

:code:`DISABLED`
   Server Certificate will not be verified
:code:`PERMISSIVE`
   The provided certificate will be verified and the connection will be established irrespective of the verification result. If verification fails the name of the server will be logged.
:code:`ENFORCED`
   Certificate will be verified and the connection will not be established if verification fails.

.. ts:cv:: CONFIG proxy.config.ssl.client.verify.server.properties STRING ALL
   :reloadable:
   :overridable:

   Configures |TS| for what the default verify callback should check during origin server verification.

   You can override this global setting on a per domain basis in the :file:`sni.yaml` file using the :ref:`verify_server_properties<override-verify-server-properties>` attribute.

   You can also override via the conf_remap plugin. Those changes will take precedence over the changes in .:file:`sni.yaml`

:code:`NONE`
   Check nothing in the standard callback.  Rely entirely on plugins to check the certificate.
:code:`SIGNATURE`
   Check only for a valid signature.
:code:`NAME`
   Check only that the SNI name is in the certificate.
:code:`ALL`
   Check both the signature and the name.

.. ts:cv:: CONFIG proxy.config.ssl.client.cert.filename STRING NULL
   :reloadable:
   :overridable:

   The filename of SSL client certificate installed on |TS|.

.. ts:cv:: CONFIG proxy.config.ssl.client.cert.path STRING /config
   :reloadable:

   The location of the SSL client certificate installed on Traffic
   Server.

.. ts:cv:: CONFIG proxy.config.ssl.client.private_key.filename STRING NULL
   :reloadable:
   :overridable:

   The filename of the |TS| private key. Change this variable
   only if the private key is not located in the |TS| SSL
   client certificate file.

.. ts:cv:: CONFIG proxy.config.ssl.client.private_key.path STRING NULL
   :reloadable:

   The location of the |TS| private key. Change this variable
   only if the private key is not located in the SSL client certificate
   file.

.. ts:cv:: CONFIG proxy.config.ssl.client.CA.cert.filename STRING NULL
   :reloadable:
   :overridable:

   The filename of the certificate authority against which the origin
   server will be verified.

.. ts:cv:: CONFIG proxy.config.ssl.client.CA.cert.path STRING NULL
   :reloadable:

   Specifies the location of the certificate authority file against
   which the origin server will be verified.

.. ts:cv:: CONFIG proxy.config.ssl.client.sni_policy STRING NULL
   :overridable:

   Indicate how the SNI value for the TLS connection to the origin is selected.

   ``host``
      This is the default. The value of the ``Host`` field in the proxy request is used.

   ``server_name``
      The SNI value of the inbound TLS connection is used.

   ``remap``
      The remapped upstream name is used.

   ``verify_with_name_source``
      The value of the ``Host`` field in the proxy request is used. In addition, if the names in the
      server certificate of the upstream are checked, they are checked against the remapped upstream
      name, not the SNI.

   ``@...``
      If the policy starts with the ``@`` character, it is treated as a literal, less the leading
      ``@``. E.g. if the policy is "@apache.org" the SNI will be "apache.org".

   We have two names that could be used in the transaction host header and the SNI value to the
   origin. These could be the host header from the client or the remap host name. Unless you have
   pristine host header enabled, these are likely the same values.
   If sni_policy = ``host``, both the sni and the value of the ``Host`` field to origin will be the
   same. If sni_policy = ``remap``, the sni value will be the remap host name and the host header
   will be the host header from the client.

   In addition, We may want to set the SNI and host headers the same (makes some common web servers
   happy), but the server certificate for the upstream may have a name that corresponds to the remap
   name. So instead of using the SNI name for the name check, we may want to use the remap name. So
   if sni_policy = ``verify_with_name_source``, the sni will be the host header value and the name
   to check in the server certificate will be the remap header value.

.. ts:cv:: CONFIG proxy.config.ssl.client.scheme_proto_mismatch_policy INT 2
   :overridable:

   This option controls how |TS| behaves when the client side connection
   protocol and the client request's scheme do not match. For example, if
   enforcement is enabled by setting this value to ``2`` and the client
   connection is a cleartext HTTP connection but the scheme of the URL is
   ``https://``, then |TS| will emit a warning and return an immediate 400 HTTP
   response without proxying the request to the origin.

   The default value is ``2``, meaning that |TS| will enforce that the protocol
   matches the scheme.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Disable verification that the protocol and scheme match.
   ``1`` Check that the protocol and scheme match, but only emit a warning if
         they do not.
   ``2`` Check that the protocol and scheme match and, if they do not, emit a
         warning and return an immediate HTTP 400 response.
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.ssl.client.TLSv1 INT 0

   Enables (``1``) or disables (``0``) TLSv1.0 in the ATS client context. If not specified, enabled by default

.. ts:cv:: CONFIG proxy.config.ssl.client.TLSv1_1 INT 0

   Enables (``1``) or disables (``0``) TLSv1_1 in the ATS client context. If not specified, enabled by default

.. ts:cv:: CONFIG proxy.config.ssl.client.TLSv1_2 INT 1

   Enables (``1``) or disables (``0``) TLSv1_2 in the ATS client context. If not specified, enabled by default

.. ts:cv:: CONFIG proxy.config.ssl.client.TLSv1_3 INT 1

   Enables (``1``) or disables (``0``) TLSv1_3 in the ATS client context. If not specified, enabled by default

.. ts:cv:: CONFIG proxy.config.ssl.client.alpn_protocols STRING ""
   :overridable:

   Sets the ALPN string that |TS| will send to the origin in the ClientHello of TLS handshakes.
   Configuring this to an empty string (the default configuration) means that the ALPN extension
   will not be sent as a part of the TLS ClientHello.

   Configuring the ALPN string provides a mechanism to control origin-side HTTP protocol
   negotiation. Configuring this requires an understanding of the ALPN TLS protocol extension. See
   `RFC 7301 <https://www.rfc-editor.org/rfc/rfc7301.html>`_ for details about the ALPN protocol.
   See the official `IANA ALPN protocol registration
   <https://www.iana.org/assignments/tls-extensiontype-values/tls-extensiontype-values.xhtml#alpn-protocol-ids>`_
   for the official list of ALPN protocol names. As a summary, the ALPN string is a comma-separated
   (no spaces) list of protocol names that the TLS client (|TS| in this case) supports. On the TLS
   server side (origin side in this case), the names are compared in order to the list of protocols
   supported by the origin. The first match is used, thus the ALPN list should be listed in
   decreasing order of preference. If no match is found, the TLS server is expected (per the RFC) to
   fail the TLS handshake with a fatal "no_application_protocol" alert.

   Currently, |TS| supports the following ALPN protocol names:

    - ``http/1.0``
    - ``http/1.1``

   Here are some example configurations and the consequences of each:

   ================================ ======================================================================
   Value                            Description
   ================================ ======================================================================
   ``""``                           No ALPN extension is sent by |TS| in origin-side TLS handshakes.
                                    |TS| will assume an HTTP/1.1 connection in this case.
   ``"http/1.1"``                   Only HTTP/1.1 is advertized by |TS|. Thus, the origin will
                                    either negotiate HTTP/1.1, or it will fail the handshake if that
                                    is not supported by the origin.
   ``"http/1.1,http/1.0"``          Both HTTP/1.1 and HTTP/1.0 are supported by |TS|, but HTTP/1.1
                                    is preferred.
   ``"h2,http/1.1,http/1.0"``       HTTP/2 is preferred by |TS| over HTTP/1.1 and HTTP/1.0. Thus, if the
                                    origin supports HTTP/2, it will be used for the connection. If
                                    not, it will fall back to HTTP/1.1 or, if that is not supported,
                                    HTTP/1.0. (HTTP/2 to origin is currently not supported by |TS|.)
   ``"h2"``                         |TS| only advertizes HTTP/2 support. Thus, the origin will
                                    either negotiate HTTP/2 or fail the handshake. (HTTP/2 to origin
                                    is currently not supported by |TS|.)
   ================================ ======================================================================

.. ts:cv:: CONFIG proxy.config.ssl.async.handshake.enabled INT 0

   Enables the use of OpenSSL async job during the TLS handshake.  Traffic
   Server must be build against OpenSSL 1.1 or greater or this to take affect.
   Can be useful if using a crypto engine that communicates off chip.  The
   thread will be rescheduled for other work until the crypto engine operation
   completes. A test crypto engine that inserts a 5 second delay on private key
   operations can be found at :ts:git:`contrib/openssl/async_engine.c`.

.. ts:cv:: CONFIG proxy.config.ssl.engine.conf_file STRING NULL

   Specify the location of the OpenSSL config file used to load dynamic crypto
   engines. This setting assumes an absolute path.  An example config file is at
   :ts:git:`contrib/openssl/load_engine.cnf`.

TLS v1.3 0-RTT Configuration
----------------------------

.. note::
   TLS v1.3 must be enabled in order to utilize 0-RTT early data.

.. ts:cv:: CONFIG proxy.config.ssl.server.max_early_data INT 0

   Specifies the maximum amount of early data in bytes that is permitted to be sent on a single connection.

   The minimum value that enables early data, and the suggested value for this option are both 16384 (16KB).

   Setting to ``0`` effectively disables 0-RTT.

   If you use BoringSSL, setting a value grater than 0 enables early data but the value won't be used to limit the
   maximum amount of early data.

.. ts:cv:: CONFIG proxy.config.ssl.server.allow_early_data_params INT 0

   Set to ``1`` to allow HTTP parameters on early data requests.

SNI Routing
-----------

.. ts:cv:: CONFIG proxy.config.tunnel.activity_check_period INT 0
   :units: seconds

   Frequency of checking the activity of SNI Routing Tunnel. Set to ``0`` to disable monitoring of the activity of the SNI tunnels.
   The feature is disabled by default.

.. ts:cv:: CONFIG proxy.config.tunnel.prewarm INT 0

   Enable :ref:`pre-warming-tls-tunnel`. The feature is disabled by default.

.. ts:cv:: CONFIG proxy.config.tunnel.prewarm.max_stats_size INT 100

   Max size of :ref:`dynamic stats for Pre-warming TLS Tunnel <pre-warming-tls-tunnel-stats>`.

.. ts:cv:: CONFIG proxy.config.tunnel.prewarm.algorithm INT 2

   Version of pre-warming algorithm.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``1`` Periodical pre-warming only
   ``2`` Event based pre-warming + Periodical pre-warming
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.tunnel.prewarm.event_period INT 1000
   :units: milliseconds

   Frequency of periodical pre-warming in milli-seconds.

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

.. ts:cv:: CONFIG proxy.config.ssl.ocsp.request_timeout INT 10
   :units: seconds

   Timeout (in seconds) for queries to OCSP responders.

.. ts:cv:: CONFIG proxy.config.ssl.ocsp.update_period INT 60
   :units: seconds

   Update period (in seconds) for stapling caches.

.. ts:cv:: CONFIG proxy.config.ssl.ocsp.response.path STRING NULL

   The directory path of the prefetched OCSP stapling responses. Change this
   variable only if you intend to use and administratively maintain
   prefetched OCSP stapling responses. All stapling responses listed in
   :file:`ssl_multicert.config` will be loaded relative to this
   path.

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

.. ts:cv:: CONFIG proxy.config.http2.initial_window_size_in INT 65535
   :reloadable:
   :units: bytes

   The initial HTTP/2 stream window size for inbound connections that |TS| as a
   receiver advertises to the peer. See IETF RFC 9113 section 5.2 for details
   concerning HTTP/2 flow control. See
   :ts:cv:`proxy.config.http2.flow_control.policy_in` for how HTTP/2 stream and
   session windows are maintained over the lifetime of HTTP/2 sessions.

.. ts:cv:: CONFIG proxy.config.http2.flow_control.policy_in INT 0
   :reloadable:

   Specifies the mechanism |TS| uses to maintian flow control via the HTTP/2
   stream and session windows for inbound connections. See IETF RFC 9113
   section 5.2 for details concerning HTTP/2 flow control.

   ===== ===========================================================================================
   Value Description
   ===== ===========================================================================================
   ``0`` Session and stream receive windows are initialized and maintained at the value as specified
         in :ts:cv:`proxy.config.http2.initial_window_size_in` over the lifetime of HTTP/2
         sessions.
   ``1`` Session receive windows are initialized to the value of the product of
         :ts:cv:`proxy.config.http2.initial_window_size_in` and
         :ts:cv:`proxy.config.http2.max_concurrent_streams_in` and are maintained as such over the
         lifetime of HTTP/2 sessions. Stream windows are initialized to the value of
         :ts:cv:`proxy.config.http2.initial_window_size_in` and are maintained as such over the
         lifetime of each HTTP/2 stream.
   ``2`` Session receive windows are initialized to the value of the product of
         :ts:cv:`proxy.config.http2.initial_window_size_in` and
         :ts:cv:`proxy.config.http2.max_concurrent_streams_in` and are maintained as such over the
         lifetime of HTTP/2 sessions. Stream windows are initialized to the value of
         :ts:cv:`proxy.config.http2.initial_window_size_in` but are dynamically adjusted to the
         session window size divided by the number of concurrent streams over the lifetime of HTTP/2
         sessions. That is, stream window sizes dynamically adjust to fill the session window in
         a way that shares the window equally among all concurrent streams.
   ===== ===========================================================================================

.. ts:cv:: CONFIG proxy.config.http2.max_frame_size INT 16384
   :reloadable:
   :units: bytes

   Indicates the size of the largest frame payload that the sender is willing
   to receive.

.. ts:cv:: CONFIG proxy.config.http2.header_table_size INT 4096
   :reloadable:

   The maximum size of the header compression table used to decode header
   blocks. This value will be advertised as SETTINGS_HEADER_TABLE_SIZE.

.. ts:cv:: CONFIG proxy.config.http2.header_table_size_limit INT 65536
   :reloadable:

   The maximum size of the header compression table ATS actually use when ATS
   encodes headers. Setting 0 means ATS doesn't insert headers into HPACK
   Dynamic Table, however, headers still can be encoded as indexable
   representations. The upper limit is 65536.

.. ts:cv:: CONFIG proxy.config.http2.max_header_list_size INT 131072
   :reloadable:

   This advisory setting informs a peer of the maximum size of header list
   that the sender is prepared to accept blocks. The default value, which is
   the unsigned int maximum value in |TS|, implies unlimited size.

.. ts:cv:: CONFIG proxy.config.http2.stream_priority_enabled INT 0
   :reloadable:

   Enable the experimental HTTP/2 Stream Priority feature.

.. ts:cv:: CONFIG proxy.config.http2.active_timeout_in INT 0
   :reloadable:
   :units: seconds

   This is the active timeout of the http2 connection. It is set when the connection is opened
   and keeps ticking regardless of activity level.

   The value of ``0`` specifies that there is no timeout.

.. ts:cv:: CONFIG proxy.config.http2.accept_no_activity_timeout INT 120
   :reloadable:
   :units: seconds

   Specifies how long |TS| keeps connections to clients open if no
   activity is received on the connection. Lowering this timeout can ease
   pressure on the proxy if misconfigured or misbehaving clients are opening
   a large number of connections without submitting requests.

.. ts:cv:: CONFIG proxy.config.http2.no_activity_timeout_in INT 120
   :reloadable:
   :units: seconds

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

.. ts:cv:: CONFIG proxy.config.http2.stream_error_rate_threshold FLOAT 0.1
   :reloadable:

   This is the maximum stream error rate |TS| allows on an HTTP/2 connection.
   |TS| gracefully closes connections that have stream error rates above this
   setting by sending GOAWAY frames.

.. ts:cv:: CONFIG proxy.config.http2.stream_error_sampling_threshold INT 10
   :reloadable:

   This is the threshold of sampling stream number to start checking the stream error rate.

.. ts:cv:: CONFIG proxy.config.http2.max_settings_per_frame INT 7
   :reloadable:

   Specifies how many settings in an HTTP/2 SETTINGS frame |TS| accepts.
   Clients exceeded this limit will be immediately disconnected with an error
   code of ENHANCE_YOUR_CALM.

.. ts:cv:: CONFIG proxy.config.http2.max_settings_per_minute INT 14
   :reloadable:

   Specifies how many settings in HTTP/2 SETTINGS frames |TS| accept for a minute.
   Clients exceeded this limit will be immediately disconnected with an error
   code of ENHANCE_YOUR_CALM.

.. ts:cv:: CONFIG proxy.config.http2.max_settings_frames_per_minute INT 14
   :reloadable:

   Specifies how many SETTINGS frames |TS| receives for a minute at maximum.
   Clients exceeded this limit will be immediately disconnected with an error
   code of ENHANCE_YOUR_CALM.

.. ts:cv:: CONFIG proxy.config.http2.max_ping_frames_per_minute INT 60
   :reloadable:

   Specifies how many number of PING frames |TS| receives for a minute at maximum.
   Clients exceeded this limit will be immediately disconnected with an error
   code of ENHANCE_YOUR_CALM.

.. ts:cv:: CONFIG proxy.config.http2.max_priority_frames_per_minute INT 120
   :reloadable:

   Specifies how many number of PRIORITY frames |TS| receives for a minute at maximum.
   Clients exceeded this limit will be immediately disconnected with an error
   code of ENHANCE_YOUR_CALM. If this is set to 0, the limit logic is disabled.
   This limit only will be enforced if :ts:cv:`proxy.config.http2.stream_priority_enabled`
   is set to 1.

.. ts:cv:: CONFIG proxy.config.http2.min_avg_window_update FLOAT 2560.0
   :reloadable:

   Specifies the minimum average window increment |TS| allows. The average will be calculated based on the last 5 WINDOW_UPDATE frames.
   Clients that send smaller window increments lower than this limit will be immediately disconnected with an error
   code of ENHANCE_YOUR_CALM.

.. ts:cv:: CONFIG proxy.config.http2.write_buffer_block_size INT 262144
   :reloadable:
   :units: bytes

   Specifies the size of a buffer block that is used for buffering outgoing
   HTTP/2 frames. The size will be rounded up based on power of 2.

.. ts:cv:: CONFIG proxy.config.http2.write_size_threshold FLOAT 0.5
   :reloadable:

   Specifies the size threshold for triggering write operation for sending HTTP/2
   frames. The default value is 0.5 and it measn write operation is going to be
   triggered when half or more of the buffer is occupied.

.. ts:cv:: CONFIG proxy.config.http2.write_time_threshold INT 100
   :reloadable:
   :units: milliseconds

   Specifies the time threshold for triggering write operation for sending HTTP/2
   frames. Write operation will be triggered at least once every this configured
   number of millisecond regardless of pending data size.

.. ts:cv:: CONFIG proxy.config.http2.default_buffer_water_mark INT -1
   :reloadable:
   :units: bytes

   Specifies the high water mark for all HTTP/2 frames on an outoging connection.
   Default is -1 to preserve existing water marking behavior.

   You can override this global setting on a per domain basis in the :file:`sni.yaml` file using the :ref:`http2_buffer_water_mark <override-h2-properties>` attribute.

HTTP/3 Configuration
====================

There is no configuration available yet on this release.

QUIC Configuration
====================

All configurations for QUIC are still experimental and may be changed or
removed in the future without prior notice.

.. ts:cv:: CONFIG proxy.config.quic.qlog_dir STRING NULL
   :reloadable:

    The qlog is enabled when this configuration is not NULL. And will dump
    the qlog to this dir.

.. ts:cv:: CONFIG proxy.config.quic.instance_id INT 0
   :reloadable:

   A static key used for calculating Stateless Reset Token. All instances in a
   cluster need to share the same value.

.. ts:cv:: CONFIG proxy.config.quic.connection_table.size INT 65521

   A size of hash table that stores connection information.

.. ts:cv:: CONFIG proxy.config.quic.proxy.config.quic.num_alt_connection_ids INT 65521
   :reloadable:

   A number of alternate Connection IDs that |TS| provides to a peer. It has to
   be at least 8.

.. ts:cv:: CONFIG proxy.config.quic.stateless_retry_enabled INT 0
   :reloadable:

   Enables Stateless Retry.

.. ts:cv:: CONFIG proxy.config.quic.client.vn_exercise_enabled INT 0
   :reloadable:

   Enables version negotiation exercise on origin server connections.

.. ts:cv:: CONFIG proxy.config.quic.client.cm_exercise_enabled INT 0
   :reloadable:

   Enables connection migration exercise on origin server connections.

.. ts:cv:: CONFIG proxy.config.quic.server.supported_groups STRING "P-256:X25519:P-384:P-521"
   :reloadable:

   Configures the list of supported groups provided by OpenSSL which will be
   used to determine the set of shared groups on QUIC origin server connections.

.. ts:cv:: CONFIG proxy.config.quic.client.supported_groups STRING "P-256:X25519:P-384:P-521"
   :reloadable:

   Configures the list of supported groups provided by OpenSSL which will be
   used to determine the set of shared groups on QUIC client connections.

.. ts:cv:: CONFIG proxy.config.quic.client.session_file STRING ""
   :reloadable:

   Only available for :program:`traffic_quic`.
   If specified, TLS session data will be stored to the file, and will be used
   for resuming a session.

.. ts:cv:: CONFIG proxy.config.quic.no_activity_timeout_in INT 30000
   :reloadable:

   This value will be advertised as ``idle_timeout`` Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.no_activity_timeout_out INT 30000
   :reloadable:

   This value will be advertised as  ``idle_timeout`` Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.preferred_address_ipv4 STRING ""
   :reloadable:

   This value will be advertised as a part of ``preferred_address``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.preferred_address_ipv6 STRING ""
   :reloadable:

   This value will be advertised as a part of ``preferred_address``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.initial_max_data_in INT 65536
   :reloadable:

   This value will be advertised as ``initial_max_data`` Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.initial_max_data_out INT 65536
   :reloadable:

   This value will be advertised as ``initial_max_data`` Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_stream_data_bidi_local_in INT 0
   :reloadable:

   This value will be advertised as ``initial_max_stream_data_bidi_local``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_stream_data_bidi_local_out INT 4096
   :reloadable:

   This value will be advertised as ``initial_max_stream_data_bidi_local``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_stream_data_bidi_remote_in INT 4096
   :reloadable:

   This value will be advertised as ``initial_max_stream_data_bidi_remote``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_stream_data_bidi_remote_out INT 0
   :reloadable:

   This value will be advertised as ``initial_max_stream_data_bidi_remote``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_stream_data_uni_in INT 4096
   :reloadable:

   This value will be advertised as ``initial_max_stream_data_uni``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_stream_data_uni_out INT 0
   :reloadable:

   This value will be advertised as ``initial_max_stream_data_uni``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_streams_bidi_in INT 100
   :reloadable:

   This value will be advertised as ``initial_max_streams_bidi``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_streams_bidi_out INT 100
   :reloadable:

   This value will be advertised as ``initial_max_streams_bidi``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_streams_uni_in INT 100
   :reloadable:

   This value will be advertised as ``initial_max_streams_uni``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_streams_uni_out INT 100
   :reloadable:

   This value will be advertised as ``initial_max_streams_uni``
   Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.ack_delay_exponent_in INT 3
   :reloadable:

   This value will be advertised as ``ack_delay_exponent`` Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.ack_delay_exponent_out INT 3
   :reloadable:

   This value will be advertised as ``ack_delay_exponent`` Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_ack_delay_in INT 25
   :reloadable:

   This value will be advertised as ``max_ack_delay`` Transport Parameter.

.. ts:cv:: CONFIG proxy.config.quic.max_ack_delay_out INT 25
   :reloadable:

   This value will be advertised as ``max_ack_delay`` Transport Parameter.


.. ts:cv:: CONFIG proxy.config.quic.loss_detection.packet_threshold INT 3
   :reloadable:

   This is just for debugging. Do not change it from the default value unless
   you really understand what this is.

.. ts:cv:: CONFIG proxy.config.quic.loss_detection.time_threshold FLOAT 1.25
   :reloadable:

   This is just for debugging. Do not change it from the default value unless
   you really understand what this is.

.. ts:cv:: CONFIG proxy.config.quic.loss_detection.granularity INT 1
   :reloadable:

   This is just for debugging. Do not change it from the default value unless
   you really understand what this is.

.. ts:cv:: CONFIG proxy.config.quic.loss_detection.initial_rtt INT 1
   :reloadable:

   This is just for debugging. Do not change it from the default value unless
   you really understand what this is.

.. ts:cv:: CONFIG proxy.config.quic.congestion_control.max_datagram_size INT 1200
   :reloadable:

   This is just for debugging. Do not change it from the default value unless
   you really understand what this is.

.. ts:cv:: CONFIG proxy.config.quic.congestion_control.initial_window INT 12000
   :reloadable:

   This is just for debugging. Do not change it from the default value unless
   you really understand what this is.

.. ts:cv:: CONFIG proxy.config.quic.congestion_control.minimum_window INT 2400
   :reloadable:

   This is just for debugging. Do not change it from the default value unless
   you really understand what this is.

.. ts:cv:: CONFIG proxy.config.quic.congestion_control.loss_reduction_factor FLOAT 0.5
   :reloadable:

   This is just for debugging. Do not change it from the default value unless
   you really understand what this is.

.. ts:cv:: CONFIG proxy.config.quic.congestion_control.persistent_congestion_threshold INT 2
   :reloadable:

   This is just for debugging. Do not change it from the default value unless
   you really understand what this is.

UDP Configuration
=====================

.. ts:cv:: CONFIG proxy.config.udp.threads INT 0

   Specifies the number of UDP threads to run. By default 0 threads are dedicated to UDP,
   which results in effectively disabling UDP support.

.. ts:cv:: CONFIG proxy.config.udp.enable_gso INT 0

   Enables (``1``) or disables (``0``) UDP GSO. When enabled, |TS| tries to use UDP GSO,
   and disables it automatically if it causes send errors.

Plug-in Configuration
=====================

.. ts:cv:: CONFIG proxy.config.plugin.plugin_dir STRING config/plugins

   Specifies the location of |TS| plugins.

.. ts:cv:: CONFIG proxy.config.plugin.dynamic_reload_mode INT 1

   Enables (``1``) or disables (``0``) the dynamic reload feature for remap
   plugins (`remap.config`). Global plugins (`plugin.config`) do not have dynamic reload feature yet.

.. ts:cv:: CONFIG proxy.config.plugin.vc.default_buffer_index INT 8
   :reloadable:
   :overridable:

   Specifies the buffer index and thus size to use when constructing IO buffers within the PluginVC.
   Tuning this can impact performance of intercept plugins. Default is 8, which aligns with the
   default value of ts:cv:`CONFIG proxy.config.http.default_buffer_size`.

.. ts:cv:: CONFIG proxy.config.plugin.vc.default_buffer_water_mark INT 0
   :reloadable:
   :overridable:

   Specifies the buffer water mark size in bytes used to control the flow of data through IO buffers
   within the PluginVC. Default is zero to preserve existing PluginVC water marking behavior.

SOCKS Processor
===============

.. ts:cv::  CONFIG proxy.config.socks.socks_needed INT 0

   Enables (``1``) or disables (``0``) the SOCKS processor

.. ts:cv::  CONFIG proxy.config.socks.socks_version INT 4

   Specifies the SOCKS version (``4``) or (``5``)

.. ts:cv::  CONFIG proxy.config.socks.socks_config_file STRING socks.config
   :deprecated:

   The socks.config file allows you to specify ranges of IP addresses
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

.. ts:cv::  CONFIG proxy.config.socks.default_servers STRING ""

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
   1)    and forwards all requests directly to the SOCKS server.

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
   On FreeBSD, ``accf_data`` module needs to be loaded.

.. ts:cv:: CONFIG proxy.config.net.listen_backlog INT -1
   :reloadable:

   This directive sets the maximum number of pending connections.
   If it is set to -1, |TS| will automatically set this
   to a platform-specific maximum.

.. ts:cv:: CONFIG  proxy.config.net.tcp_congestion_control_in STRING ""

   This directive will override the congestion control algorithm for incoming
   connections (accept sockets). On Linux, the allowed values are typically
   specified in a space separated list in /proc/sys/net/ipv4/tcp_allowed_congestion_control

.. ts:cv:: CONFIG  proxy.config.net.tcp_congestion_control_out STRING ""

   This directive will override the congestion control algorithm for outgoing
   connections (connect sockets). On Linux, the allowed values are typically
   specified in a space separated list in /proc/sys/net/ipv4/tcp_allowed_congestion_control

.. ts:cv:: CONFIG proxy.config.net.sock_send_buffer_size_in INT 0

   Sets the send buffer size for connections from the client to |TS|.

.. ts:cv:: CONFIG proxy.config.net.sock_recv_buffer_size_in INT 0

   Sets the receive buffer size for connections from the client to |TS|.

.. ts:cv:: CONFIG proxy.config.net.sock_option_flag_in INT 0x1

   Turns different options "on" for the socket handling client connections:::

        TCP_NODELAY  (1)
        SO_KEEPALIVE (2)
        SO_LINGER (4) - with a timeout of 0 seconds
        TCP_FASTOPEN (8)
        PACKET_MARK (16)
        PACKET_TOS (32)
        TCP_NOTSENT_LOWAT (64)

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
        PACKET_MARK (16)
        PACKET_TOS (32)
        TCP_NOTSENT_LOWAT (64)

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

.. ts:cv:: CONFIG proxy.config.net.sock_notsent_lowat INT 16384
   :overridable:

   Set socket option TCP_NOTSENT_LOWAT to specified value for a connection

.. ts:cv:: CONFIG proxy.config.net.poll_timeout INT 10

   Same as the command line option ``--poll_timeout``, or ``-t``, which
   specifies the timeout used for the polling mechanism used. This timeout is
   always in milliseconds (ms). This is the timeout to ``epoll_wait()`` on
   Linux platforms, and to ``kevent()`` on BSD type OSs. The default value is
   ``10`` on all platforms or 30 on Solaris.

   Changing this configuration can reduce CPU usage on an idle system, since
   periodic tasks gets processed at these intervals. On busy servers, this
   overhead is diminished, since polled events triggers more frequently.
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
   before returning the objects to the global pool. If set to ``0``, there is no limit enforced.

.. ts:cv:: CONFIG proxy.config.allocator.thread_freelist_low_watermark INT 32

   Sets the minimum number of items a ProxyAllocator (per-thread) will guarantee to be
   holding at any one time.

.. ts:cv:: CONFIG proxy.config.allocator.hugepages INT 0

   Enable (1) the use of huge pages on supported platforms. (Currently only Linux)

   You must also enable hugepages at the OS level. In modern Linux kernels,
   this can be done by setting ``/proc/sys/vm/nr_overcommit_hugepages`` to a
   sufficiently large value. It is reasonable to use (system
   memory/hugepage size) because these pages are only created on demand.

   For more information on the implications of enabling huge pages, see
   `Wikipedia <http://en.wikipedia.org/wiki/Page_%28computer_memory%29#Page_size_trade-off>_`.

.. ts:cv:: CONFIG proxy.config.dump_mem_info_frequency INT 0
   :reloadable:

   Enable <value>. When enabled makes Traffic Server dump IO Buffer memory information
   to ``traffic.out`` at ``<value>`` (intervals are in seconds). A zero value implies it is
   disabled

.. ts:cv:: CONFIG proxy.config.res_track_memory INT 0

   When enabled makes Traffic Server track memory usage (allocations and releases). This
   information is dumped  to ``traffic.out`` when the user sends a SIGUSR1 signal or
   periodically when :ts:cv:`proxy.config.dump_mem_info_frequency` is enabled.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Memory tracking Disabled
   ``1`` Tracks IO Buffer Memory allocations and releases
   ``2`` Tracks IO Buffer Memory and OpenSSL Memory allocations and releases
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.allocator.dontdump_iobuffers INT 1

   Enable (1) the exclusion of IO buffers from core files when ATS crashes on supported
   platforms.  (Currently only Linux).  IO buffers are allocated with the MADV_DONTDUMP
   with madvise() on Linux platforms that support MADV_DONTDUMP.  Enabled by default.

.. ts:cv:: CONFIG proxy.config.allocator.iobuf_chunk_sizes STRING

   This configures the chunk sizes of each of the IO buffer allocators.  The chunk size is the number
   of buffers allocated in a batch when the allocator's freelist is exhausted.  This must be specified as a
   space separated list of up to 15 numbers.  If not specified or if any value specified is 0, the default
   value will be used.

   The list of numbers will specify the chunk sizes in the following order:

   ``128 256 512 1k 2k 4k 8k 16k 32k 64k 128k 256k 512k 1M 2M``

   The defaults for each allocator is:

   ``128 128 128 128 128 128 32 32 32 32 32 32 32 32 32``

   Even though this is specified, the actual chunk size might be modified based on the system's page size (or hugepage
   size if enabled).

   You might want to adjust these values to reduce the overall number of allocations that ATS needs to make based
   on your configured RAM cache size.  On a running system, you can send SIGUSR1 to the ATS process to have it
   log the allocator statistics and see how many of each buffer size have been allocated.

.. ts:cv:: CONFIG proxy.config.allocator.iobuf_use_hugepages INT 0

   This setting controls whether huge pages allocations are used to allocate io buffers.  If enabled, and hugepages are
   not available, this will fall back to normal size pages. Using hugepages for iobuffer can sometimes improve performance
   by utilizing more of the TLB and reducing TLB misses.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` IO buffer allocation uses normal pages sizes
   ``1`` IO buffer allocation uses huge pages
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.cache.dir.enable_hugepages INT 0

   This setting controls whether huge pages allocations are used to allocate memory for cache volume dir entries.

   ===== ======================================================================
   Value Description
   ===== ======================================================================
   ``0`` Use normal pages sizes
   ``1`` Use huge pages
   ===== ======================================================================

.. ts:cv:: CONFIG proxy.config.ssl.misc.io.max_buffer_index INT 8

   Configures the max IOBuffer Block index used for various SSL Operations
   such as Handshake or Protocol Probe. Default value is 8 which maps to a 32K buffer

.. ts:cv:: CONFIG proxy.config.hostdb.io.max_buffer_index INT 8

   Configures the max IOBuffer Block index used for storing HostDB records.
   Default value is 8 which maps to a 32K buffer

.. ts:cv:: CONFIG proxy.config.payload.io.max_buffer_index INT 8

   Configures the max IOBuffer Block index used for storing request payload buffer
   for a POST request. Default value is 8 which maps to a 32K buffer

.. ts:cv:: CONFIG proxy.config.msg.io.max_buffer_index INT 8

   Configures the max IOBuffer Block index used for storing miscellaneous transactional
   buffers such as error response body. Default value is 8 which maps to a 32K buffer

.. ts:cv:: CONFIG proxy.config.log.io.max_buffer_index INT 8

   Configures the max IOBuffer Block index used for storing an access log entry.
   Default value is 8 which maps to a 32K buffer

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
