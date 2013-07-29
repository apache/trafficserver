records.config
**************

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

The :file:`records.config` file is a list of configurable variables used by
the Traffic Server software. Many of the variables in the
:file:`records.config` file are set automatically when you set configuration
options in Traffic Line or Traffic Shell. After you modify the
:file:`records.config` file, navigate to the Traffic Server\ ``bin``
directory and run the command ``traffic_line -x`` to apply the changes.
When you apply changes to one node in a cluster, Traffic Server
automatically applies the changes to all other nodes in the cluster.

Format
======

Each variable has the following format::

   SCOPE variable_name DATATYPE variable_value

where

``SCOPE`` is related to clustering and is either ``CONFIG`` (all members of the cluster) or ``LOCAL`` (only the local machine)

``DATATYPE`` is one of ``INT`` (integer), ``STRING`` (string), ``FLOAT`` (floating point).

A variable marked as ``Deprecated`` is still functional but should be avoided as it may be removed in a future release without warning.

A variable marked as ``Reloadable`` can be updated via the command::

   traffic_line -x

Examples
========

In the following example, the variable `proxy.config.proxy_name`_ is
a ``STRING`` datatype with the value ``my_server``. This means that the
name of the Traffic Server proxy is ``my_server``. ::

   CONFIG proxy.config.proxy_name STRING my_server

If the server name should be ``that_server`` the line would be ::

   CONFIG proxy.config.proxy_name STRING that_server

In the following example, the variable `proxy.config.arm.enabled`_ is
a yes/no flag. A value of ``0`` (zero) disables the option; a value of
``1`` enables the option. ::

   CONFIG proxy.config.arm.enabled INT 0

In the following example, the variable sets the cluster startup timeout
to 10 seconds. ::

   CONFIG proxy.config.cluster.startup_timeout INT 10

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
:file:`records.config` or applied with :program:`traffic_line`.

For example, we could override the `proxy.config.product_company`_ variable
like this::

   $ PROXY_CONFIG_PRODUCT_COMPANY=example traffic_cop &
   $ traffic_line -r proxy.config.product_company

Configuration Variables
=======================

The following list describes the configuration variables available in
the :file:`records.config` file.

System Variables
----------------

.. ts:confvar:: CONFIG proxy.config.product_company STRING Apache Software Foundation

   The name of the organization developing Traffic Server.

.. ts:confvar:: CONFIG proxy.config.product_vendor STRING Apache

   The name of the vendor providing Traffic Server.

.. ts:confvar:: CONFIG proxy.config.product_name STRING Traffic Server

   The name of the product.

.. ts:confvar:: CONFIG proxy.config.proxy_name STRING ``build_machine``
   :reloadable:

   The name of the Traffic Server node.

.. ts:confvar:: CONFIG proxy.config.bin_path STRING bin

   The location of the Traffic Server ``bin`` directory.


.. ts:confvar:: CONFIG proxy.config.proxy_binary STRING traffic_server

   The name of the executable that runs the ``traffic_server`` process.


.. ts:confvar:: CONFIG proxy.config.proxy_binary_opts STRING -M

   The command-line options for starting Traffic Server.


.. ts:confvar:: CONFIG proxy.config.manager_binary STRING traffic_manager

   The name of the executable that runs the ``traffic_manager`` process.


.. ts:confvar:: CONFIG proxy.config.cli_binary STRING traffic_line

   The name of the executable that runs the command-line interface `traffic_line`_.

.. ts:confvar:: CONFIG proxy.config.watch_script STRING traffic_cop

   The name of the executable that runs the ``traffic_cop`` process.

.. ts:confvar:: CONFIG proxy.config.env_prep STRING

   The script executed before the ``traffic_manager`` process spawns
   the ``traffic_server`` process.

.. ts:confvar:: CONFIG proxy.config.config_dir STRING config

   The directory that contains Traffic Server configuration files.

.. ts:confvar:: CONFIG proxy.config.temp_dir STRING /tmp

   The directory used for Traffic Server temporary files.

.. ts:confvar:: CONFIG proxy.config.alarm_email STRING
   :reloadable:

   The email address to which Traffic Server sends alarm messages.

During a custom Traffic Server installation, you can specify the email address;
otherwise, Traffic Server uses the Traffic Server user account name as the default value for this variable.

.. ts:confvar:: CONFIG proxy.config.syslog_facility STRING LOG_DAEMON

   The facility used to record system log files. Refer to
   `Understanding Traffic Server Log Files <../working-log-files#UnderstandingTrafficServerLogFiles>`_.

.. ts:confvar:: CONFIG proxy.config.cop.core_signal INT 0

   The signal sent to ``traffic_cop``'s managed processes to stop them.

A value of ``0`` means no signal will be sent.


.. ts:confvar:: CONFIG proxy.config.cop.linux_min_swapfree_kb INT 10240

   The minimum amount of free swap space allowed before Traffic Server stops the ``traffic_server`` and ``traffic_manager`` processes to
   prevent the system from hanging. This configuration variable applies if swap is enabled in Linux 2.2 only.

.. ts:confvar:: CONFIG proxy.config.output.logfile  STRING traffic.out

   The name and location of the file that contains warnings, status messages, and error messages produced by the Traffic Server
   processes. If no path is specified, then Traffic Server creates the file in its logging directory.

.. ts:confvar:: CONFIG proxy.config.snapshot_dir STRING snapshots

   The directory in which Traffic Server stores configuration snapshots on the local system. Unless you specify an absolute path, this
   directory is located in the Traffic Server ``config`` directory.

.. ts:confvar:: CONFIG proxy.config.exec_thread.autoconfig INT 1

   When enabled (the default, ``1``), Traffic Server scales threads according to the available CPU cores. See the config option below.

.. ts:confvar:: CONFIG proxy.config.exec_thread.autoconfig.scale FLOAT 1.5

   Factor by which Traffic Server scales the number of threads. The ultiplier is usually the number of available CPU cores. By default
   this is scaling factor is ``1.5``.

.. ts:confvar:: CONFIG proxy.config.exec_thread.limit INT 2

   *XXX* What does this do?

.. ts:confvar:: CONFIG proxy.config.accept_threads INT 0

   When enabled (``1``), runs a separate thread for accept processing. If disabled (``0``), then only 1 thread can be created.

.. ts:confvar:: CONFIG proxy.config.thread.default.stacksize  INT 1096908

   The new default thread stack size, for all threads. The original default is set at 1 MB.

Network
=======

.. ts:confvar:: LOCAL proxy.local.incoming_ip_to_bind STRING 0.0.0.0 ::

   Controls the global default IP addresses to which to bind proxy server ports. The value is a space separated list of IP addresses, one per supported IP address family (currently IPv4 and IPv6).

Unless explicitly specified in `proxy.config.http.server_ports`_ the server port will be bound to one of these addresses, selected by IP address family. The built in default is any address. This is used if no address for a family is specified. This setting is useful if most or all server ports should be bound to the same address.

.. note:: This is ignored for inbound transparent server ports because they must be able to accept connections on arbitrary IP addresses.

.. topic:: Example

   Set the global default for IPv4 to ``192.168.101.18`` and leave the global default for IPv6 as any address.::

      LOCAL proxy.local.incoming_ip_to_bind STRING 192.168.101.18

.. topic:: Example

   Set the global default for IPv4 to ``191.68.101.18`` and the global default for IPv6 to ``fc07:192:168:101::17``.::

      LOCAL proxy.local.incoming_ip_to_bind STRING 192.168.101.18 fc07:192:168:101::17

.. ts:confvar:: LOCAL proxy.local.outgoing_ip_to_bind STRING 0.0.0.0 ::

   This controls the global default for the local IP address for outbound connections to origin servers. The value is a list of space separated IP addresses, one per supported IP address family (currently IPv4 and IPv6).

   Unless explicitly specified in `proxy.config.http.server_ports`_ one of these addresses, selected by IP address family, will be used as the local address for outbound connections. This setting is useful if most or all of the server ports should use the same outbound IP addresses.

