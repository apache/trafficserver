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

The ``records.config`` file is a list of configurable variables used by
the Traffic Server software. Many of the variables in the
``records.config`` file are set automatically when you set configuration
options in Traffic Line or Traffic Shell. After you modify the
``records.config`` file, navigate to the Traffic Server\ ``bin``
directory and run the command ``traffic_line -x`` to apply the changes.
When you apply changes to one node in a cluster, Traffic Server
automatically applies the changes to all other nodes in the cluster.

Format
======

Each variable has the following format:

::

    CONFIG variable_name DATATYPE variable_value

where ``DATATYPE`` is ``INT`` (integer), ``STRING`` (string), or
``FLOAT`` (floating point).

Examples
========

In the following example, the variable .. _proxy.config.net.sock_mss_in:
``proxy.config.proxy_name`` is
a ``STRING`` datatype with the value ``my_server``. This means that the
name of the Traffic Server proxy is ``my_server``.

::

    CONFIG proxy.config.proxy_name STRING my_server

In the following example, the variable ``proxy.config.arm.enabled`` is
a yes/no flag. A value of ``0`` (zero) disables the option; a value of
``1`` enables the option.

::

    CONFIG proxy.config.arm.enabled INT 0

In the following example, the variable sets the cluster startup timeout
to 10 seconds.

::

    CONFIG proxy.config.cluster.startup_timeout INT 10

Configuration Variables
=======================

The following list describes the configuration variables available in
the ``records.config`` file.

System Variables
================

.. _proxy.config.product_company:
:``proxy.config.product_company``:
:Type: ``STRING``
:Default: ``Apache Software Foundation``
:Description:  The name of the organization developing Traffic Server.



.. _proxy.config.product_vendor:
:``proxy.config.product_vendor``:
:Type: ``STRING``
:Default: ``Apache``
:Description: The name of the vendor providing Traffic Server.


.. _proxy.config.product_name:
:``proxy.config.product_name``:
Type: ``STRING``
:Default: ``Traffic Server``
:Description: The name of the product.


.. _proxy.config.proxy_name:
:``proxy.config.proxy_name``:
:Type: ``STRING``
:Default: ``build_machine``
:Reloadable: Yes
:Description: The name of the Traffic Server node.

.. _proxy.config.bin_path:
:``proxy.config.bin_path``:
:Type: ``STRING``
:Default: ``bin``
Description: The location of the Traffic Server ``bin`` directory.


.. _proxy.config.proxy_binary:
:``proxy.config.proxy_binary``:
:Type: ``STRING``
:Default: ``traffic_server``
:Description: The name of the executable that runs the ``traffic_server`` process.


.. _proxy.config.proxy_binary_opts:
:``proxy.config.proxy_binary_opts``:
:Type: ``STRING``
:Default: ``-M``
:Description: The command-line options for starting Traffic Server.


.. _proxy.config.manager_binary:
:``proxy.config.manager_binary``:
:Type: ``STRING``
:Default: ``traffic_manager``
:Description: The name of the executable that runs the ``traffic_manager`` process.


.. _proxy.config.cli_binary:
:``proxy.config.cli_binary``:
:Type: ``STRING``
:Default: ``traffic_line``
:Description: The name of the executable that runs the command-line interface
    (Traffic Line).


.. _proxy.config.watch_script:
:``proxy.config.watch_script``:
:Type: ``STRING``
:Default: ``traffic_cop``
:Description: The name of the executable that runs the ``traffic_cop`` process.

.. _proxy.config.env_prep:
:``proxy.config.env_prep``:
:Type: ``STRING``
:Default: (none)
:Description: The script executed before the ``traffic_manager`` process spawns
    the ``traffic_server`` process.

.. _proxy.config.config_dir:
:``proxy.config.config_dir``:
:Type: ``STRING``
:Default: ``config``
:Description: The directory that contains Traffic Server configuration files.

.. _proxy.config.temp_dir:
:``proxy.config.temp_dir``:
:Type: ``STRING``
:Default: ``/tmp``
:Description: The directory used for Traffic Server temporary files.

.. _proxy.config.alarm_email:
:``proxy.config.alarm_email``:
:Type: ``STRING``
:Default: (none)
:Reloadable: Yes
:Description: The email address to which Traffic Server sends alarm messages.
    During a custom Traffic Server installation, you can specify the
    email address; otherwise, Traffic Server uses the Traffic Server
    user account name as the default value for this variable.

.. _proxy.config.syslog_facility:
:``proxy.config.syslog_facility``:
:Type: ``STRING``
:Default: ``LOG_DAEMON``
:Description: The facility used to record system log files. Refer to
    `Understanding Traffic Server Log Files <../working-log-files#UnderstandingTrafficServerLogFiles>`_.

.. _proxy.config.cop.core_signal:
:``proxy.config.cop.core_signal``:
:Type: ``INT``
:Default: ``0``
:Description: The signal sent to ``traffic_cop``'s managed processes to stop them.
    ``0`` = no signal is sent.

.. _proxy.config.cop.linux_min_swapfree_kb:
:``proxy.config.cop.linux_min_swapfree_kb``:
:Type: ``INT``
:Default: ``10240``
:Description: The minimum amount of free swap space allowed before Traffic Server
    stops the ``traffic_server``\ and ``traffic_manager`` processes to
    prevent the system from hanging. This configuration variable applies
    if swap is enabled in Linux 2.2 only.

.. _proxy.config.output.logfile:
:``proxy.config.output.logfile``:
:Type: ``STRING``
:Default: ``traffic.out``
:Description: The name and location of the file that contains warnings, status
    messages, and error messages produced by the Traffic Server
    processes. If no path is specified, then Traffic Server creates the
    file in its logging directory.

.. _proxy.config.snapshot_dir:
:``proxy.config.snapshot_dir``:
:Type: ``STRING``
:Default: ``snapshots``
:Description: The directory in which Traffic Server stores configuration snapshots
    on the local system. Unless you specify an absolute path, this
    directory is located in the Traffic Server ``config`` directory.

.. _proxy.config.exec_thread.autoconfig:
:``proxy.config.exec_thread.autoconfig``:
:Type: ``INT``
:Default: ``1``
:Description: When enabled (the default, ``1``), Traffic Server scales threads
    according to the available CPU cores. See the config option below.

.. _proxy.config.exec_thread.autoconfig.scale:
:``proxy.config.exec_thread.autoconfig.scale``:
:Type: ``FLOAT``
:Default: ``1.5``
:Description: Factor by which Traffic Server scales the number of threads. The
    multiplier is usually the number of available CPU cores. By default
    this is scaling factor is ``1.5``.

.. _proxy.config.exec_thread.limit:
:``proxy.config.exec_thread.limit``:
:Type: ``INT``
:Default: ``2``
:Description: What does this do?

.. _proxy.config.accept_threads:
:``proxy.config.accept_threads``:
:Type: ``INT``
:Default: ``0``
:Description: When enabled (``1``), runs a separate thread for accept processing.
    If disabled (``0``), then only 1 thread can be created.

.. _proxy.config.thread.default.stacksize:
:``proxy.config.thread.default.stacksize``:
:Type: ``INT``
:Default: ``1096908``
:Description: The new default thread stack size, for all threads. The original
    default is set at 1 MB.

Network
=======

