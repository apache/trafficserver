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

.. _traffic_line:

traffic_line
************

.. note::

   This utility is deprecated as of v6.0.0, and replaced with
   :program:`traffic_ctl`. You should change your tools / scripts to use this
   new application instead.

Synopsis
========

:program:`traffic_line` [options]

Description
===========

:program:`traffic_line` is used to execute individual Traffic Server
commands and to script multiple commands in a shell.

Options
=======

.. program:: traffic_line

.. option:: -B, --bounce_cluster

    Bounce all Traffic Server nodes in the cluster. Bouncing Traffic
    Server shuts down and immediately restarts Traffic Server,
    node-by-node.

.. option:: -b, --bounce_local

    Bounce Traffic Server on the local node. Bouncing Traffic Server
    shuts down and immediately restarts the Traffic Server node.

.. option:: -C, --clear_cluster

    Clears accumulated statistics on all nodes in the cluster.

.. option:: -c, --clear_node

    Clears accumulated statistics on the local node.

.. option:: --drain

    This option modifies the behavior of :option:`traffic_line -b`
    and :option:`traffic_line -L` such that :program:`traffic_server`
    is not shut down until the number of active client connections
    drops to the number given by the
    :ts:cv:`proxy.config.restart.active_client_threshold` configuration
    variable.

.. option:: -h, --help

    Print usage information and exit.

.. option:: -L, --restart_local

    Restart the :program:`traffic_manager` and :program:`traffic_server`
    processes on the local node.

.. option:: -M, --restart_cluster

    Restart the :program:`traffic_manager` process and the
    :program:`traffic_server` process on all the nodes in a cluster.

.. option:: -m REGEX, --match_var REGEX

    Display the current values of all performance statistics or configuration
    variables whose names match the given regular expression.

.. option:: -r VAR, --read_var VAR

    Display specific performance statistics or a current configuration
    setting.

.. option:: -s VAR, --set_var VAR

    Set the configuration variable named *VAR*. The value of the configuration
    variable is given by the :option:`traffic_line -v` option.
    Refer to the :file:`records.config` documentation for a list
    of the configuration variables you can specify.

.. option:: -S, --shutdown

    Shut down Traffic Server on the local node.

.. option:: -U, --startup

    Start Traffic Server on the local node.

.. option:: -v VALUE, --value VALUE

    Specify the value to set when setting a configuration variable.

.. option:: -V, --version

    Print version information and exit.

.. option:: -x, --reread_config

    Initiate a Traffic Server configuration file reread. Use this
    command to update the running configuration after any configuration
    file modification.

    The timestamp of the last reconfiguration event (in seconds
    since epoch) is published in the `proxy.node.config.reconfigure_time`
    metric.

.. option:: -Z, --zero_cluster

    Reset performance statistics to zero across the cluster.

.. option:: -z, --zero_node

    Reset performance statistics to zero on the local node.

.. option:: --offline PATH

   Mark a cache storage device as offline. The storage is identified by a *path* which must match exactly a path
   specified in :file:`storage.config`. This removes the storage from the cache and redirects requests that would have
   used this storage to other storage. This has exactly the same effect as a disk failure for that storage. This does
   not persist across restarts of the :program:`traffic_server` process.

.. option:: --alarms

   List all alarm events that have not been acknowledged (cleared).