.. note:: This is ignore for outbound transparent ports as the local outbound address will be the same as the client local address.

.. topic:: Example

   Set the default local outbound IP address for IPv4 connectionsn to ``192.168.101.18``.::

      LOCAL proxy.local.outgoing_ip_to_bind STRING 192.168.101.18

.. topic:: Example

   Set the default local outbound IP address to ``192.168.101.17`` for IPv4 and ``fc07:192:168:101::17`` for IPv6.::

      LOCAL proxy.local.outgoing_ip_to_bind STRING 192.168.101.17 fc07:192:168:101::17

Cluster
=======

.. ts:confvar:: LOCAL proxy.local.cluster.type INT 3

   Sets the clustering mode:

===== ====================
Value Effect
===== ====================
1     full-clustering mode
2     management-only mode
3     no clustering
===== ====================

.. ts:confvar:: CONFIG proxy.config.cluster.rsport INT 8088

   The reliable service port. The reliable service port is used to send configuration information between the nodes in a cluster. All nodes
   in a cluster must use the same reliable service port. 

.. ts:confvar:: CONFIG proxy.config.cluster.threads INT 1

   The number of threads for cluster communication. On heavy cluster, the number should be adjusted. It is recommend that take the thread
   CPU usage as a reference when adjusting.

Local Manager
=============

.. ts:confvar:: CONFIG proxy.config.lm.sem_id INT 11452

   The semaphore ID for the local manager.

.. ts:confvar:: CONFIG proxy.config.admin.autoconf_port INT 8083

   The autoconfiguration port.

.. ts:confvar:: CONFIG proxy.config.admin.number_config_bak INT 3

   The maximum number of copies of rolled configuration files to keep.

.. ts:confvar:: CONFIG proxy.config.admin.user_id STRING nobody

   Option used to specify who to run the ``traffic_server`` process as; also used to specify ownership of config and log files.

   The nonprivileged user account designated to Traffic Server.

   As of version 2.1.1 if the user_id is prefixed with pound character
   (#) the remaining of the string is considered to be ``numeric user
   identifier <http://en.wikipedia.org/wiki/User_identifier>``_. If the
   value is set to '#-1' Traffic Server will not change the user during
   startup.

   Setting ``user_id`` to ``root`` or ``#0`` is now forbidden to
   increase security. Trying to do so, will cause the
   ``traffic_server`` fatal failure. However there are two ways to
   bypass that restriction: Specify ``-DBIG_SECURITY_HOLE`` in
   ``CXXFLAGS`` during compilation Set the ``user_id=#-1`` and start
   trafficserver as root.

Process Manager
===============

.. ts:confvar:: CONFIOG proxy.config.process_manager.mgmt_port  INT 8084

   The port used for internal communication between the ``traffic_manager`` and ``traffic_server`` processes.

Alarm Configuration
===================

.. ts:confvar:: CONFIG proxy.config.alarm.bin STRING example_alarm_bin.sh

   Name of the script file that can execute certain actions when an alarm is signaled. The default file is a sample script named
   ``example_alarm_bin.sh`` located in the ``bin`` directory. You must dit the script to suit your needs.

.. ts:confvar:: CONFIG proxy.config.alarm.abs_path STRING NULL

   The full path to the script file that sends email to alert someone bout Traffic Server problems.

HTTP Engine
===========

.. ts:confvar:: CONFIG proxy.config.http.server_ports STRING 8080

   Ports used for proxying HTTP traffic.

This is a list, separated by space or comma, of :index:`port descriptors`. Each descriptor is a sequence of keywords and values separated by colons. Not all keywords have values, those that do are specifically noted. Keywords with values can have an optional '=' character separating the keyword and value. The case of keywords is ignored. The order of keywords is irrelevant but unspecified results may occur if incompatible options are used (noted below). Options without values are idempotent. Options with values use the last (right most) value specified, except for ``ip-out`` as detailed later.

Quick reference chart.

=========== =============== ========================================
Name        Note            Definition 
=========== =============== ========================================
*number*    **Required**    The local port.
ipv4        **Default**     Bind to IPv4 address family.
ipv6                        Bind to IPv6 address family.
tr-in                       Inbound transparent.
tr-out                      Outbound transparent.
tr-full                     Fully transparent (inbound and outbound)
tr-pass                     Pass through enabled.
ssl                         SSL terminated.
ip-in       **Value**       Local inbound IP address.
ip-out      **Value**       Local outbound IP address.
ip-resolve  **Value**       IP address resolution style.
blind                       Blind (``CONNECT``) port.
compress    **N/I**         Compressed. Not implemented.
=========== =============== ========================================

*number*
   Local IP port to bind. This is the port to which ATS clients will connect.

ipv4
   Use IPv4. This is the default and is included primarily for completeness. This forced if the ``ip-in`` option is used with an IPv4 address.

ipv6
   Use IPv6. This is forced if the ``ip-in`` option is used with an IPv6 address.

tr-in
   Inbound transparent. The proxy port will accept connections to any IP address on the port. To have IPv6 inbound transparent you must use this and the ``ipv6`` option. This overrides `proxy.local.incoming_ip_to_bind`_.

   Not compatible with: ``ip-in``, ``ssl``, ``blind``

tr-out
   Outbound transparent. If ATS connects to an origin server for a transaction on this port, it will use the client's address as its local address. This overrides `proxy.local.outgoing_ip_to_bind`_.

   Not compatible with: ``ip-out``, ``ssl``

tr-full
   Fully transparent. This is a convenience option and is identical to specifying both ``tr-in`` and ``tr-out``.

   Not compatible with: Any option not compatible with ``tr-in`` or ``tr-out``.

tr-pass
   Transparent pass through. This option is useful only for inbound transparent proxy ports. If the parsing of the expected HTTP header fails, then the transaction is switched to a blind tunnel instead of generating an error response to the client. It effectively enables `proxy.config.http.use_client_target_addr`_ for the transaction as there is no other place to obtain the origin server address.

ip-in
   Set the local IP address for the port. This is the address to which clients will connect. This forces the IP address family for the port. The ``ipv4`` or ``ipv6`` can be used but it is optional and is an error for it to disagree with the IP address family of this value. An IPv6 address **must** be enclosed in square brackets. If this is omitted `proxy.local.incoming_ip_to_bind`_ is used.

   Not compatible with: ``tr-in``.

ip-out
   Set the local IP address for outbound connections. This is the address used by ATS locally when it connects to an origin server for transactions on this port. If this is omitted `proxy.local.outgoing_ip_to_bind`_ is used.

   This option can used multiple times, once for each IP address family. The address used is selected by the IP address family of the origin server address.

   Not compatible with: ``tr-out``.

ip-resolve
   Set the IP address resolution style for the origin server for transactions on this proxy port.

ssl
   Require SSL termination for inbound connections. SSL must be configured for this option to provide a functional server port.

   Not compatible with: ``tr-in``, ``tr-out``, ``blind``.

blind
   Accept only ``CONNECT`` transactions on this port.

   Not compatible with: ``tr-in``, ``ssl``.

compress
   Compress the connection. Retained only by inertia, should be considered "not implemented".

.. topic:: Example

   Listen on port 80 on any address for IPv4 and IPv6.::

      80 80:ipv6

.. topic:: Example

   Listen transparently on any IPv4 address on port 8080, and
   transparently on port 8080 on local address ``fc01:10:10:1::1``
   (which implies ``ipv6``).::

      IPv4:tr-FULL:8080 TR-full:IP-in=[fc02:10:10:1::1]:8080

.. topic:: Example

   Listen on port 8080 for IPv6, fully transparent. Set up an SSL port on 443. These ports will use the IP address from `proxy.local.incoming_ip_to_bind`_.  Listen on IP address ``192.168.17.1``, port 80, IPv4, and connect to origin servers using the local address ``10.10.10.1`` for IPv4 and ``fc01:10:10:1::1`` for IPv6.::

      8080:ipv6:tr-full 443:ssl ip-in=192.168.17.1:80:ip-out=[fc01:10:10:1::1]:ip-out=10.10.10.1

.. ts:confvar:: CONFIG proxy.config.http.connect_ports STRING 443 563

   The range of origin server ports that can be used for tunneling via ``CONNECT``.

Traffic Server allows tunnels only to the specified ports.
Supports both wildcards ('\*') and ranges ("0-1023").

.. note:: These are the ports on the *origin server*, not `server ports <#proxy-config-http-server-ports>`_.

.. ts:confvar:: CONFIG proxy.config.http.insert_request_via_str INT 1
   :reloadable:

   Set how the ``Via`` field is handled on a request to the origin server.   

===== ============================================
Value Effect
===== ============================================
0     no extra information is added to the string.
1     all extra information is added.
2     some extra information is added.
===== ============================================

.. note:: the ``Via`` header string interpretation can be `decoded here. </tools/via>`_

.. ts:confvar:: CONFIG proxy.config.http.insert_response_via_str INT 1
   :reloadable:

   Set how the ``Via`` field is handled on the response to the client.

===== ======================   
Value Effect
===== ======================   
0     no extra information is added to the string.
1     all extra information is added.
2     some extra information is added.
===== ======================   

.. ts:confvar:: CONFIG proxy.config.http.response_server_enabled INT 1
   :reloadable:

   You can specify one of the following:

   -  ``0`` no Server: header is added to the response.
   -  ``1`` the Server: header is added (see string below).
   -  ``2`` the Server: header is added only if the response from rigin does not have one already.

.. ts:confvar:: CONFIG proxy.config.http.insert_age_in_response INT 1
   :reloadable:

   This option specifies whether Traffic Server should insert an ``Age`` header in the response. The Age field value is the cache's
   estimate of the amount of time since the response was generated or revalidated by the origin server.

   -  ``0`` no ``Age`` header is added
   -  ``1`` the ``Age`` header is added

.. ts:confvar:: CONFIG proxy.config.http.response_server_str STRING ATS/
   :reloadable:

   The Server: string that ATS will insert in a response header (if requested, see above). Note that the current version number is
   always appended to this string.

.. ts:confvar:: CONFIG proxy.config.http.enable_url_expandomatic INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) ``.com`` domain expansion. This configures the Traffic Server to resolve unqualified hostnames by
   prepending with ``www.`` and appending with ``.com`` before redirecting to the expanded address. For example: if a client makes
   a request to ``host``, then Traffic Server redirects the request to ``www.host.com``.