``proxy.local.incoming_ip_to_bind``
{#proxy.local.incoming_ip_to_bind}
    ``STRING``
    Default: ANY address (0.0.0.0 and ::)
    This variable can be used to bind to a specific IP addresses in a
    multi-interface setup. It sets a global default which is used for
    all ports unless specifically overridden in a port configuration
    descriptor. To specify addresses for both IPv4 and IPv6 list both
    addresses in this value. The defaults for the IP addresses families
    are handled independently.

    Specify the IPv4 address to use for the local address of client
    (listening) connections.

    ::

        LOCAL proxy.local.incoming_ip_to_bind STRING 192.168.101.18

    Specify the IPv4 and IPv6 addresses to use for the local address of
    client (listening) connections.

    ::

        LOCAL proxy.local.incoming_ip_to_bind STRING 192.168.101.17 fc07:192:168:101::17

``proxy.local.outgoing_ip_to_bind``
{#proxy.local.outgoing_ip_to_bind}
    ``STRING``
    Default: ANY address (0.0.0.0 and ::)
    This sets the default IP address used for the local address when
    connecting to an origin server. It is used unless specifically
    overridden in a port configuration descriptor. To specify addresses
    for both IPv4 and IPv6 list both addresses in this value. The
    defaults for the IP addresses families are handled independently.

    Specify the IPv4 address to use for the local address of origin
    server connections.

    ::

        LOCAL proxy.local.outgoing_ip_to_bind STRING 192.168.101.18

    Specify the IPv4 and IPv6 addresses to use for the local address of
    origin server connections.

    ::

        LOCAL proxy.local.outgoing_ip_to_bind STRING 192.168.101.17 fc07:192:168:101::17

Cluster
=======

``proxy.local.cluster.type`` {#proxy.local.cluster.type}
    ``INT``
    Default: ``3``
    Sets the clustering mode:

    -  ``1`` = full-clustering mode
    -  ``2`` = management-only mode
    -  ``3`` = no clustering

``proxy.config.cluster.rsport`` {#proxy.config.cluster.rsport}
    ``INT``
    Default: ``8088``
    The reliable service port. The reliable service port is used to send
    configuration information between the nodes in a cluster. All nodes
    in a cluster must use the same reliable service port.

``proxy.config.cluster.threads`` {#proxy.config.cluster.threads}
    ``INT``
    Default: ``1``
    The number of threads for cluster communication. On heavy cluster,
    the number should be adjusted. It is recommend that take the thread
    CPU usage as a reference when adjusting.

Local Manager
=============

``proxy.config.lm.sem_id`` {#proxy.config.lm.sem_id}
    ``INT``
    Default: ``11452``
    The semaphore ID for the local manager.

``proxy.config.admin.autoconf_port``
{#proxy.config.admin.autoconf_port}
    ``INT``
    Default: ``8083``
    The autoconfiguration port.

``proxy.config.admin.number_config_bak``
{#proxy.config.admin.number_config_bak}
    ``INT``
    Default: ``3``
    The maximum number of copies of rolled configuration files to keep.

``proxy.config.admin.user_id`` {#proxy.config.admin.user_id}
    ``STRING``
    Default: ``nobody``
    Option used to specify who to run the ``traffic_server`` process as;
    also used to specify ownership of config and log files.

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

``proxy.config.process_manager.mgmt_port``
{#proxy.config.process_manager.mgmt_port}
    ``INT``
    Default: ``8084``
    The port used for internal communication between the
    ``traffic_manager`` and ``traffic_server`` processes.

Alarm Configuration
===================

``proxy.config.alarm.bin`` {#proxy.config.alarm.bin}
    ``STRING``
    Default: ``example_alarm_bin.sh``
    Name of the script file that can execute certain actions when an
    alarm is signaled. The default file is a sample script named
    ``example_alarm_bin.sh`` located in the ``bin`` directory. You must
    edit the script to suit your needs.

``proxy.config.alarm.abs_path`` {#proxy.config.alarm.abs_path}
    ``STRING``
    Default: ``NULL``
    The full path to the script file that sends email to alert someone
    about Traffic Server problems.

HTTP Engine
===========

``proxy.config.http.server_ports`` {#proxy.config.http.server_ports}
    ``STRING``
    Default: ``8080``
    Ports used for proxying HTTP traffic. This is a list, separated by
    space or comma, of port descriptors. Each descriptor is a sequence
    of keywords and values separated by colons. Not all keywords have
    values, those that do are specifically noted. Keywords with values
    can have an optional '=' character separating the keyword and value.
    The case of keywords is ignored. The order of keywords is irrelevant
    unless keywords conflict (e.g. ``tr-full`` and ``ssl``) in which
    case the right most keyword dominates, although in such cases odd
    behavior may result.

    .. raw:: html

       <table><tr><td>

    Keyword

    .. raw:: html

       </td><td>

    Meaning

    .. raw:: html

       </td></tr>
       <tr><td>

    number

    .. raw:: html

       </td><td>

    IP port. Required.

    .. raw:: html

       </td></tr>
       <tr><td>

    ipv6

    .. raw:: html

       </td><td>

    Use IPv6.

    .. raw:: html

       </td></tr>
       <tr><td>

    ipv4

    .. raw:: html

       </td><td>

    Use IPv4. Default.

    .. raw:: html

       </td></tr>
       <tr><td>

    tr-in

    .. raw:: html

       </td><td>

    Use inbound transparency (to client).

    .. raw:: html

       </td></tr>
       <tr><td>

    tr-out

    .. raw:: html

       </td><td>

    Use outbound transparency (to server).

    .. raw:: html

       </td></tr>
       <tr><td>

    tr-full

    .. raw:: html

       </td><td>

    Full transparency, both inbound and outbound.

    .. raw:: html

       </td></tr>
       <tr><td>

    ssl

    .. raw:: html

       </td><td>

    Use SSL termination.

    .. raw:: html

       </td></tr>
       <tr><td>

    blind

    .. raw:: html

       </td><td>

    Use as a blind tunnel (for ``CONNECT``).

    .. raw:: html

       </td></tr>
       <tr><td>

    ip-in

    .. raw:: html

       </td><td>

    Use the keyword value as the local inbound (listening) address. This
    will also set the address family if not explicitly specified. If the
    IP address family is specified by ``ipv4`` or ``ipv6`` it must agree
    with this address.

    .. raw:: html

       </td></tr>
       <tr><td>

    ip-out

    .. raw:: html

       </td><td>

    Use the value as the local address when connecting to a server. This
    may be specified twice, once for IPv4 and once for IPv6. The actual
    address used will be determined by the family of the origin server
    address.

    .. raw:: html

       </td></tr>
       </table>

    Examples -

     80 80:ipv6

    Listen on port 80 on any address for IPv4 and IPv6.

     IPv4:8080:tr-FULL TR-full:IP-in=[fc02:10:10:1::1]:8080

    Listen transparently on any IPv4 address on port 8080, and
    transparently on port 8080 on local address ``fc01:10:10:1::1``
    (which implies ``ipv6``).

     8080:ipv6:tr-full 443:ssl
    80:ip-in=192.168.17.1:ip-out=[fc01:10:10:1::1]:ip-out=10.10.10.1

    Listen on port 8080 any address for IPv6, fully transparent. Set up
    an SSL port on 443. Listen on IP address 192.168.17.1, port 80,
    IPv4, and connect to origin servers using the local address
    10.10.10.1 for IPv4 and fc01:10:10:1::1 for IPv6.

    Note: All IPv6 addresses must be enclosed in square brackets.

    Note: For SSL you must still configure the certificates, this option
    handles only the port configuration.

    Note: old style configuration of ports should still work but support
    for that will be removed at some point in the future.

``proxy.config.http.server_port`` {#proxy.config.http.server_port}
    ``INT``
    Default: ``8080``
    DEPRECATED: 3.2
    The port that Traffic Server uses when acting as a web proxy server
    for web traffic.

``proxy.config.http.server_port_attr``
{#proxy.config.http.server_port_attr}
    ``STRING``
    Default: ``X``
    DEPRECATED: 3.2
    The server port options. You can specify one of the following:

    -  C=SERVER_PORT_COMPRESSED
    -  X=SERVER_PORT_DEFAULT
    -  T=SERVER_PORT_BLIND_TUNNEL

``proxy.config.http.server_other_ports``
{#proxy.config.http.server_other_ports}
    ``STRING``
    Default: ``NULL``
    DEPRECATED: 3.2
    The ports other than the port specified by the variable
    ``proxy.config.http.server_port`` to bind for incoming HTTP
    requests. Example: CONFIG proxy.config.http.server_other_ports
    STRING 6060:X 9090:X would listen to ports ``6060``, ``9090``, and
    the port specified by ``proxy.config.http.server_port``.

``proxy.config.http.connect_ports`` {#proxy.config.http.connect_ports}
    ``STRING``
    Default: ``443 563``
    The range of ports that can be used for tunneling. 
    Traffic Server allows tunnels only to the specified ports. 
    Supports both wildcards ('*') and ranges ("0-1023").

``proxy.config.http.insert_request_via_str``
{#proxy.config.http.insert_request_via_str}
    ``INT``
    Default: ``1``
    Reloadable.
    You can specify one of the following:

    -  ``0`` = no extra information is added to the string.
    -  ``1`` = all extra information is added.
    -  ``2`` = some extra information is added.

    Note: the Via: header string interpretation can be ``decoded
    here. </tools/via>``_

``proxy.config.http.insert_response_via_str``
{#proxy.config.http.insert_response_via_str}
    ``INT``
    Default: ``1``
    Reloadable.
    You can specify one of the following:

    -  ``0`` no extra information is added to the string.
    -  ``1`` all extra information is added.
    -  ``2`` some extra information is added.

``proxy.config.http.response_server_enabled``
{#proxy.config.http.response_server_enabled}
    ``INT``
    Default: ``1``
    Reloadable.
    You can specify one of the following:

    -  ``0`` no Server: header is added to the response.
    -  ``1`` the Server: header is added (see string below).
    -  ``2`` the Server: header is added only if the response from
       Origin does not have one already.

``proxy.config.http.insert_age_in_response``
{#proxy.config.http.insert_age_in_response}
    ``INT``
    Default: ``1``
    Reloadable.
    This option specifies whether Traffic Server should insert an
    ``Age`` header in the response. The Age field value is the cache's
    estimate of the amount of time since the response was generated or
    revalidated by the origin server.

    -  ``0`` no ``Age`` header is added
    -  ``1`` the ``Age`` header is added

``proxy.config.http.response_server_str``
{#proxy.config.http.response_server_str}
    ``STRING``
    Default: ``ATS/``
    Reloadable.
    The Server: string that ATS will insert in a response header (if
    requested, see above). Note that the current version number is
    always appended to this string.

``proxy.config.http.enable_url_expandomatic``
{#proxy.config.http.enable_url_expandomatic}
    ``INT``
    Default: ``1``
    Reloadable.
    Enables (``1``) or disables (``0``) ``.com`` domain expansion. This
    configures the Traffic Server to resolve unqualified hostnames by
    prepending with ``www.`` and appending with ``.com`` before
    redirecting to the expanded address. For example: if a client makes
    a request to ``host``, then Traffic Server redirects the request to
    ``www.host.com``.

``proxy.config.http.chunking_enabled``
{#proxy.config.http.chunking_enabled}
    ``INT``
    Default: ``1``
    Reloadable.
    Specifies whether Traffic Sever can generate a chunked response:

    -  ``0`` Never
    -  ``1`` Always
    -  ``2`` Generate a chunked response if the server has returned
       HTTP/1.1 before
    -  ``3`` = Generate a chunked response if the client request is
       HTTP/1.1 and the origin server has returned HTTP/1.1 before

    **Note:** If HTTP/1.1 is used, then Traffic Server can use
    keep-alive connections with pipelining to origin servers. If
    HTTP/0.9 is used, then Traffic Server does not use ``keep-alive``
    connections to origin servers. If HTTP/1.0 is used, then Traffic
    Server can use ``keep-alive`` connections without pipelining to
    origin servers.

``proxy.config.http.share_server_sessions``
{#proxy.config.http.share_server_sessions}
    ``INT``
    Default: ``1``
    Enables (``1``) or disables (``0``) the reuse of server sessions.

``proxy.config.http.record_heartbeat``
{#proxy.config.http.record_heartbeat}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) ``traffic_cop`` heartbeat
    logging.

``proxy.config.http.use_client_target_addr``
{#proxy.config.http.use_client_target_addr}
    ``INT``
    Default: ``0``
    Reloadable.
    Avoid DNS lookup for forward transparent requests:

    -  ``0`` Never.
    -  ``1`` Avoid DNS lookup if possible.

    This option causes Traffic Server to avoid where possible doing DNS
    lookups in forward transparent proxy mode. The option is only
    effective if the following three conditions are true -

    -  Traffic Server is in forward proxy mode.
    -  Traffic Server is using client side transparency.
    -  The target URL has not been modified by either remapping or a
       plugin.

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

    1. Embedded IP addresses in a protocol with DNS load sharing. In
       this case, even though Traffic Server and the client both make
       the same request to the same DNS resolver chain, they may get
       different origin server addresses. If the address is embedded in
       the protocol then the overall exchange will fail. One current
       example is Microsoft Windows update, which presumably embeds the
       address as a security measure.

    2. The client has access to local DNS zone information which is not
       available to Traffic Server. There are corporate nets with local
       DNS information for internal servers which, by design, is not
       propagated outside the core corporate network. Depending a
       network topology it can be the case that Traffic Server can
       access the servers by IP address but cannot resolve such
       addresses by name. In such as case the client supplied target
       address must be used.

    Additional Notes:

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

``proxy.config.http.parent_proxy_routing_enable``
{#proxy.config.http.parent_proxy_routing_enable}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) the parent caching option. Refer
    to ``Hierarchical Caching <../hierachical-caching>``_.

``proxy.config.http.parent_proxy.retry_time``
{#proxy.config.http.parent_proxy.retry_time}
    ``INT``
    Default: ``300``
    Reloadable.
    The amount of time allowed between connection retries to a parent
    cache that is unavailable.

``proxy.config.http.parent_proxy.fail_threshold``
{#proxy.config.http.parent_proxy.fail_threshold}
    ``INT``
    Default: ``10``
    Reloadable.
    The number of times the connection to the parent cache can fail
    before Traffic Server considers the parent unavailable.

``proxy.config.http.parent_proxy.total_connect_attempts``
{#proxy.config.http.parent_proxy.total_connect_attempts}
    ``INT``
    Default: ``4``
    Reloadable.
    The total number of connection attempts allowed to a parent cache
    before Traffic Server bypasses the parent or fails the request
    (depending on the ``go_direct`` option in the ``bypass.config``
    file).

``proxy.config.http.parent_proxy.per_parent_connect_attempts``
{#proxy.config.http.parent_proxy.per_parent_connect_attempts}
    ``INT``
    Default: ``2``
    Reloadable.
    The total number of connection attempts allowed per parent, if
    multiple parents are used.

``proxy.config.http.parent_proxy.connect_attempts_timeout``
{#proxy.config.http.parent_proxy.connect_attempts_timeout}
    ``INT``
    Default: ``30``
    Reloadable.
    The timeout value (in seconds) for parent cache connection attempts.

``proxy.config.http.forward.proxy_auth_to_parent``
{#proxy.config.http.forward.proxy_auth_to_parent}
    ``INT``
    Default: ``0``
    Reloadable.
    Configures Traffic Server to send proxy authentication headers on to
    the parent cache.

HTTP Connection Timeouts
========================

``proxy.config.http.keep_alive_no_activity_timeout_in``
{#proxy.config.http.keep_alive_no_activity_timeout_in}
    ``INT``
    Default: ``10``
    Reloadable.
    Specifies how long Traffic Server keeps connections to clients open
    for a subsequent request after a transaction ends.

``proxy.config.http.keep_alive_no_activity_timeout_out``
{#proxy.config.http.keep_alive_no_activity_timeout_out}
    ``INT``
    Default: ``10``
    Reloadable.
    Specifies how long Traffic Server keeps connections to origin
    servers open for a subsequent transfer of data after a transaction
    ends.

``proxy.config.http.transaction_no_activity_timeout_in``
{#proxy.config.http.transaction_no_activity_timeout_in}
    ``INT``
    Default: ``120``
    Reloadable.
    Specifies how long Traffic Server keeps connections to clients open
    if a transaction stalls.

``proxy.config.http.transaction_no_activity_timeout_out``
{#proxy.config.http.transaction_no_activity_timeout_out}
    ``INT``
    Default: ``120``
    Reloadable.
    Specifies how long Traffic Server keeps connections to origin
    servers open if the transaction stalls.

``proxy.config.http.transaction_active_timeout_in``
{#proxy.config.http.transaction_active_timeout_in}
    ``INT``
    Default: ``0``
    Reloadable.
    The maximum amount of time Traffic Server can remain connected to a
    client. If the transfer to the client is not complete before this
    timeout expires, then Traffic Server closes the connection.

The default value of ``0`` specifies that there is no timeout.

``proxy.config.http.transaction_active_timeout_out``
{#proxy.config.http.transaction_active_timeout_out}
    ``INT``
    Default: ``0``
    Reloadable.
    The maximum amount of time Traffic Server waits for fulfillment of a
    connection request to an origin server. If Traffic Server does not
    complete the transfer to the origin server before this timeout
    expires, then Traffic Server terminates the connection request.

The default value of ``0`` specifies that there is no timeout.

``proxy.config.http.accept_no_activity_timeout``
{#proxy.config.http.accept_no_activity_timeout}
    ``INT``
    Default: ``120``
    Reloadable.
    The timeout interval in seconds before Traffic Server closes a
    connection that has no activity.

``proxy.config.http.background_fill_active_timeout``
{#proxy.config.http.background_fill_active_timeout}
    ``INT``
    Default: ``60``
    Reloadable.
    Specifies how long Traffic Server continues a background fill before
    giving up and dropping the origin server connection.

``proxy.config.http.background_fill_completed_threshold``
{#proxy.config.http.background_fill_completed_threshold}
    ``FLOAT``
    Default: ``0.50000``
    Reloadable.
    The proportion of total document size already transferred when a
    client aborts at which the proxy continues fetching the document
    from the origin server to get it into the cache (a **background
    fill**).

Origin Server Connect Attempts
==============================

``proxy.config.http.connect_attempts_max_retries``
{#proxy.config.http.connect_attempts_max_retries}
    ``INT``
    Default: ``6``
    Reloadable.
    The maximum number of connection retries Traffic Server can make
    when the origin server is not responding.

``proxy.config.http.connect_attempts_max_retries_dead_server``
{#proxy.config.http.connect_attempts_max_retries_dead_server}
    ``INT``
    Default: ``2``
    Reloadable.
    The maximum number of connection retries Traffic Server can make
    when the origin server is unavailable.

``proxy.config.http.server_max_connections``
{#proxy.config.http.server_max_connections}
    ``INT``
    Default: ``0``
    Reloadable.
    Limits the number of socket connections across all origin servers to
    the value specified. To disable, set to zero (``0``).

``proxy.config.http.origin_max_connections``
{#proxy.config.http.origin_max_connections}
    ``INT``
    Default: ``0``
    Reloadable.
    Limits the number of socket connections per origin server to the
    value specified. To enable, set to one (``1``).

``proxy.config.http.origin_min_keep_alive_connections``
{#proxy.config.http.origin_min_keep_alive_connections}
    ``INT``
    Default: ``0``
    Reloadable.
    As connection to an origin server are opened, keep at least 'n'
    number of connections open to that origin, even if the connection
    isn't used for a long time period. Useful when the origin supports
    keep-alive, removing the time needed to set up a new connection from
    the next request at the expense of added (inactive) connections. To
    enable, set to one (``1``).

``proxy.config.http.connect_attempts_rr_retries``
{#proxy.config.http.connect_attempts_rr_retries}
    ``INT``
    Default: ``2``
    Reloadable.
    The maximum number of failed connection attempts allowed before a
    round-robin entry is marked as 'down' if a server has round-robin
    DNS entries.

``proxy.config.http.connect_attempts_timeout``
{#proxy.config.http.connect_attempts_timeout}
    ``INT``
    Default: ``30``
    Reloadable.
    The timeout value (in seconds) for an origin server connection.

``proxy.config.http.post_connect_attempts_timeout``
{#proxy.config.http.post_connect_attempts_timeout}
    ``INT``
    Default: ``1800``
    Reloadable.
    The timeout value (in seconds) for an origin server connection when
    the client request is a ``POST`` or ``PUT`` request.

``proxy.config.http.down_server.cache_time``
{#proxy.config.http.down_server.cache_time}
    ``INT``
    Default: ``900``
    Reloadable.
    Specifies how long (in seconds) Traffic Server remembers that an
    origin server was unreachable.

``proxy.config.http.down_server.abort_threshold``
{#proxy.config.http.down_server.abort_threshold}
    ``INT``
    Default: ``10``
    Reloadable.
    The number of seconds before Traffic Server marks an origin server
    as unavailable after a client abandons a request because the origin
    server was too slow in sending the response header.

Congestion Control
==================

``proxy.config.http.congestion_control.enabled``
{#proxy.config.http.congestion_control.enabled}
    ``INT``
    Default: ``0``
    Enables (``1``) or disables (``0``) the Congestion Control option,
    which configures Traffic Server to stop forwarding HTTP requests to
    origin servers when they become congested. Traffic Server sends the
    client a message to retry the congested origin server later. Refer
    to ``Using Congestion
    Control <../http-proxy-caching#UsingCongestionControl>``_.

Negative Response Caching
=========================

``proxy.config.http.negative_caching_enabled``
{#proxy.config.http.negative_caching_enabled}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server caches negative responses (such
    as ``404 Not Found``) when a requested page does not exist. The next
    time a client requests the same page, Traffic Server serves the
    negative response directly from cache.

    **Note**: ``Cache-Control`` directives from the server forbidding
    cache are ignored for the following HTTP response codes, regardless
    of the value specified for the
    ``proxy.config.http.negative_caching_enabled`` variable. The
    following negative responses are cached by Traffic Server:

    ::

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

``proxy.config.http.anonymize_remove_from``
{#proxy.config.http.anonymize_remove_from}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server removes the ``From`` header to
    protect the privacy of your users.

``proxy.config.http.anonymize_remove_referer``
{#proxy.config.http.anonymize_remove_referer}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server removes the ``Referrer`` header
    to protect the privacy of your site and users.

``proxy.config.http.anonymize_remove_user_agent``
{#proxy.config.http.anonymize_remove_user_agent}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server removes the ``User-agent``
    header to protect the privacy of your site and users.

``proxy.config.http.anonymize_remove_cookie``
{#proxy.config.http.anonymize_remove_cookie}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server removes the ``Cookie`` header
    to protect the privacy of your site and users.

``proxy.config.http.anonymize_remove_client_ip``
{#proxy.config.http.anonymize_remove_client_ip}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server removes ``Client-IP`` headers
    for more privacy.

``proxy.config.http.anonymize_insert_client_ip``
{#proxy.config.http.anonymize_insert_client_ip}
    ``INT``
    Default: ``1``
    Reloadable.
    When enabled (``1``), Traffic Server inserts ``Client-IP`` headers
    to retain the client IP address.

``proxy.config.http.append_xforwards_header``
{#proxy.config.http.append_xforwards_header}
    ``INT``
    Default: ``0``
    When enabled (``1``), Traffic Server appends ``X-Forwards`` headers
    to outgoing requests.

``proxy.config.http.anonymize_other_header_list``
{#proxy.config.http.anonymize_other_header_list}
    ``STRING``
    Default: ``NULL``
    Reloadable.
    The headers Traffic Server should remove from outgoing requests.

``proxy.config.http.insert_squid_x_forwarded_for``
{#proxy.config.http.insert_squid_x_forwarded_for}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server adds the client IP address to
    the ``X-Forwarded-For`` header.

``proxy.config.http.normalize_ae_gzip``
{#proxy.config.http.normalize_ae_gzip}
    ``INT``
    Default: ``0``
    Reloadable.
    Enable (``1``) to normalize all ``Accept-Encoding:`` headers to one
    of the following:

    -  ``Accept-Encoding: gzip`` (if the header has ``gzip`` or
       ``x-gzip`` with any ``q``) **OR**
    -  *blank* (for any header that does not include ``gzip``)

    This is useful for minimizing cached alternates of documents (e.g.
    ``gzip, deflate`` vs. ``deflate, gzip``). Enabling this option is
    recommended if your origin servers use no encodings other than
    ``gzip``.

Security
========

``proxy.config.http.push_method_enabled``
{#proxy.config.http.push_method_enabled}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) the HTTP ``PUSH`` option, which
    allows you to deliver content directly to the cache without a user
    request.

    **Important:** If you enable this option, then you must also specify
    a filtering rule in the ip_allow.config file to allow only certain
    machines to push content into the cache.

Cache Control
=============

``proxy.config.cache.enable_read_while_writer``
{#proxy.config.cache.enable_read_while_writer}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) ability to a read cached object
    while the another connection is completing the write to cache for
    the same object.

``proxy.config.cache.force_sector_size``
{#proxy.config.cache.force_sector_size}
    ``INT``
    Default: ``512``
    Reloadable.
    Forces the use of a specific hardware sector size (512 - 8192
    bytes).

``proxy.config.http.cache.http`` {#proxy.config.http.cache.http}
    ``INT``
    Default: ``1``
    Reloadable.
    Enables (``1``) or disables (``0``) caching of HTTP requests.

``proxy.config.http.cache.ignore_client_no_cache``
{#proxy.config.http.cache.ignore_client_no_cache}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server ignores client requests to
    bypass the cache.

``proxy.config.http.cache.ims_on_client_no_cache``
{#proxy.config.http.cache.ims_on_client_no_cache}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server issues a conditional request to
    the origin server if an incoming request has a ``No-Cache`` header.

``proxy.config.http.cache.ignore_server_no_cache``
{#proxy.config.http.cache.ignore_server_no_cache}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server ignores origin server requests
    to bypass the cache.

``proxy.config.http.cache.cache_responses_to_cookies``
{#proxy.config.http.cache.cache_responses_to_cookies}
    ``INT``
    Default: ``3``
    Reloadable.
    Specifies how cookies are cached:

    -  ``0`` = do not cache any responses to cookies
    -  ``1`` = cache for any content-type
    -  ``2`` = cache only for image types
    -  ``3`` = cache for all but text content-types

``proxy.config.http.cache.ignore_authentication``
{#proxy.config.http.cache.ignore_authentication}
    ``INT``
    Default: ``0``
    When enabled (``1``), Traffic Server ignores ``WWW-Authentication``
    headers in responses ``WWW-Authentication`` headers are removed and
    not cached.

``proxy.config.http.cache.cache_urls_that_look_dynamic``
{#proxy.config.http.cache.cache_urls_that_look_dynamic}
    ``INT``
    Default: ``1``
    Reloadable.
    Enables (``1``) or disables (``0``) caching of URLs that look
    dynamic, i.e.: URLs that end in *``.asp``* or contain a question
    mark (*``?``*), a semicolon (*``;``*), or *``cgi``*. For a
    full list, please refer to
    ``HttpTransact::url_looks_dynamic </link/to/doxygen>``_

``proxy.config.http.cache.enable_default_vary_headers``
{#proxy.config.http.cache.enable_default_vary_headers}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) caching of alternate versions of
    HTTP objects that do not contain the ``Vary`` header.

``proxy.config.http.cache.when_to_revalidate``
{#proxy.config.http.cache.when_to_revalidate}
    ``INT``
    Default: ``0``
    Reloadable.
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

``proxy.config.http.cache.when_to_add_no_cache_to_msie_requests``
{#proxy.config.http.cache.when_to_add_no_cache_to_msie_requests}
    ``INT``
    Default: 0
    Reloadable.
    Specifies when to add ``no-cache`` directives to Microsoft Internet
    Explorer requests. You can specify the following:

    -  ``0`` = ``no-cache`` is *not* added to MSIE requests
    -  ``1`` = ``no-cache`` is added to IMS MSIE requests
    -  ``2`` = ``no-cache`` is added to all MSIE requests

``proxy.config.http.cache.required_headers``
{#proxy.config.http.cache.required_headers}
    ``INT``
    Default: ``0``
    Reloadable.
    The type of headers required in a request for the request to be
    cacheable.

    -  ``0`` = no headers required to make document cacheable
    -  ``1`` = either the ``Last-Modified`` header, or an explicit
       lifetime header, ``Expires`` or ``Cache-Control: max-age``, is
       required
    -  ``2`` = explicit lifetime is required, ``Expires`` or
       ``Cache-Control: max-age``

``proxy.config.http.cache.max_stale_age``
{#proxy.config.http.cache.max_stale_age}
    ``INT``
    Default: ``604800``
    Reloadable.
    The maximum age allowed for a stale response before it cannot be
    cached.

``proxy.config.http.cache.range.lookup``
{#proxy.config.http.cache.range.lookup}
    ``INT``
    Default: ``1``
    When enabled (``1``), Traffic Server looks up range requests in the
    cache.

``proxy.config.http.cache.enable_read_while_writer``
{#proxy.config.http.cache.enable_read_while_writer}
    ``INT``
    Default: ``0``
    Enables (``1``) or disables (``0``) the ability to read a cached
    object while another connection is completing a write to cache for
    the same object.

``proxy.config.http.cache.fuzz.min_time``
{#proxy.config.http.cache.fuzz.min_time}
    ``INT``
    Default: ``0``
    Reloadable.
    Sets a minimum fuzz time; the default value is ``0``. **Effective
    fuzz time** is a calculation in the range (``fuzz.min_time`` -
    ``fuzz.min_time``).

``proxy.config.http.cache.ignore_accept_mismatch``
{#proxy.config.http.cache.ignore_accept_mismatch}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server serves documents from cache
    with a ``Content-Type:`` header that does not match the ``Accept:``
    header of the request.

    **Note:** This option should only be enabled if you're having
    problems with caching *and* one of the following is true:

    -  Your origin server sets ``Vary: Accept`` when doing content
       negotiation with ``Accept`` *OR*
    -  The server does not send a ``406 (Not Acceptable)`` response for
       types that it cannot serve.

``proxy.config.http.cache.ignore_accept_language_mismatch``
{#proxy.config.http.cache.ignore_accept_language_mismatch}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server serves documents from cache
    with a ``Content-Language:`` header that does not match the
    ``Accept-Language:`` header of the request.

    **Note:** This option should only be enabled if you're having
    problems with caching and your origin server is guaranteed to set
    ``Vary: Accept-Language`` when doing content negotiation with
    ``Accept-Language``.

``proxy.config.http.cache.ignore_accept_charset_mismatch``
{#proxy.config.http.cache.ignore_accept_charset_mismatch}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server serves documents from cache
    with a ``Content-Type:`` header that does not match the
    ``Accept-Charset:`` header of the request.

    **Note:** This option should only be enabled if you're having
    problems with caching and your origin server is guaranteed to set
    ``Vary: Accept-Charset`` when doing content negotiation with
    ``Accept-Charset``.

``proxy.config.http.cache.ignore_client_cc_max_age``
{#proxy.config.http.cache.ignore_client_cc_max_age}
    ``INT``
    Default: ``1``
    Reloadable.
    When enabled (``1``), Traffic Server ignores any
    ``Cache-Control:  max-age`` headers from the client.

``proxy.config.cache.permit.pinning``
{#proxy.config.cache.permit.pinning}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), Traffic Server will keep certain HTTP objects
    in the cache for a certain time as specified in cache.config.

Heuristic Expiration
====================

``proxy.config.http.cache.heuristic_min_lifetime``
{#proxy.config.http.cache.heuristic_min_lifetime}
    ``INT``
    Default: ``3600`` (1 hour)
    Reloadable.
    The minimum amount of time an HTTP object without an expiration date
    can remain fresh in the cache before is considered to be stale.

``proxy.config.http.cache.heuristic_max_lifetime``
{#proxy.config.http.cache.heuristic_max_lifetime}
    ``INT``
    Default: ``86400`` (1 day)
    Reloadable.
    The maximum amount of time an HTTP object without an expiration date
    can remain fresh in the cache before is considered to be stale.

``proxy.config.http.cache.heuristic_lm_factor``
{#proxy.config.http.cache.heuristic_lm_factor}
    ``FLOAT``
    Default: ``0.10000``
    Reloadable.
    The aging factor for freshness computations. Traffic Server stores
    an object for this percentage of the time that elapsed since it last
    changed.

``proxy.config.http.cache.fuzz.time``
{#proxy.config.http.cache.fuzz.time}
    ``INT``
    Default: ``240``
    Reloadable.
    How often Traffic Server checks for an early refresh, during the
    period before the document stale time. The interval specified must
    be in seconds.

``proxy.config.http.cache.fuzz.probability``
{#proxy.config.http.cache.fuzz.probability}
    ``FLOAT``
    Default: ``0.00500``
    Reloadable.
    The probability that a refresh is made on a document during the
    specified fuzz time.

Dynamic Content & Content Negotiation
=====================================

``proxy.config.http.cache.vary_default_text``
{#proxy.config.http.cache.vary_default_text}
    ``STRING``
    Default: ``NULL``
    Reloadable.
    The header on which Traffic Server varies for text documents.

For example: if you specify ``User-agent``, then Traffic Server caches
all the different user-agent versions of documents it encounters.

``proxy.config.http.cache.vary_default_images``
{#proxy.config.http.cache.vary_default_images}
    ``STRING``
    Default: ``NULL``
    Reloadable.
    The header on which Traffic Server varies for images.

``proxy.config.http.cache.vary_default_other``
{#proxy.config.http.cache.vary_default_other}
    ``STRING``
    Default: ``NULL``
    Reloadable.
    The header on which Traffic Server varies for anything other than
    text and images.

Customizable User Response Pages
================================

``proxy.config.body_factory.enable_customizations``
{#proxy.config.body_factory.enable_customizations}
    ``INT``
    Default: ``0``
    Specifies whether customizable response pages are enabled or
    disabled and which response pages are used:

    -  ``0`` = disable customizable user response pages
    -  ``1`` = enable customizable user response pages in the default
       directory only
    -  ``2`` = enable language-targeted user response pages

``proxy.config.body_factory.enable_logging``
{#proxy.config.body_factory.enable_logging}
    ``INT``
    Default: ``1``
    Enables (``1``) or disables (``0``) logging for customizable
    response pages. When enabled, Traffic Server records a message in
    the error log each time a customized response page is used or
    modified.

``proxy.config.body_factory.template_sets_dir``
{#proxy.config.body_factory.template_sets_dir}
    ``STRING``
    Default: ``config/body_factory``
    The customizable response page default directory.

``proxy.config.body_factory.response_suppression_mode``
{#proxy.config.body_factory.response_suppression_mode}
    ``INT``
    Default: ``0``
    Specifies when Traffic Server suppresses generated response pages:

    -  ``0`` = never suppress generated response pages
    -  ``1`` = always suppress generated response pages
    -  ``2`` = suppress response pages only for intercepted traffic

DNS
===

``proxy.config.dns.search_default_domains``
{#proxy.config.dns.search_default_domains}
    ``INT``
    Default: ``1``
    Reloadable.
    Enables (``1``) or disables (``0``) local domain expansion so that
    Traffic Server can attempt to resolve unqualified hostnames by
    expanding to the local domain. For example: if a client makes a
    request to an unqualified host (``host_x``) and the Traffic Server
    local domain is ``y.com`` , then Traffic Server will expand the
    hostname to ``host_x.y.com``.

``proxy.config.dns.splitDNS.enabled``
{#proxy.config.dns.splitDNS.enabled}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) DNS server selection. When
    enabled, Traffic Server refers to the ``splitdns.config`` file for
    the selection specification. Refer to ``Configuring DNS Server
    Selection (Split
    DNS) <../security-options#ConfiguringDNSServerSelectionSplit>``_.

``proxy.config.dns.url_expansions``
{#proxy.config.dns.url_expansions}
    ``STRING``
    Default: ``NULL``
    Specifies a list of hostname extensions that are automatically added
    to the hostname after a failed lookup. For example: if you want
    Traffic Server to add the hostname extension .org, then specify
    ``org`` as the value for this variable (Traffic Server automatically
    adds the dot (.)).

    **Note:** If the variable
    ``proxy.config.http.enable_url_expandomatic`` is set to ``1`` (the
    default value), then you do not have to add *``www.``* and
    *``.com``* to this list because Traffic Server automatically tries
    www. and .com after trying the values you've specified.

``proxy.config.dns.resolv_conf`` {#proxy.config.dns.resolv_conf}
    ``STRING``
    Default: ``/etc/resolv.conf``
    Allows to specify which ``resolv.conf`` file to use for finding
    resolvers. While the format of this file must be the same as the
    standard ``resolv.conf`` file, this option allows an administrator
    to manage the set of resolvers in an external configuration file,
    without affecting how the rest of the operating system uses DNS.

``proxy.config.dns.round_robin_nameservers``
{#proxy.config.dns.round_robin_nameservers}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) DNS server round-robin.

``proxy.config.dns.nameservers`` {#proxy.config.dns.nameservers}
    ``STRING``
    Default: ``NULL``
    Reloadable.
    The DNS servers.

``proxy.config.srv_enabled`` {#proxy.config.srv_enabled}
    ``INT``
    Default: ``0``
    Reloadable.
    Indicates whether to use SRV records for orgin server lookup.

HostDB
======

``proxy.config.hostdb.serve_stale_for``
{#proxy.config.hostdb.serve_stale_for} : ``INT`` : Default: ``0`` :
The number of seconds for which to use a stale NS record while
initiating a background fetch for the new data.

``proxy.config.hostdb.storage_size``
{#proxy.config.hostdb.storage_size} : ``INT`` : Default: ``33554432`` :
The amount of space (in bytes) used to store ``hostdb``. Thevalue of
this variable must be increased if you increase the sizeof the
``proxy.config.hostdb.size`` variable.

``proxy.config.hostdb.size`` {#proxy.config.hostdb.size} : ``INT`` :
Default: ``200000`` : The maximum number of entries allowed in the host
database.

**Note:** For values above ``200000``, you must increase the value ofthe
``proxy.config.hostdb.storage_size`` variable by at least44 bytes per
entry.

``proxy.config.hostdb.ttl_mode`` {#proxy.config.hostdb.ttl_mode}
    ``INT``
    Default: ``0``
    Reloadable.
    The host database time to live mode. You can specify one of the
    following:

    -  ``0`` = obey
    -  ``1``\ = ignore
    -  ``2`` = min(X,ttl)
    -  ``3`` = max(X,ttl)

``proxy.config.hostdb.timeout`` {#proxy.config.hostdb.timeout}
    ``INT``
    Default: ``1440``
    Reloadable.
    The foreground timeout (in minutes).

``proxy.config.hostdb.strict_round_robin``
{#proxy.config.hostdb.strict_round_robin}
    ``INT``
    Default: ``0``
    Reloadable.
    When disabled (``0``), Traffic Server always uses the same origin
    server for the same client, for as long as the origin server is
    available.

Logging Configuration
=====================

``proxy.config.log.logging_enabled``
{#proxy.config.log.logging_enabled}
    ``INT``
    Default: ``3``
    Reloadable.
    Enables and disables event logging:

    -  ``0`` = logging disabled
    -  ``1`` = log errors only
    -  ``2`` = log transactions only
    -  ``3`` = full logging (errors + transactions)

    Refer to ``Working with Log Files <../working-log-files>``_.

``proxy.config.log.max_secs_per_buffer``
{#proxy.config.log.max_secs_per_buffer}
    ``INT``
    Default: ``5``
    Reloadable.
    The maximum amount of time before data in the buffer is flushed to
    disk.

``proxy.config.log.max_space_mb_for_logs``
{#proxy.config.log.max_space_mb_for_logs}
    ``INT``
    Default: ``2000``
    Reloadable.
    The amount of space allocated to the logging directory (in MB).
    **Note:** All files in the logging directory contribute to the space
    used, even if they are not log files. In collation client mode, if
    there is no local disk logging, or max_space_mb_for_orphan_logs
    is set to a higher value than max_space_mb_for_logs, TS will
    take proxy.config.log.max_space_mb_for_orphan_logs for maximum
    allowed log space.

``proxy.config.log.max_space_mb_for_orphan_logs``
{#proxy.config.log.max_space_mb_for_orphan_logs}
    ``INT``
    Default: ``25``
    Reloadable.
    The amount of space allocated to the logging directory (in MB) if
    this node is acting as a collation client. **Note:** When
    max_space_mb_for_orphan_logs is take as the maximum allowedlog
    space in the logging system, the same rule apply to
    proxy.config.log.max_space_mb_for_logs also apply to
    proxy.config.log.max_space_mb_for_orphan_logs, ie: All files in
    the logging directory contribute to the space used, even if they are
    not log files. you may need to consider this when you enable full
    remote logging, and bump to the same size as
    proxy.config.log.max_space_mb_for_logs.

``proxy.config.log.max_space_mb_headroom``
{#proxy.config.log.max_space_mb_headroom}
    ``INT``
    Default: ``10``
    Reloadable.
    The tolerance for the log space limit (in bytes). If the variable
    ``proxy.config.log.auto_delete_rolled_file`` is set to ``1``
    (enabled), then autodeletion of log files is triggered when the
    amount of free space available in the logging directory is less than
    the value specified here.

``proxy.config.log.hostname`` {#proxy.config.log.hostname}
    ``STRING``
    Default: ``localhost``
    Reloadable.
    The hostname of the machine running Traffic Server.

``proxy.config.log.logfile_dir`` {#proxy.config.log.logfile_dir}
    ``STRING``
    Default: ``install_dir``\ ``/logs``
    Reloadable.
    The full path to the logging directory. This can be an absolute path
    or a path relative to the directory in which Traffic Server is
    installed. **Note:** The directory you specify must already exist.

``proxy.config.log.logfile_perm`` {#proxy.config.log.logfile_perm}
    ``STRING``
    Default: ``rw-r--r--``
    Reloadable.
    The log file permissions. The standard UNIX file permissions are
    used (owner, group, other). Permissible values are:

    ``-``\ no permission ``r``\ read permission ``w``\ write permission
    ``x``\ execute permission

    Permissions are subject to the umask settings for the Traffic Server
    process. This means that a umask setting of\ ``002``\ will not allow
    write permission for others, even if specified in the configuration
    file. Permissions for existing log files are not changed when the
    configuration is changed.

``proxy.config.log.custom_logs_enabled``
{#proxy.config.log.custom_logs_enabled}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) custom logging.

``proxy.config.log.squid_log_enabled``
{#proxy.config.log.squid_log_enabled}
    ``INT``
    Default: ``1``
    Reloadable.
    Enables (``1``) or disables (``0``) the ``squid log file
    format <../working-log-files/log-formats#SquidFormat>``_.

``proxy.config.log.squid_log_is_ascii``
{#proxy.config.log.squid_log_is_ascii}
    ``INT``
    Default: ``1``
    Reloadable.
    The squid log file type:

    -  ``1`` = ASCII
    -  ``0`` = binary

``proxy.config.log.squid_log_name``
{#proxy.config.log.squid_log_name}
    ``STRING``
    Default: ``squid``
    Reloadable.
    The ``squid log <../working-log-files/log-formats#SquidFormat>``_
    filename.

``proxy.config.log.squid_log_header``
{#proxy.config.log.squid_log_header} : ``STRING`` : Default: ``NULL``
: The ``squid log <../working-log-files/log-formats#SquidFormat>``_ file
header text.

``proxy.config.log.common_log_enabled``
{#proxy.config.log.common_log_enabled}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) the ``Netscape common log file
    format <../working-log-files/log-formats#NetscapeFormats>``_.

``proxy.config.log.common_log_is_ascii``
{#proxy.config.log.common_log_is_ascii}
    ``INT``
    Default: ``1``
    Reloadable.
    The ``Netscape common
    log <../working-log-files/log-formats#NetscapeFormats>``_ file type:

    -  ``1`` = ASCII
    -  ``0`` = binary

``proxy.config.log.common_log_name``
{#proxy.config.log.common_log_name}
    ``STRING``
    Default: ``common``
    Reloadable.
    The ``Netscape common
    log <../working-log-files/log-formats#NetscapeFormats>``_ filename.

``proxy.config.log.common_log_header``
{#proxy.config.log.common_log_header}
    ``STRING``
    Default: ``NULL``
    Reloadable.
    The ``Netscape common
    log <../working-log-files/log-formats#NetscapeFormats>``_ file header
    text.

``proxy.config.log.extended_log_enabled``
{#proxy.config.log.extended_log_enabled}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) the ``Netscape extended log file
    format <../working-log-files/log-formats#NetscapeFormats>``_.

``proxy.confg.log.extended_log_is_ascii``
{#proxy.confg.log.extended_log_is_ascii}
    ``INT``
    Default: ``1``
    The ``Netscape extended
    log <../working-log-files/log-formats#NetscapeFormats>``_ file type:

    -  ``1`` = ASCII
    -  ``0`` = binary

``proxy.config.log.extended_log_name``
{#proxy.config.log.extended_log_name}
    ``STRING``
    Default: ``extended``
    The ``Netscape extended
    log <../working-log-files/log-formats#NetscapeFormats>``_ filename.

``proxy.config.log.extended_log_header``
{#proxy.config.log.extended_log_header}
    ``STRING``
    Default: ``NULL``
    Reloadable.
    The ``Netscape extended
    log <../working-log-files/log-formats#NetscapeFormats>``_ file header
    text.

``proxy.config.log.extended2_log_enabled``
{#proxy.config.log.extended2_log_enabled}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) the ``Netscape Extended-2 log
    file format <../working-log-files/log-formats#NetscapeFormats>``_.

``proxy.config.log.extended2_log_is_ascii``
{#proxy.config.log.extended2_log_is_ascii}
    ``INT``
    Default: ``1``
    Reloadable.
    The ``Netscape Extended-2
    log <../working-log-files/log-formats#NetscapeFormats>``_ file type:

    -  ``1`` = ASCII
    -  ``0`` = binary

``proxy.config.log.extended2_log_name``
{#proxy.config.log.extended2_log_name}
    ``STRING``
    Default: ``extended2``
    Reloadable.
    The ``Netscape Extended-2
    log <../working-log-files/log-formats#NetscapeFormats>``_ filename.

``proxy.config.log.extended2_log_header``
{#proxy.config.log.extended2_log_header}
    ``STRING``
    Default: ``NULL``
    Reloadable.
    The ``Netscape Extended-2
    log <../working-log-files/log-formats#NetscapeFormats>``_ file header
    text.

``proxy.config.log.separate_icp_logs``
{#proxy.config.log.separate_icp_logs}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), configures Traffic Server to store ICP
    transactions in a separate log file.

    -  ``0`` = separation is disabled, all ICP transactions are recorded
       in the same file as HTTP transactions
    -  ``1`` = all ICP transactions are recorded in a separate log file.
    -  ``-1`` = filter all ICP transactions from the default log files;
       ICP transactions are not logged anywhere.

``proxy.config.log.separate_host_logs``
{#proxy.config.log.separate_host_logs}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), configures Traffic Server to create a separate
    log file for HTTP transactions for each origin server listed in the
    ``log_hosts.config`` file. Refer to ``HTTP Host Log
    Splitting <../working-log-files#HTTPHostLogSplitting>``_.

``proxy.local.log.collation_mode`` {#proxy.local.log.collation_mode}
    ``INT``
    Default: ``0``
    Reloadable.
    The log collation mode:

    -  ``0`` = collation is disabled
    -  ``1`` = this host is a log collation server
    -  ``2`` = this host is a collation client and sends entries using
       standard formats to the collation server
    -  ``3`` = this host is a collation client and sends entries using
       the traditional custom formats to the collation server
    -  ``4`` = this host is a collation client and sends entries that
       use both the standard and traditional custom formats to the
       collation server

    For information on sending XML-based custom formats to the collation
    server, refer to ``logs_xml.config <logs_xml.config>``_.

    **Note:** Although Traffic Server supports traditional custom
    logging, you should use the more versatile XML-based custom formats.

``proxy.confg.log.collation_host`` {#proxy.confg.log.collation_host}
    ``STRING``
    Default: ``NULL``
    The hostname of the log collation server.

``proxy.config.log.collation_port``
{#proxy.config.log.collation_port}
    ``INT``
    Default: ``8085``
    Reloadable.
    The port used for communication between the collation server and
    client.

``proxy.config.log.collation_secret``
{#proxy.config.log.collation_secret}
    ``STRING``
    Default: ``foobar``
    Reloadable.
    The password used to validate logging data and prevent the exchange
    of unauthorized information when a collation server is being used.

``proxy.config.log.collation_host_tagged``
{#proxy.config.log.collation_host_tagged}
    ``INT``
    Default: ``0``
    Reloadable.
    When enabled (``1``), configures Traffic Server to include the
    hostname of the collation client that generated the log entry in
    each entry.

``proxy.config.log.collation_retry_sec``
{#proxy.config.log.collation_retry_sec}
    ``INT``
    Default: ``5``
    Reloadable.
    The number of seconds between collation server connection retries.

``proxy.config.log.rolling_enabled``
{#proxy.config.log.rolling_enabled}
    ``INT``
    Default: ``1``
    Reloadable.
    Specifies how log files are rolled. You can specify the following
    values:

    -  ``0`` = disables log file rolling
    -  ``1`` = enables log file rolling at specific intervals during the
       day (specified with the
       ``proxy.config.log.rolling_interval_sec`` and
       ``proxy.config.log.rolling_offset_hr`` variables)
    -  ``2`` = enables log file rolling when log files reach a specific
       size (specified with the ``proxy.config.log.rolling_size_mb``
       variable)
    -  ``3`` = enables log file rolling at specific intervals during the
       day or when log files reach a specific size (whichever occurs
       first)
    -  ``4`` = enables log file rolling at specific intervals during the
       day when log files reach a specific size (i.e., at a specified
       time if the file is of the specified size)

``proxy.config.log.rolling_interval_sec``
{#proxy.config.log.rolling_interval_sec}
    ``INT``
    Default: ``86400``
    Reloadable.
    The log file rolling interval, in seconds. The minimum value is
    ``300`` (5 minutes). The maximum value is 86400 seconds (one day).

    **Note:** If you start Traffic Server within a few minutes of the
    next rolling time, then rolling might not occur until the next
    rolling time.

``proxy.config.log.rolling_offset_hr``
{#proxy.config.log.rolling_offset_hr}
    ``INT``
    Default: ``0``
    Reloadable.
    The file rolling offset hour. The hour of the day that starts the
    log rolling period.

``proxy.config.log.rolling_size_mb``
{#proxy.config.log.rolling_size_mb}
    ``INT``
    Default: ``10``
    Reloadable.
    The size that log files must reach before rolling takes place.

``proxy.config.log.auto_delete_rolled_files``
{#proxy.config.log.auto_delete_rolled_files}
    ``INT``
    Default: ``1``
    Reloadable.
    Enables (``1``) or disables (``0``) automatic deletion of rolled
    files.

``proxy.config.log.sampling_frequency``
{#proxy.config.log.sampling_frequency}
    ``INT``
    Default: ``1``
    Reloadable.
    Configures Traffic Server to log only a sample of transactions
    rather than every transaction. You can specify the following values:

    -  ``1`` = log every transaction
    -  ``2`` = log every second transaction
    -  ``3`` = log every third transaction and so on...

``proxy.config.http.slow.log.threshold``
{#proxy.config.http.slow.log.threshold}
    ``INT``
    Default: ``0``
    Reloadable.
    The number of milliseconds before a slow connection's debugging
    stats are dumped. Specify ``1`` to enable or ``0`` to disable.

Diagnostic Logging Configuration
================================

``proxy.config.diags.output.diag`` {#proxy.config.diags.output.diag}
``proxy.config.diags.output.debug`` {#proxy.config.diags.output.debug}
``proxy.config.diags.output.status``
{#proxy.config.diags.output.status} ``proxy.config.diags.output.note``
{#proxy.config.diags.output.note}
``proxy.config.diags.output.warning``
{#proxy.config.diags.output.warning}
``proxy.config.diags.output.error`` {#proxy.config.diags.output.error}
``proxy.config.diags.output.fatal`` {#proxy.config.diags.output.fatal}
``proxy.config.diags.output.alert`` {#proxy.config.diags.output.alert}
``proxy.config.diags.output.emergency``
{#proxy.config.diags.output.emergency} : ``STRING`` : These options
control where Traffic Server should log diagnostic output. Messages at
diagnostic level can be directed to any combination of diagnostic
destinations. Valid diagnostic message destinations are:

::

    * 'O' = Log to standard output
    * 'E' = Log to standard error
    * 'S' = Log to syslog
    * 'L' = Log to diags.log

    For example, to log debug diagnostics to both syslog and diags.log:

        proxy.config.diags.output.debug STRING SL

Reverse Proxy
=============

``proxy.config.reverse_proxy.enabled``
{#proxy.config.reverse_proxy.enabled}
    ``INT``
    Default: ``1``
    Reloadable.
    Enables (``1``) or disables (``0``) HTTP reverse proxy.

``proxy.config.header.parse.no_host_url_redirect``
{#proxy.config.header.parse.no_host_url_redirect}
    ``STRING``
    Default: ``NULL``
    Reloadable.
    The URL to which to redirect requests with no host headers (reverse
    proxy).

URL Remap Rules
===============

``proxy.config.url_remap.default_to_server_pac``
{#proxy.config.url_remap.default_to_server_pac}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) requests for a PAC file on the
    proxy service port (8080 by default) to be redirected to the PAC
    port. For this type of redirection to work, the variable
    ``proxy.config.reverse_proxy.enabled`` must be set to ``1``.

``proxy.config.url_remap.default_to_server_pac_port``
{#proxy.config.url_remap.default_to_server_pac_port}
    ``INT``
    Default: ``-1``
    Reloadable.
    Sets the PAC port so that PAC requests made to the Traffic Server
    proxy service port are redirected this port. ``-1`` is the default
    setting that sets the PAC port to the autoconfiguration port (the
    default autoconfiguration port is 8083). This variable can be used
    together with the ``proxy.config.url_remap.default_to_server_pac``
    variable to get a PAC file from a different port. You must create
    and run a process that serves a PAC file on this port. For example:
    if you create a Perl script that listens on port 9000 and writes a
    PAC file in response to any request, then you can set this variable
    to ``9000``. Browsers that request the PAC file from a proxy server
    on port 8080 will get the PAC file served by the Perl script.

``proxy.config.url_remap.remap_required``
{#proxy.config.url_remap.remap_required}
    ``INT``
    Default: ``1``
    Reloadable.
    Set this variable to ``1`` if you want Traffic Server to serve
    requests only from origin servers listed in the mapping rules of the
    ``remap.config`` file. If a request does not match, then the browser
    will receive an error.

``proxy.config.url_remap.pristine_host_hdr``
{#proxy.config.url_remap.pristine_host_hdr}
    ``INT``
    Default: ``1``
    Reloadable.
    Set this variable to ``1`` if you want to retain the client host
    header in a request during remapping.

SSL Termination
===============

``proxy.config.ssl.accelerator_required``
{#proxy.config.ssl.accelerator_required}
    ``INT``
    Default: ``0``
    Indicates if an accelerator card is required for operation. Traffic
    Server supports Cavium accelerator cards.

    You can specify:

    -  ``0`` - not required
    -  ``1`` - accelerator card is required and Traffic Server will not
       enable SSL unless an accelerator card is present.
    -  ``2`` - accelerator card is required and Traffic Server will not
       start unless an accelerator card is present.

    You can verify operation by
    running\ ``/home/y/bin/openssl_accelerated`` (this comes as part of
    ``openssl_engines_init``).

``proxy.config.ssl.enabled`` {#proxy.config.ssl.enabled}
    ``INT``
    Default: ``0``
    Enables (``1``) or disables (``0``) the ``SSL
    Termination <../security-options#UsingSSLTermination>``_ option.

``proxy.config.ssl.SSLv2`` {#proxy.config.ssl.SSLv2}
    ``INT``
    Default: ``1``
    Enables (``1``) or disables (``0``) SSLv2. Please disable it.

``proxy.config.ssl.SSLv3`` {#proxy.config.ssl.SSLv3}
    ``INT``
    Default: ``1``
    Enables (``1``) or disables (``0``) SSLv3.

``proxy.config.ssl.TLSv1`` {#proxy.config.ssl.TLSv1}
    ``INT``
    Default: ``1``
    Enables (``1``) or disables (``0``) TLSv1.

``proxy.confg.ssl.accelerator.type``
{#proxy.confg.ssl.accelerator.type}
    ``INT``
    Default: ``0``
    Specifies if the Cavium SSL accelerator card is installed on (and
    required by) your Traffic Server machine:

    -  ``0`` = none (no SSL accelerator card is installed on the Traffic
       Server machine, so the Traffic Server's CPU determines the number
       of requests served per second)
    -  ``1`` = accelerator card is present and required by Traffic
       Server

``proxy.config.ssl.server_port`` {#proxy.config.ssl.server_port}
    ``INT``
    Default: ``443``
    The port used for SSL communication.

``proxy.config.ssl.client.certification_level``
{#proxy.config.ssl.client.certification_level}
    ``INT``
    Default: ``0``
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

``proxy.config.ssl.server.cert.filename``
{#proxy.config.ssl.server.cert.filename}
    ``STRING``
    Default: ``server.pem``
    The filename of the Traffic Server SSL certificate (the server
    certificate).

``proxy.config.ssl.server.cert_chain.filename``
{#proxy.config.ssl.server.cert_chain.filename}
    ``STRING``
    Default: ``NULL``
    The file, in a chain of certificates, that is the root certificate
    recognized by your website.

``proxy.config.ssl.server.cert.path``
{#proxy.config.ssl.server.cert.path}
    ``STRING``
    Default: ``/config``
    The location of the Traffic Server SSL certificate (the server
    certificate).

``proxy.config.ssl.server.private_key.filename``
{#proxy.config.ssl.server.private_key.filename}
    ``STRING``
    Default: ``NULL``
    The filename of the Traffic Server private key. Change this variable
    only if the private key is not located in the Traffic Server SSL
    certificate file.

``proxy.config.ssl.server.private_key.path``
{#proxy.config.ssl.server.private_key.path}
    ``STRING``
    Default: ``NULL``
    The location of the Traffic Server private key. Change this variable
    only if the private key is not located in the SSL certificate file.

``proxy.config.ssl.CA.cert.filename``
{#proxy.config.ssl.CA.cert.filename}
    ``STRING``
    Default: ``NULL``
    The filename of the certificate authority that client certificates
    will be verified against.

``proxy.config.ssl.CA.cert.path`` {#proxy.config.ssl.CA.cert.path}
    ``STRING``
    Default: ``NULL``
    The location of the certificate authority file that client
    certificates will be verified against.

Client-Related Configuration
----------------------------

``proxy.config.ssl.client.verify.server``
{#proxy.config.ssl.client.verify.server}
    ``INT``
    Default: ``0``
    Configures Traffic Server to verify the origin server certificate
    with the Certificate Authority (CA).

``proxy.config.ssl.client.cert.filename``
{#proxy.config.ssl.client.cert.filename}
    ``STRING``
    Default: ``NULL``
    The filename of SSL client certificate installed on Traffic Server.

``proxy.config.ssl.client.cert.path``
{#proxy.config.ssl.client.cert.path}
    ``STRING``
    Default: ``/config``
    The location of the SSL client certificate installed on Traffic
    Server.

``proxy.config.ssl.client.private_key.filename``
{#proxy.config.ssl.client.private_key.filename}
    ``STRING``
    Default: ``NULL``
    The filename of the Traffic Server private key. Change this variable
    only if the private key is not located in the Traffic Server SSL
    client certificate file.

``proxy.config.ssl.client.private_key.path``
{#proxy.config.ssl.client.private_key.path}
    ``STRING``
    Default: ``NULL``
    The location of the Traffic Server private key. Change this variable
    only if the private key is not located in the SSL client certificate
    file.

``proxy.config.ssl.client.CA.cert.filename``
{#proxy.config.ssl.client.CA.cert.filename}
    ``STRING``
    Default: ``NULL``
    The filename of the certificate authority against which the origin
    server will be verified.

``proxy.config.ssl.client.CA.cert.path``
{#proxy.config.ssl.client.CA.cert.path}
    ``STRING``
    Default: ``NULL``
    Specifies the location of the certificate authority file against
    which the origin server will be verified.

ICP Configuration
=================

``proxy.config.icp.enabled`` {#proxy.config.icp.enabled}
    ``INT``
    Default: ``0``
    Sets ICP mode for hierarchical caching:

    -  ``0`` = disables ICP
    -  ``1`` = allows Traffic Server to receive ICP queries only
    -  ``2`` = allows Traffic Server to send and receive ICP queries

    Refer to ``ICP Peering <../hierachical-caching#ICPPeering>``_.

``proxy.config.icp.icp_interface`` {#proxy.config.icp.icp_interface}
    ``STRING``
    Default: ``your_interface``
    Specifies the network interface used for ICP traffic.

    **Note:** The Traffic Server installation script detects your
    network interface and sets this variable appropriately. If your
    system has multiple network interfaces, check that this variable
    specifies the correct interface.

``proxy.config.icp.icp_port`` {#proxy.config.icp.icp_port}
    ``INT``
    Default: ``3130``
    Reloadable.
    Specifies the UDP port that you want to use for ICP messages.

``proxy.config.icp.query_timeout`` {#proxy.config.icp.query_timeout}
    ``INT``
    Default: ``2``
    Reloadable.
    Specifies the timeout used for ICP queries.

Scheduled Update Configuration
==============================

``proxy.config.update.enabled`` {#proxy.config.update.enabled}
    ``INT``
    ``0``
    Enables (``1``) or disables (``0``) the Scheduled Update option.

``proxy.config.update.force`` {#proxy.config.update.force}
    ``INT``
    Default: ``0``
    Reloadable.
    Enables (``1``) or disables (``0``) a force immediate update. When
    enabled, Traffic Server overrides the scheduling expiration time for
    all scheduled update entries and initiates updates until this option
    is disabled.

``proxy.config.update.retry_count``
{#proxy.config.update.retry_count}
    ``INT``
    Default: ``10``
    Reloadable.
    Specifies the number of times Traffic Server can retry the scheduled
    update of a URL in the event of failure.

``proxy.config.update.retry_interval``
{#proxy.config.update.retry_interval}
    ``INT``
    Default: ``2``
    Reloadable.
    Specifies the delay (in seconds) between each scheduled update retry
    for a URL in the event of failure.

``proxy.config.update.concurrent_updates``
{#proxy.config.update.concurrent_updates}
    ``INT``
    Default: ``100``
    Reloadable.
    Specifies the maximum simultaneous update requests allowed at any
    time. This option prevents the scheduled update process from
    overburdening the host.

Remap Plugin Processor
======================

``proxy.config.remap.use_remap_processor``
{#proxy.config.remap.use_remap_processor}
    ``INT``
    Default: ``0``
    Enables (``1``) or disables (``0``) the ability to run separate
    threads for remap plugin processing.

``proxy.config.remap.num_remap_threads``
{#proxy.config.remap.num_remap_threads}
    ``INT``
    Default: ``1``
    Specifies the number of threads that will be used for remap plugin
    processing.

Plug-in Configuration
=====================

``proxy.config.plugin.plugin_dir`` {#proxy.config.plugin.plugin_dir}
    ``STRING``
    Default: ``config/plugins``
    Specifies the location of Traffic Server plugins.

Sockets
=======

``proxy.config.net.defer_accept`` {#proxy.config.net.defer_accept}
    ``INT``
    Default: default: ``1`` meaning ``on`` all Platforms except Linux:
    ``45`` seconds
    This directive enables operating system specific optimizations for a
    listening socket. ``defer_accept`` holds a call to ``accept(2)``
    back until data has arrived. In Linux' special case this is up to a
    maximum of 45 seconds.

``proxy.config.net.sock_send_buffer_size_in``
{#proxy.config.net.sock_send_buffer_size_in}
    ``INT``
    Default: ``0``
    Sets the send buffer size for connections from the client to Traffic
    Server.

``proxy.config.net.sock_recv_buffer_size_in``
{#proxy.config.net.sock_recv_buffer_size_in}
    ``INT``
    Default: ``0``
    Sets the receive buffer size for connections from the client to
    Traffic Server.

``proxy.config.net.sock_option_flag_in``
{#proxy.config.net.sock_option_flag_in}
    ``INT``
    Default: ``0``
    Turns different options "on" for the socket handling client
    connections:

    ::

        TCP_NODELAY (1)
        SO_KEEPALIVE (2)

    **Note:** This is a flag and you look at the bits set. Therefore,
    you must set the value to ``3`` if you want to enable both options
    above.

``proxy.config.net.sock_send_buffer_size_out``
{#proxy.config.net.sock_send_buffer_size_out}
    ``INT``
    Default: ``0``
    Sets the send buffer size for connections from Traffic Server to the
    origin server.

.. _proxy.config.net.sock_recv_buffer_size_out:
``proxy.config.net.sock_recv_buffer_size_out``
    ``INT``
    Default: ``0``
    Sets the receive buffer size for connections from Traffic Server to
    the origin server.

.. _proxy.config.net.sock_option_flag_out:
``proxy.config.net.sock_option_flag_out``
    ``INT``
    Default: ``0``
    Turns different options "on" for the origin server socket:

    ::

        TCP_NODELAY (1)
        SO_KEEPALIVE (2)

    **Note:** This is a flag and you look at the bits set. Therefore,
    you must set the value to ``3``\ if you want to enable both options
    above.

``proxy.config.net.sock_mss_in``
    ``INT``
    Default: ``0``
    Same as the command line option ``--accept_mss`` that sets the MSS
    for all incoming requests.