.. option:: --clear_alarms [all | #event | name]

   Clear (acknowledge) an alarm event. The arguments are "all" for all current
   alarms, a specific alarm number (e.g. ''1''), or an alarm string identifier
   (e.g. ''MGMT_ALARM_PROXY_CONFIG_ERROR'').

.. option:: --status

   Show the current proxy server status, indicating if we're running or not.

.. _traffic-line-performance-statistics:

Performance Statistics
======================

proxy.process.ssl.user_agent_other_errors
  Total number of *other* ssl client connection errors (counts ssl
  errors that are not captured in other user agent stats below)

proxy.process.ssl.user_agent_expired_cert
  Total number of ssl client connection failures where the cert was
  expired.

proxy.process.ssl.user_agent_revoked_cert
  Total number of ssl client connection failures where the cert was
  revoked.

proxy.process.ssl.user_agent_unknown_cert
  Total number of ssl client connection failures related to the cert,
  but specific error was unknown.

proxy.process.ssl.user_agent_cert_verify_failed
  Total number of ssl client connection failures where cert verification
  failed.

proxy.process.ssl.user_agent_bad_cert
  Total number of ssl client connection failures where the cert is bad.

proxy.process.ssl.user_agent_decryption_failed
  Total number of ssl client connection decryption failures (during
  negotiation).

proxy.process.ssl.user_agent_wrong_version
  Total number of ssl client connections that provided an invalid protocol
  version.

proxy.process.ssl.user_agent_unknown_ca
  Total number of ssl client connection that failed due to unknown ca.

proxy.process.ssl.origin_server_other_errors
  Total number of *other* ssl origin server connection errors (counts ssl
  errors that are not captured in other origin server stats below).

proxy.process.ssl.origin_server_expired_cert
  Total number of ssl origin server connection failures where the cert
  was expired.

proxy.process.ssl.origin_server_revoked_cert
  Total number of ssl origin server connection failures where the cert
  was revoked.

proxy.process.ssl.origin_server_unknown_cert
  Total number of ssl origin server connection failures related to the
  cert where specific error was unknown.

proxy.process.ssl.origin_server_cert_verify_failed
  Total number of ssl origin server connection failures where cert
  verification failed.

proxy.process.ssl.origin_server_bad_cert
  Total number of ssl origin server connection failures where the cert
  is bad.

proxy.process.ssl.origin_server_decryption_failed
  Total number of ssl origin server connection decryption failures
  (during negotiation).

proxy.process.ssl.origin_server_wrong_version
  Total number of ssl origin server connections that provided an invalid
  protocol version.

proxy.process.ssl.origin_server_unknown_ca
  Total number of ssl origin server connection that failed due to
  unknown ca.

proxy.process.ssl.user_agent_sessions
  Total number of ssl/tls sessions created.

proxy.process.ssl.user_agent_session_hit
  Total number of session hits.  A previous session was reused which
  resulted in an abbreviated ssl client negotiation.

proxy.process.ssl.user_agent_session_miss
  Total number of session misses.  The ssl client provided a session id
  that was not found in cache and, therefore, could not be used.

proxy.process.ssl.user_agent_session_timeout
  Total number of session timeouts.  The ssl client provided a session, but
  it could not be used because it was past the session timeout.

proxy.process.ssl.cipher.user_agent.{CIPHERNAME}
  Total number of ssl client connections that used cipherName.  The
  list of cipher statistics is dynamic and depends upon the installed
  ciphers and the :ts:cv:`proxy.config.ssl.server.cipher_suite`
  configuration. The set of cipher statistics can be discovered
  with :option:`traffic_line -m`. For example::

    $ traffic_line -m proxy.process.ssl.cipher.user_agent.
    proxy.process.ssl.cipher.user_agent.ECDHE-RSA-AES256-GCM-SHA384 0
    proxy.process.ssl.cipher.user_agent.ECDHE-ECDSA-AES256-GCM-SHA384 0
    proxy.process.ssl.cipher.user_agent.ECDHE-RSA-AES256-SHA384 0
    proxy.process.ssl.cipher.user_agent.ECDHE-ECDSA-AES256-SHA384 0
    ...

Cache Statistics
======================

Cache statistics come in two varieties, global and per cache volume. These will be listed here in the global form. To get a
cache volume statistic add `.volume_#` to the name after `cache` where `#` is 1-based index of the volume in :file:`storage.config`.
For example the statistic `proxy.process.cache.sync.bytes` is a global statistic. The value for the third cache volume is
`proxy.process.cache.volume_3.sync.bytes`.

proxy.process.cache.sync.bytes
   The total number of bytes written to disk to synchronize the cache directory.

proxy.process.cache.sync.time
   The total time, in nanoseconds, during which the cache directory was being written to disk.

proxy.process.cache.sync.count
   The number of times a cache directory sync has been done.

proxy.process.cache.wrap_count
   The number of times a cache stripe has cycled. Each stripe is a circular buffer and this is incremented each time the
   write cursor is reset to the start of the stripe.

Examples
========

Configure Traffic Server to log in Squid format::

    $ traffic_line -s proxy.config.log.squid_log_enabled -v 1
    $ traffic_line -s proxy.config.log.squid_log_is_ascii -v 1
    $ traffic_ctl config reload

See also
========

:manpage:`records.config(5)`,
:manpage:`storage.config(5)`