.. ts:confvar:: CONFIG proxy.config.http.chunking_enabled INT 1
   :reloadable:

   Specifies whether Traffic Sever can generate a chunked response:

   -  ``0`` Never
   -  ``1`` Always
   -  ``2`` Generate a chunked response if the server has returned HTTP/1.1 before
   -  ``3`` = Generate a chunked response if the client request is HTTP/1.1 and the origin server has returned HTTP/1.1 before

   **Note:** If HTTP/1.1 is used, then Traffic Server can use
   keep-alive connections with pipelining to origin servers. If
   HTTP/0.9 is used, then Traffic Server does not use ``keep-alive``
   connections to origin servers. If HTTP/1.0 is used, then Traffic
   Server can use ``keep-alive`` connections without pipelining to
   origin servers.

.. ts:confvar:: CONFIG proxy.config.http.share_server_sessions INT 1

   Enables (``1``) or disables (``0``) the reuse of server sessions.

.. ts:confvar:: CONFIG proxy.config.http.record_heartbeat INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) ``traffic_cop`` heartbeat ogging.

.. ts:confvar:: proxy.config.http.use_client_target_addr  INT 0

   For fully transparent ports use the same origin server address as the client.

This option causes Traffic Server to avoid where possible doing DNS
lookups in forward transparent proxy mode. The option is only
effective if the following three conditions are true -

*  Traffic Server is in forward proxy mode.
*  The proxy port is inbound transparent.
*  The target URL has not been modified by either remapping or a plugin.

If any of these conditions are not true, then normal DNS processing
is done for the connection.

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

Parent Proxy Configuration
==========================

.. ts:confvar:: CONFIG proxy.config.http.parent_proxy_routing_enable INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) the parent caching option. Refer to Hierarchical Caching <../hierachical-caching>_.

.. ts:confvar:: CONFIG proxy.config.http.parent_proxy.retry_time INT 300
   :reloadable:

   The amount of time allowed between connection retries to a parent cache that is unavailable.

.. ts:confvar:: CONFIG proxy.config.http.parent_proxy.fail_threshold INT 10
   :reloadable:

   The number of times the connection to the parent cache can fail before Traffic Server considers the parent unavailable.

.. ts:confvar:: CONFIG proxy.config.http.parent_proxy.total_connect_attempts INT 4
   :reloadable:

   The total number of connection attempts allowed to a parent cache before Traffic Server bypasses the parent or fails the request
   (depending on the ``go_direct`` option in the ``bypass.config`` file).

.. ts:confvar:: CONFIG proxy.config.http.parent_proxy.per_parent_connect_attempts INT 2
   :reloadable:

   The total number of connection attempts allowed per parent, if multiple parents are used.

.. ts:confvar:: CONFIG proxy.config.http.parent_proxy.connect_attempts_timeout INT 30
   :reloadable:

   The timeout value (in seconds) for parent cache connection attempts.

.. ts:confvar:: CONFIG proxy.config.http.forward.proxy_auth_to_parent INT 0
   :reloadable:

   Configures Traffic Server to send proxy authentication headers on to the parent cache.

HTTP Connection Timeouts
========================

.. ts:confvar:: CONFIG proxy.config.http.keep_alive_no_activity_timeout_in INT 10
   :reloadable:

   Specifies how long Traffic Server keeps connections to clients open for a subsequent request after a transaction ends.

.. ts:confvar:: CONFIG proxy.config.http.keep_alive_no_activity_timeout_out INT 10
   :reloadable:

   Specifies how long Traffic Server keeps connections to origin servers open for a subsequent transfer of data after a transaction ends.

.. ts:confvar:: CONFIG proxy.config.http.transaction_no_activity_timeout_in INT 120
   :reloadable:

   Specifies how long Traffic Server keeps connections to clients open if a transaction stalls.

.. ts:confvar:: CONFIG proxy.config.http.transaction_no_activity_timeout_out INT 120
   :reloadable:

   Specifies how long Traffic Server keeps connections to origin servers open if the transaction stalls.

.. ts:confvar:: CONFIG proxy.config.http.transaction_active_timeout_in INT 0
   :reloadable:

   The maximum amount of time Traffic Server can remain connected to a client. If the transfer to the client is not complete before this
   timeout expires, then Traffic Server closes the connection.

The default value of ``0`` specifies that there is no timeout.

.. ts:confvar:: CONFIG proxy.config.http.transaction_active_timeout_out INT 0
   :reloadable:

   The maximum amount of time Traffic Server waits for fulfillment of a connection request to an origin server. If Traffic Server does not
   complete the transfer to the origin server before this timeout expires, then Traffic Server terminates the connection request.

The default value of ``0`` specifies that there is no timeout.

.. ts:confvar:: CONFIG proxy.config.http.accept_no_activity_timeout INT 120
   :reloadable:

   The timeout interval in seconds before Traffic Server closes a connection that has no activity.

.. ts:confvar:: CONFIG proxy.config.http.background_fill_active_timeout INT 60
   :reloadable:

   Specifies how long Traffic Server continues a background fill before giving up and dropping the origin server connection.

.. ts:confvar:: CONFIG proxy.config.http.background_fill_completed_threshold FLOAT 0.50000
   :reloadable:

   The proportion of total document size already transferred when a client aborts at which the proxy continues fetching the document
   from the origin server to get it into the cache (a **background fill**).

Origin Server Connect Attempts
==============================

.. ts:confvar:: CONFIG proxy.config.http.connect_attempts_max_retries INT 6
   :reloadable:

   The maximum number of connection retries Traffic Server can make when the origin server is not responding.

.. ts:confvar:: CONFIG proxy.config.http.connect_attempts_max_retries_dead_server INT 2
   :reloadable:

   The maximum number of connection retries Traffic Server can make when the origin server is unavailable.

.. ts:confvar:: CONFIG proxy.config.http.server_max_connections INT 0
   :reloadable:

   Limits the number of socket connections across all origin servers to the value specified. To disable, set to zero (``0``).

.. ts:confvar:: CONFIG proxy.config.http.origin_max_connections INT 0
   :reloadable:

   Limits the number of socket connections per origin server to the value specified. To enable, set to one (``1``).

.. ts:confvar:: CONFIG proxy.config.http.origin_min_keep_alive_connections INT 0
   :reloadable:

   As connection to an origin server are opened, keep at least 'n' number of connections open to that origin, even if the connection
   isn't used for a long time period. Useful when the origin supports keep-alive, removing the time needed to set up a new connection from
   the next request at the expense of added (inactive) connections. To enable, set to one (``1``).

.. ts:confvar:: CONFIG proxy.config.http.connect_attempts_rr_retries INT 2
   :reloadable:

   The maximum number of failed connection attempts allowed before a round-robin entry is marked as 'down' if a server has round-robin DNS entries.

.. ts:confvar:: CONFIG proxy.config.http.connect_attempts_timeout INT 30
   :reloadable:

   The timeout value (in seconds) for an origin server connection.

.. ts:confvar:: CONFIG proxy.config.http.post_connect_attempts_timeout INT 1800
   :reloadable:

   The timeout value (in seconds) for an origin server connection when the client request is a ``POST`` or ``PUT`` request.

.. ts:confvar:: CONFIG proxy.config.http.down_server.cache_time INT 900
   :reloadable:

   Specifies how long (in seconds) Traffic Server remembers that an origin server was unreachable.

.. ts:confvar:: CONFIG proxy.config.http.down_server.abort_threshold INT 10
   :reloadable:

   The number of seconds before Traffic Server marks an origin server as unavailable after a client abandons a request because the origin
   server was too slow in sending the response header.

Congestion Control
==================

.. ts:confvar:: CONFIG proxy.config.http.congestion_control.enabled INT 0

   Enables (``1``) or disables (``0``) the Congestion Control option, which configures Traffic Server to stop forwarding HTTP requests to
   origin servers when they become congested. Traffic Server sends the client a message to retry the congested origin server later. Refer
   to `Using Congestion Control <../http-proxy-caching#UsingCongestionControl>`_.

Negative Response Caching
=========================

.. ts:confvar:: CONFIG proxy.config.http.negative_caching_enabled INT 0
   :reloadable:

   When enabled (``1``), Traffic Server caches negative responses (such as ``404 Not Found``) when a requested page does not exist. The next
   time a client requests the same page, Traffic Server serves the negative response directly from cache.

   **Note**: ``Cache-Control`` directives from the server forbidding ache are ignored for the following HTTP response codes, regardless
   of the value specified for the `proxy.config.http.negative_caching_enabled`_ variable. The
   following negative responses are cached by Traffic Server:::

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

Proxy User Variables
====================

.. ts:confvar:: CONFIG proxy.config.http.anonymize_remove_from INT 0
   :reloadable:

   When enabled (``1``), Traffic Server removes the ``From`` header to protect the privacy of your users.

.. ts:confvar:: CONFIG proxy.config.http.anonymize_remove_referer INT 0
   :reloadable:

   When enabled (``1``), Traffic Server removes the ``Referrer`` header to protect the privacy of your site and users.

.. ts:confvar:: CONFIG proxy.config.http.anonymize_remove_user_agent INT 0
   :reloadable:

   When enabled (``1``), Traffic Server removes the ``User-agent`` header to protect the privacy of your site and users.

.. ts:confvar:: CONFIG proxy.config.http.anonymize_remove_cookie INT 0
   :reloadable:

   When enabled (``1``), Traffic Server removes the ``Cookie`` header to protect the privacy of your site and users.

.. ts:confvar:: CONFIG proxy.config.http.anonymize_remove_client_ip INT 0
   :reloadable:

   When enabled (``1``), Traffic Server removes ``Client-IP`` headers for more privacy.

.. ts:confvar:: CONFIG proxy.config.http.anonymize_insert_client_ip INT 1
   :reloadable:

   When enabled (``1``), Traffic Server inserts ``Client-IP`` headers to retain the client IP address.

.. ts:confvar:: CONFIG proxy.config.http.append_xforwards_header INT 0

   When enabled (``1``), Traffic Server appends ``X-Forwards`` headers to outgoing requests.

.. ts:confvar:: CONFIG proxy.config.http.anonymize_other_header_list STRING NULL
   :reloadable:

   The headers Traffic Server should remove from outgoing requests.

.. ts:confvar:: CONFIG proxy.config.http.insert_squid_x_forwarded_for INT 0
   :reloadable:

   When enabled (``1``), Traffic Server adds the client IP address to the ``X-Forwarded-For`` header.

.. ts:confvar:: CONFIG proxy.config.http.normalize_ae_gzip INT 0
   :reloadable:

   Enable (``1``) to normalize all ``Accept-Encoding:`` headers to one of the following:

   -  ``Accept-Encoding: gzip`` (if the header has ``gzip`` or ``x-gzip`` with any ``q``) **OR**
   -  *blank* (for any header that does not include ``gzip``)

   This is useful for minimizing cached alternates of documents (e.g. ``gzip, deflate`` vs. ``deflate, gzip``). Enabling this option is
   recommended if your origin servers use no encodings other than ``gzip``.

Security
========

.. ts:confvar:: CONFIG proxy.config.http.push_method_enabled INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) the HTTP ``PUSH`` option, which allows you to deliver content directly to the cache without a user
   request.

   **Important:** If you enable this option, then you must also specify
   a filtering rule in the ip_allow.config file to allow only certain
   machines to push content into the cache.

Cache Control
=============

.. ts:confvar:: CONFIG proxy.config.cache.enable_read_while_writer INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) ability to a read cached object while the another connection is completing the write to cache for
   the same object.

.. ts:confvar:: CONFIG proxy.config.cache.force_sector_size INT 512
   :reloadable:

   Forces the use of a specific hardware sector size (512 - 8192 bytes).

.. ts:confvar:: CONFIG proxy.config.http.cache.http INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) caching of HTTP requests.

.. ts:confvar:: CONFIG proxy.config.http.cache.ignore_client_no_cache INT 0
   :reloadable:

   When enabled (``1``), Traffic Server ignores client requests to bypass the cache.

.. ts:confvar:: CONFIG proxy.config.http.cache.ims_on_client_no_cache INT 0
   :reloadable:

   When enabled (``1``), Traffic Server issues a conditional request to the origin server if an incoming request has a ``No-Cache`` header.

.. ts:confvar:: CONFIG proxy.config.http.cache.ignore_server_no_cache INT 0
   :reloadable:

   When enabled (``1``), Traffic Server ignores origin server requests to bypass the cache.

.. ts:confvar:: CONFIG proxy.config.http.cache.cache_responses_to_cookies INT 3
   :reloadable:

   Specifies how cookies are cached:

   -  ``0`` = do not cache any responses to cookies
   -  ``1`` = cache for any content-type
   -  ``2`` = cache only for image types
   -  ``3`` = cache for all but text content-types

.. ts:confvar:: CONFIG proxy.config.http.cache.ignore_authentication INT 0

   When enabled (``1``), Traffic Server ignores ``WWW-Authentication`` headers in responses ``WWW-Authentication`` headers are removed and
   not cached.

.. ts:confvar:: CONFIG proxy.config.http.cache.cache_urls_that_look_dynamic INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) caching of URLs that look dynamic, i.e.: URLs that end in *``.asp``* or contain a question
   mark (*``?``*), a semicolon (*``;``*), or *``cgi``*. For a full list, please refer to 
   `HttpTransact::url_looks_dynamic </link/to/doxygen>`_

.. ts:confvar:: CONFIG proxy.config.http.cache.enable_default_vary_headers INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) caching of alternate versions of HTTP objects that do not contain the ``Vary`` header.

.. ts:confvar:: CONFIG proxy.config.http.cache.when_to_revalidate INT 0
   :reloadable:

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

.. ts:confvar:: CONFIG proxy.config.http.cache.when_to_add_no_cache_to_msie_requests INT 0
   :reloadable:

   Specifies when to add ``no-cache`` directives to Microsoft Internet Explorer requests. You can specify the following:

   -  ``0`` = ``no-cache`` is *not* added to MSIE requests
   -  ``1`` = ``no-cache`` is added to IMS MSIE requests
   -  ``2`` = ``no-cache`` is added to all MSIE requests

.. ts:confvar:: CONFIG proxy.config.http.cache.required_headers INT 0
   :reloadable:

   The type of headers required in a request for the request to be cacheable.

   -  ``0`` = no headers required to make document cacheable
   -  ``1`` = either the ``Last-Modified`` header, or an explicit lifetime header, ``Expires`` or ``Cache-Control: max-age``, is required
   -  ``2`` = explicit lifetime is required, ``Expires`` or ``Cache-Control: max-age``

.. ts:confvar:: CONFIG proxy.config.http.cache.max_stale_age INT 604800
   :reloadable:

   The maximum age allowed for a stale response before it cannot be cached.

.. ts:confvar:: CONFIG proxy.config.http.cache.range.lookup INT 1

   When enabled (``1``), Traffic Server looks up range requests in the cache.

.. ts:confvar:: CONFIG proxy.config.http.cache.enable_read_while_writer INT 0

   Enables (``1``) or disables (``0``) the ability to read a cached object while another connection is completing a write to cache
   for the same object.

.. ts:confvar:: CONFIG proxy.config.http.cache.fuzz.min_time INT 0
   :reloadable:

   Sets a minimum fuzz time; the default value is ``0``. **Effective fuzz time** is a calculation in the range
   (``fuzz.min_time`` - ``fuzz.min_time``).

.. ts:confvar:: CONFIG proxy.config.http.cache.ignore_accept_mismatch INT 0
   :reloadable:

   When enabled (``1``), Traffic Server serves documents from cache with a ``Content-Type:`` header that does not match the ``Accept:``
   header of the request.

   **Note:** This option should only be enabled if you're having
   problems with caching *and* one of the following is true:

   -  Your origin server sets ``Vary: Accept`` when doing content negotiation with ``Accept`` *OR*
   -  The server does not send a ``406 (Not Acceptable)`` response for types that it cannot serve.

.. ts:confvar:: CONFIG proxy.config.http.cache.ignore_accept_language_mismatch INT 0
   :reloadable:

   When enabled (``1``), Traffic Server serves documents from cache with a ``Content-Language:`` header that does not match the
   ``Accept-Language:`` header of the request.

   **Note:** This option should only be enabled if you're having
   problems with caching and your origin server is guaranteed to set
   ``Vary: Accept-Language`` when doing content negotiation with
   ``Accept-Language``.

.. ts:confvar:: CONFIG proxy.config.http.cache.ignore_accept_charset_mismatch INT 0
   :reloadable:

   When enabled (``1``), Traffic Server serves documents from cache with a ``Content-Type:`` header that does not match the
   ``Accept-Charset:`` header of the request.

   **Note:** This option should only be enabled if you're having
   problems with caching and your origin server is guaranteed to set
   ``Vary: Accept-Charset`` when doing content negotiation with
   ``Accept-Charset``.

.. ts:confvar:: CONFIG proxy.config.http.cache.ignore_client_cc_max_age INT 1
   :reloadable:

   When enabled (``1``), Traffic Server ignores any ``Cache-Control:  max-age`` headers from the client.

.. ts:confvar:: CONFIG proxy.config.cache.permit.pinning INT 0
   :reloadable:

   When enabled (``1``), Traffic Server will keep certain HTTP objects in the cache for a certain time as specified in cache.config.

Heuristic Expiration
====================

.. ts::confvar:: proxy.config.http.cache.heuristic_min_lifetime INT 3600
   :reloadable:

   The minimum amount of time an HTTP object without an expiration date can remain fresh in the cache before is considered to be stale.

.. ts::confvar:: proxy.config.http.cache.heuristic_max_lifetime INT 86400
   :reloadable:

   The maximum amount of time an HTTP object without an expiration date can remain fresh in the cache before is considered to be stale.

.. ts:confvar:: CONFIG proxy.config.http.cache.heuristic_lm_factor FLOAT 0.10000
   :reloadable:

   The aging factor for freshness computations. Traffic Server stores an object for this percentage of the time that elapsed since it last
   changed.

.. ts:confvar:: CONFIG proxy.config.http.cache.fuzz.time INT 240
   :reloadable:

   How often Traffic Server checks for an early refresh, during the period before the document stale time. The interval specified must
   be in seconds.

.. ts:confvar:: CONFIG proxy.config.http.cache.fuzz.probability FLOAT 0.00500
   :reloadable:

   The probability that a refresh is made on a document during the specified fuzz time.

Dynamic Content & Content Negotiation
=====================================

.. ts:confvar:: CONFIG proxy.config.http.cache.vary_default_text STRING NULL
   :reloadable:

   The header on which Traffic Server varies for text documents.

For example: if you specify ``User-agent``, then Traffic Server caches
all the different user-agent versions of documents it encounters.

.. ts:confvar:: CONFIG proxy.config.http.cache.vary_default_images STRING NULL
   :reloadable:

   The header on which Traffic Server varies for images.

.. ts:confvar:: CONFIG proxy.config.http.cache.vary_default_other STRING NULL
   :reloadable:

   The header on which Traffic Server varies for anything other than text and images.

Customizable User Response Pages
================================

.. ts:confvar:: CONFIG proxy.config.body_factory.enable_customizations INT 0
   Specifies whether customizable response pages are enabled or
   disabled and which response pages are used:

   -  ``0`` = disable customizable user response pages
   -  ``1`` = enable customizable user response pages in the default directory only
   -  ``2`` = enable language-targeted user response pages

.. ts:confvar:: CONFIG proxy.config.body_factory.enable_logging INT 1

   Enables (``1``) or disables (``0``) logging for customizable response pages. When enabled, Traffic Server records a message in
   the error log each time a customized response page is used or modified.

.. ts:confvar:: CONFIG proxy.config.body_factory.template_sets_dir STRING config/body_factory

   The customizable response page default directory.

.. ts:confvar:: CONFIG proxy.config.body_factory.response_suppression_mode INT 0

   Specifies when Traffic Server suppresses generated response pages:

   -  ``0`` = never suppress generated response pages
   -  ``1`` = always suppress generated response pages
   -  ``2`` = suppress response pages only for intercepted traffic

DNS
===

.. ts:confvar:: CONFIG proxy.config.dns.search_default_domains INT 1
   :Reloadable:

   Enables (``1``) or disables (``0``) local domain expansion.

Traffic Server can attempt to resolve unqualified hostnames by
expanding to the local domain. For example if a client makes a
request to an unqualified host (``host_x``) and the Traffic Server
local domain is ``y.com`` , then Traffic Server will expand the
hostname to ``host_x.y.com``.

.. ts:confvar:: CONFIG proxy.config.dns.splitDNS.enabled INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) DNS server selection. When enabled, Traffic Server refers to the ``splitdns.config`` file for
   the selection specification. Refer to `Configuring DNS Server Selection (Split DNS) <../security-options#ConfiguringDNSServerSelectionSplit>`_.

.. ts:confvar:: CONFIG proxy.config.dns.url_expansions STRING NULL

   Specifies a list of hostname extensions that are automatically added to the hostname after a failed lookup. For example: if you want
   Traffic Server to add the hostname extension .org, then specify ``org`` as the value for this variable (Traffic Server automatically
   adds the dot (.)).

   **Note:** If the variable
   `proxy.config.http.enable_url_expandomatic`_ is set to ``1`` (the default value), then you do not have to add *``www.``* and
   *``.com``* to this list because Traffic Server automatically tries www. and .com after trying the values you've specified.

.. ts:confvar:: CONFIG proxy.config.dns.resolv_conf STRING /etc/resolv.conf

   Allows to specify which ``resolv.conf`` file to use for finding resolvers. While the format of this file must be the same as the
   standard ``resolv.conf`` file, this option allows an administrator to manage the set of resolvers in an external configuration file,
   without affecting how the rest of the operating system uses DNS.

.. ts:confvar:: CONFIG proxy.config.dns.round_robin_nameservers INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) DNS server round-robin.

.. ts:confvar:: CONFIG proxy.config.dns.nameservers STRING NULL
   :reloadable:

   The DNS servers.

.. ts:confvar:: CONFIG proxy.config.srv_enabled INT 0
   :reloadable:

   Indicates whether to use SRV records for orgin server lookup.

HostDB
======

.. ts:confvar:: CONFIG proxy.config.hostdb.serve_stale_for INT

   The number of seconds for which to use a stale NS record while initiating a background fetch for the new data.

.. ts:confvar:: proxy.config.hostdb.storage_size INT 200000

   The amount of space (in bytes) used to store ``hostdb``. Thevalue of this variable must be increased if you increase the sizeof the
   `proxy.config.hostdb.size`_ variable.

   Default: ``200000`` : The maximum number of entries allowed in the host database.

   **Note:** For values above ``200000``, you must increase the value ofthe `proxy.config.hostdb.storage_size`_ variable by at least 44 bytes per
   entry.

.. ts:confvar:: CONFIG proxy.config.hostdb.ttl_mode INT 0
   :reloadable:

   The host database time to live mode. You can specify one of the
   following:

   -  ``0`` = obey
   -  ``1`` = ignore
   -  ``2`` = min(X,ttl)
   -  ``3`` = max(X,ttl)

.. ts:confvar:: CONFIG proxy.config.hostdb.timeout INT 1440
   :reloadable:

   The foreground timeout (in minutes).

.. ts:confvar:: CONFIG proxy.config.hostdb.strict_round_robin INT 0
   :reloadable:

   When disabled (``0``), Traffic Server always uses the same origin
   server for the same client, for as long as the origin server is
   available.

Logging Configuration
=====================

.. ts:confvar:: CONFIG proxy.config.log.logging_enabled INT 3
   :reloadable:

   Enables and disables event logging:

   -  ``0`` = logging disabled
   -  ``1`` = log errors only
   -  ``2`` = log transactions only
   -  ``3`` = full logging (errors + transactions)

   Refer to `Working with Log Files <../working-log-files>`_.

.. ts:confvar:: CONFIG proxy.config.log.max_secs_per_buffer INT 5
   :reloadable:

   The maximum amount of time before data in the buffer is flushed to disk.

.. ts:confvar:: CONFIG proxy.config.log.max_space_mb_for_logs INT 2000
   :reloadable:

   The amount of space allocated to the logging directory (in MB). 
   **Note:** All files in the logging directory contribute to the space used, even if they are not log files. In collation client mode, if
   there is no local disk logging, or max_space_mb_for_orphan_logs is set to a higher value than max_space_mb_for_logs, TS will
   take proxy.config.log.max_space_mb_for_orphan_logs for maximum allowed log space.

.. ts:confvar:: CONFIG proxy.config.log.max_space_mb_for_orphan_logs INT 25
   :reloadable:

   The amount of space allocated to the logging directory (in MB) if this node is acting as a collation client.

   **Note:** When max_space_mb_for_orphan_logs is take as the maximum allowedlog space in the logging system, the same rule apply to
   proxy.config.log.max_space_mb_for_logs also apply to proxy.config.log.max_space_mb_for_orphan_logs, ie: All files in
   the logging directory contribute to the space used, even if they are not log files. you may need to consider this when you enable full
   remote logging, and bump to the same size as proxy.config.log.max_space_mb_for_logs.

.. ts:confvar:: CONFIG proxy.config.log.max_space_mb_headroom INT 10
   :reloadable:

   The tolerance for the log space limit (in bytes). If the variable `proxy.config.log.auto_delete_rolled_file`_ is set to ``1``
   (enabled), then autodeletion of log files is triggered when the amount of free space available in the logging directory is less than
   the value specified here.

.. ts:confvar:: CONFIG proxy.config.log.hostname STRING localhost
   :reloadable:

   The hostname of the machine running Traffic Server.

.. ts:confvar:: CONFIG proxy.config.log.logfile_dir STRING install_dir\ ``/logs``
   :reloadable:

   The full path to the logging directory. This can be an absolute path or a path relative to the directory in which Traffic Server is installed.

   **Note:** The directory you specify must already exist.

.. ts:confvar:: CONFIG proxy.config.log.logfile_perm STRING rw-r--r--
   :reloadable:

   The log file permissions. The standard UNIX file permissions are used (owner, group, other). Permissible values are:

   ``-`` no permission ``r`` read permission ``w`` write permission ``x`` execute permission

   Permissions are subject to the umask settings for the Traffic Server process. This means that a umask setting of\ ``002`` will not allow
   write permission for others, even if specified in the configuration file. Permissions for existing log files are not changed when the
   configuration is changed.

.. ts:confvar:: CONFIG proxy.config.log.custom_logs_enabled INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) custom logging.

.. ts:confvar:: CONFIG proxy.config.log.squid_log_enabled INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) the `squid log file format <../working-log-files/log-formats#SquidFormat>`_.

.. ts:confvar:: CONFIG proxy.config.log.squid_log_is_ascii INT 1
   :reloadable:

   The squid log file type:

   -  ``1`` = ASCII
   -  ``0`` = binary

.. ts:confvar:: CONFIG proxy.config.log.squid_log_name STRING squid
   :reloadable:

   The `squid log <../working-log-files/log-formats#SquidFormat>`_ filename.

.. ts:confvar:: CONFIG proxy.config.log.squid_log_header STRING NULL

   The `squid log <../working-log-files/log-formats#SquidFormat>`_ file header text.

.. ts:confvar:: CONFIG proxy.config.log.common_log_enabled INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) the `Netscape common log file format <../working-log-files/log-formats#NetscapeFormats>`_.

.. ts:confvar:: CONFIG proxy.config.log.common_log_is_ascii INT 1
   :reloadable:

   The `Netscape common log <../working-log-files/log-formats#NetscapeFormats>`_ file type:

   -  ``1`` = ASCII
   -  ``0`` = binary

.. ts:confvar:: CONFIG proxy.config.log.common_log_name STRING common
   :reloadable:

   The `Netscape common log <../working-log-files/log-formats#NetscapeFormats>`_ filename.

.. ts:confvar:: CONFIG proxy.config.log.common_log_header STRING NULL
   :reloadable:

   The `Netscape common log <../working-log-files/log-formats#NetscapeFormats>``_ file header text.

.. ts:confvar:: CONFIG proxy.config.log.extended_log_enabled INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) the `Netscape extended log file format <../working-log-files/log-formats#NetscapeFormats>`_.

.. ts:confvar:: CONFIG proxy.confg.log.extended_log_is_ascii INT 1

   The `Netscape extended log <../working-log-files/log-formats#NetscapeFormats>`_ file type:

   -  ``1`` = ASCII
   -  ``0`` = binary

.. ts:confvar:: CONFIG proxy.config.log.extended_log_name STRING extended

   The `Netscape extended log <../working-log-files/log-formats#NetscapeFormats>`_ filename.

.. ts:confvar:: CONFIG proxy.config.log.extended_log_header STRING NULL
   :reloadable:

   The `Netscape extended log <../working-log-files/log-formats#NetscapeFormats>`_ file header text.

.. ts:confvar:: CONFIG proxy.config.log.extended2_log_enabled INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) the `Netscape Extended-2 log file format <../working-log-files/log-formats#NetscapeFormats>`_.

.. ts:confvar:: CONFIG proxy.config.log.extended2_log_is_ascii INT 1
   :reloadable:

   The `Netscape Extended-2 log <../working-log-files/log-formats#NetscapeFormats>`_ file type:

   -  ``1`` = ASCII
   -  ``0`` = binary

.. ts:confvar:: CONFIG proxy.config.log.extended2_log_name STRING extended2
   :reloadable:

   The `Netscape Extended-2 log <../working-log-files/log-formats#NetscapeFormats>`_ filename.

.. ts:confvar:: CONFIG proxy.config.log.extended2_log_header STRING NULL
   :reloadable:

   The `Netscape Extended-2 log <../working-log-files/log-formats#NetscapeFormats>`_ file header text.

.. ts:confvar:: CONFIG proxy.config.log.separate_icp_logs INT 0
   :reloadable:

   When enabled (``1``), configures Traffic Server to store ICP transactions in a separate log file.

   -  ``0`` = separation is disabled, all ICP transactions are recorded in the same file as HTTP transactions
   -  ``1`` = all ICP transactions are recorded in a separate log file.
   -  ``-1`` = filter all ICP transactions from the default log files; ICP transactions are not logged anywhere.

.. ts:confvar:: CONFIG proxy.config.log.separate_host_logs INT 0
   :reloadable:

   When enabled (``1``), configures Traffic Server to create a separate log file for HTTP transactions for each origin server listed in the
   ``log_hosts.config`` file. Refer to `HTTP Host Log Splitting <../working-log-files#HTTPHostLogSplitting>`_.

.. ts:confvar:: LOCAL proxy.local.log.collation_mode INT 0
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
server, refer to `logs_xml.config <logs_xml.config>`_.

.. note:: Although Traffic Server supports traditional custom logging, you should use the more versatile XML-based custom formats.

.. ts:confvar:: proxy.confg.log.collation_host STRING NULL

   The hostname of the log collation server. 

.. ts:confvar:: CONFIG proxy.config.log.collation_port INT 8085
   :reloadable:

   The port used for communication between the collation server and client.

.. ts:confvar:: CONFIG proxy.config.log.collation_secret STRING foobar
   :reloadable:

   The password used to validate logging data and prevent the exchange of unauthorized information when a collation server is being used.

.. ts:confvar:: CONFIG proxy.config.log.collation_host_tagged INT 0
   :reloadable:

   When enabled (``1``), configures Traffic Server to include the hostname of the collation client that generated the log entry in each entry.

.. ts:confvar:: CONFIG proxy.config.log.collation_retry_sec INT 5
   :reloadable:

   The number of seconds between collation server connection retries.

.. ts:confvar:: CONFIG proxy.config.log.rolling_enabled INT 1
   :reloadable:

   Specifies how log files are rolled. You can specify the following values:

   -  ``0`` = disables log file rolling
   -  ``1`` = enables log file rolling at specific intervals during the day (specified with the
       `proxy.config.log.rolling_interval_sec`_ and `proxy.config.log.rolling_offset_hr`_ variables)
   -  ``2`` = enables log file rolling when log files reach a specific size (specified with the `proxy.config.log.rolling_size_mb`_ variable)
   -  ``3`` = enables log file rolling at specific intervals during the day or when log files reach a specific size (whichever occurs first)
   -  ``4`` = enables log file rolling at specific intervals during the day when log files reach a specific size (i.e., at a specified
       time if the file is of the specified size)

.. ts:confvar:: CONFIG proxy.config.log.rolling_interval_sec INT 86400
   :reloadable:

   The log file rolling interval, in seconds. The minimum value is ``300`` (5 minutes). The maximum, and default, value is 86400 seconds (one day).

   **Note:** If you start Traffic Server within a few minutes of the next rolling time, then rolling might not occur until the next rolling time.

.. ts:confvar:: CONFIG proxy.config.log.rolling_offset_hr INT 0
   :reloadable:

   The file rolling offset hour. The hour of the day that starts the log rolling period.

.. ts:confvar:: CONFIG proxy.config.log.rolling_size_mb INT 10
   :reloadable:

   The size that log files must reach before rolling takes place.

.. ts:confvar:: CONFIG proxy.config.log.auto_delete_rolled_files INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) automatic deletion of rolled files.

.. ts:confvar:: CONFIG proxy.config.log.sampling_frequency INT 1
   :reloadable:

   Configures Traffic Server to log only a sample of transactions rather than every transaction. You can specify the following values:

   -  ``1`` = log every transaction
   -  ``2`` = log every second transaction
   -  ``3`` = log every third transaction and so on...

.. ts:confvar:: CONFIG proxy.config.http.slow.log.threshold INT 0
   :reloadable:

   The number of milliseconds before a slow connection's debugging stats are dumped. Specify ``1`` to enable or ``0`` to disable.

Diagnostic Logging Configuration
================================

.. ts:confvar:: CONFIG proxy.config.diags.output.status STRING
.. ts:confvar:: CONFIG proxy.config.diags.output.warning STRING 
.. ts:confvar:: CONFIG proxy.config.diags.output.emergency STRING 

   control where Traffic Server should log diagnostic output. Messages at diagnostic level can be directed to any combination of diagnostic
   destinations. Valid diagnostic message destinations are:::

   * 'O' = Log to standard output
   * 'E' = Log to standard error
   * 'S' = Log to syslog
   * 'L' = Log to diags.log

.. topic:: Example

   To log debug diagnostics to both syslog and diags.log:::

        proxy.config.diags.output.debug STRING SL

Reverse Proxy
=============

.. ts:confvar:: CONFIG proxy.config.reverse_proxy.enabled INT 1
   :reloadable:

   Enables (``1``) or disables (``0``) HTTP reverse proxy.

.. ts:confvar:: CONFIG proxy.config.header.parse.no_host_url_redirect STRING NULL
   :reloadable:

   The URL to which to redirect requests with no host headers (reverse
   proxy).

URL Remap Rules
===============

.. ts:confvar:: CONFIG proxy.config.url_remap.default_to_server_pac INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) requests for a PAC file on the proxy service port (8080 by default) to be redirected to the PAC
   port. For this type of redirection to work, the variable `proxy.config.reverse_proxy.enabled`_ must be set to ``1``.

.. ts:confvar:: CONFIG proxy.config.url_remap.default_to_server_pac_port INT -1
   :reloadable:

   Sets the PAC port so that PAC requests made to the Traffic Server 
   proxy service port are redirected this port. ``-1`` is the default
   setting that sets the PAC port to the autoconfiguration port (the
   default autoconfiguration port is 8083). This variable can be used
   together with the `proxy.config.url_remap.default_to_server_pac`_
   variable to get a PAC file from a different port. You must create
   and run a process that serves a PAC file on this port. For example:
   if you create a Perl script that listens on port 9000 and writes a
   PAC file in response to any request, then you can set this variable
   to ``9000``. Browsers that request the PAC file from a proxy server
   on port 8080 will get the PAC file served by the Perl script.

.. ts:confvar:: CONFIG proxy.config.url_remap.remap_required INT 1
   :reloadable:

   Set this variable to ``1`` if you want Traffic Server to serve
   requests only from origin servers listed in the mapping rules of the
   ``remap.config`` file. If a request does not match, then the browser
   will receive an error.

.. ts:confvar:: CONFIG proxy.config.url_remap.pristine_host_hdr INT 1
   :reloadable:

   Set this variable to ``1`` if you want to retain the client host
   header in a request during remapping.

SSL Termination
===============

.. ts:confvar:: CONFIG proxy.config.ssl.SSLv2 INT 0

   Enables (``1``) or disables (``0``) SSLv2. Please don't enable it.

.. ts:confvar:: CONFIG proxy.config.ssl.SSLv3 INT 1

   Enables (``1``) or disables (``0``) SSLv3.

.. ts:confvar:: CONFIG proxy.config.ssl.TLSv1 INT 1

   Enables (``1``) or disables (``0``) TLSv1.

.. ts:confvar:: CONFIG proxy.config.ssl.client.certification_level INT 0

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

.. ts:confvar:: CONFIG proxy.config.ssl.server.cert.path STRING /config

   The location of the SSL certificates and chains used for accepting
   and validation new SSL sessions. If this is a relative path,
   it is appended to the Traffic Server installation PREFIX. All
   certificates and certificate chains listed in
   :file:`ssl_multicert.config` will be loaded relative to this path.

.. ts:confvar:: CONFIG proxy.config.ssl.server.private_key.path STRING NULL

   The location of the SSL certificate private keys. Change this
   variable only if the private key is not located in the SSL
   certificate file. All private keys listed in
   :file:`ssl_multicert.config` will be loaded relative to this
   path.

.. ts:confvar:: CONFIG proxy.config.ssl.server.cert_chain.filename STRING NULL

   The name of a file containing a global certificate chain that
   should be used with every server certificate. This file is only
   used if there are certificates defined in :file:`ssl_multicert.conf`.
   Unless this is an absolute path, it is loaded relative to the
   path specified by `proxy.config.ssl.server.cert.path`_.

.. ts:confvar:: CONFIG proxy.config.ssl.CA.cert.path STRING NULL

   The location of the certificate authority file that client
   certificates will be verified against.

.. ts:confvar:: CONFIG proxy.config.ssl.CA.cert.filename STRING NULL

   The filename of the certificate authority that client certificates
   will be verified against.

Client-Related Configuration
----------------------------

.. ts:confvar:: CONFIG proxy.config.ssl.client.verify.server INT 0

   Configures Traffic Server to verify the origin server certificate
   with the Certificate Authority (CA).

.. ts:confvar:: CONFIG proxy.config.ssl.client.cert.filename STRING NULL

   The filename of SSL client certificate installed on Traffic Server.

.. ts:confvar:: CONFIG proxy.config.ssl.client.cert.path STRING /config

   The location of the SSL client certificate installed on Traffic
   Server.

.. ts:confvar:: CONFIG proxy.config.ssl.client.private_key.filename STRING NULL

   The filename of the Traffic Server private key. Change this variable
   only if the private key is not located in the Traffic Server SSL
   client certificate file.

.. ts:confvar:: CONFIG proxy.config.ssl.client.private_key.path STRING NULL

   The location of the Traffic Server private key. Change this variable
   only if the private key is not located in the SSL client certificate
   file.

.. ts:confvar:: CONFIG proxy.config.ssl.client.CA.cert.filename STRING NULL

   The filename of the certificate authority against which the origin
   server will be verified.

.. ts:confvar:: CONFIG proxy.config.ssl.client.CA.cert.path STRING NULL

   Specifies the location of the certificate authority file against
   which the origin server will be verified.

ICP Configuration
=================

.. ts:confvar:: CONFIG proxy.config.icp.enabled INT 0

   Sets ICP mode for hierarchical caching:

   -  ``0`` = disables ICP
   -  ``1`` = allows Traffic Server to receive ICP queries only
   -  ``2`` = allows Traffic Server to send and receive ICP queries

   Refer to `ICP Peering <../hierachical-caching#ICPPeering>`_.

.. ts:confvar:: CONFIG proxy.config.icp.icp_interface STRING your_interface

   Specifies the network interface used for ICP traffic.

   **Note:** The Traffic Server installation script detects your
   network interface and sets this variable appropriately. If your
   system has multiple network interfaces, check that this variable
   specifies the correct interface.

.. ts:confvar:: CONFIG proxy.config.icp.icp_port INT 3130
   :reloadable:

   Specifies the UDP port that you want to use for ICP messages.

.. ts:confvar:: CONFIG proxy.config.icp.query_timeout INT 2
   :reloadable:

   Specifies the timeout used for ICP queries.

Scheduled Update Configuration
==============================

.. XXX this is missing something:

   ``INT``
   ``0``
   Enables (``1``) or disables (``0``) the Scheduled Update option.

.. ts:confvar:: CONFIG proxy.config.update.force INT 0
   :reloadable:

   Enables (``1``) or disables (``0``) a force immediate update. When
   enabled, Traffic Server overrides the scheduling expiration time for
   all scheduled update entries and initiates updates until this option
   is disabled.

.. ts:confvar:: CONFIG proxy.config.update.retry_count INT 10
   :reloadable:

   Specifies the number of times Traffic Server can retry the scheduled
   update of a URL in the event of failure.

.. ts:confvar:: CONFIG proxy.config.update.retry_interval INT 2
   :reloadable:

   Specifies the delay (in seconds) between each scheduled update retry
   for a URL in the event of failure.

.. ts:confvar:: CONFIG proxy.config.update.concurrent_updates INT 100
   :reloadable:

   Specifies the maximum simultaneous update requests allowed at any
   time. This option prevents the scheduled update process from
   overburdening the host.

Remap Plugin Processor
======================

.. ts:confvar:: CONFIG proxy.config.remap.use_remap_processor INT 0

   Enables (``1``) or disables (``0``) the ability to run separate threads for remap plugin processing.

.. ts:confvar:: CONFIG proxy.config.remap.num_remap_threads INT 1

   Specifies the number of threads that will be used for remap plugin rocessing.

Plug-in Configuration
=====================

.. ts:confvar:: CONFIG proxy.config.plugin.plugin_dir STRING config/plugins

   Specifies the location of Traffic Server plugins.

Sockets
=======

.. ts:confvar:: CONFIG proxy.config.net.defer_accept INT `1`

   default: ``1`` meaning ``on`` all Platforms except Linux: ``45`` seconds

   This directive enables operating system specific optimizations for a listening socket. ``defer_accept`` holds a call to ``accept(2)``
   back until data has arrived. In Linux' special case this is up to a maximum of 45 seconds.

.. ts:confvar:: CONFIG proxy.config.net.sock_send_buffer_size_in INT 0

   Sets the send buffer size for connections from the client to Traffic Server.

.. ts:confvar:: CONFIG proxy.config.net.sock_recv_buffer_size_in INT 0

   Sets the receive buffer size for connections from the client to Traffic Server.

.. ts:confvar:: CONFIG proxy.config.net.sock_option_flag_in INT 0

   Turns different options "on" for the socket handling client connections:::

        TCP_NODELAY (1)
        SO_KEEPALIVE (2)

   **Note:** This is a flag and you look at the bits set. Therefore,
   you must set the value to ``3`` if you want to enable both options
   above.

.. ts:confvar:: CONFIG proxy.config.net.sock_send_buffer_size_out INT 0

   Sets the send buffer size for connections from Traffic Server to the origin server.

.. ts:confvar:: CONFIG proxy.config.net.sock_recv_buffer_size_out INT 0

   Sets the receive buffer size for connections from Traffic Server to
   the origin server.

.. ts:confvar:: CONFIG proxy.config.net.sock_option_flag_out INT 0

   Turns different options "on" for the origin server socket:::

        TCP_NODELAY (1)
        SO_KEEPALIVE (2)

   **Note:** This is a flag and you look at the bits set. Therefore,
   you must set the value to ``3`` if you want to enable both options
   above.

.. ts:confvar:: CONFIG proxy.config.net.sock_mss_in INT 0

   Same as the command line option ``--accept_mss`` that sets the MSS for all incoming requests.


