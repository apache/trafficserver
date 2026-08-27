.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements. See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership. The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License. You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing,
   software distributed under the License is distributed on an
   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
   KIND, either express or implied. See the License for the
   specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. _admin-plugins-abuse-shield:

Abuse Shield Plugin
*******************

The abuse_shield global plugin provides consolidated abuse protection using
bounded per-client state and token-bucket rate limiting. It supports:

* per-IP request, connection, and HTTP/2 error token buckets;
* trusted IP ranges and rule-specific rate-limit tiers;
* logging, temporary IP blocking, and connection closing;
* atomic rule reloads through traffic_ctl; and
* optional ClientHello fingerprint matching before the TLS handshake continues.

Building and Loading
====================

Enable the experimental plugin and build it with CMake::

   cmake -B build -DENABLE_ABUSE_SHIELD=ON
   cmake --build build --target abuse_shield

Add the plugin and its YAML configuration to plugin.config::

   abuse_shield.so abuse_shield.yaml

A relative configuration path is resolved from the |TS| configuration
directory.

Fingerprint rules also require global JAx instances before Abuse Shield. Each
method publishes to the registry named by ``global.fingerprint_registry``::

   jax_fingerprint.so --method JA3 --export abuse_shield.fingerprints
   jax_fingerprint.so --method JA4 --export abuse_shield.fingerprints
   abuse_shield.so abuse_shield.yaml

Configuration
=============

The following example configures several available abuse controls:

.. code-block:: yaml

   global:
     ip_tracking:
       slots: 50000
     blocking:
       duration_seconds: 300
     trusted_ips_file: /etc/trafficserver/abuse_shield_trusted.yaml
     log_interval_sec: 10
     log_file: abuse_shield
     fingerprint_registry: abuse_shield.fingerprints

   rules:
     - name: excessive_requests
       filter:
         max_req_rate: 100
         req_burst_multiplier: 2.0
       action: [log, block, close]

     - name: excessive_connections
       filter:
         max_conn_rate: 20
         conn_burst_multiplier: 1.5
       action: [log, block, close]

     - name: excessive_h2_errors
       filter:
         max_h2_error_rate: 10
       action: [log, block, close]

     - name: blocked_tls_clients
       filter:
         fingerprints:
           JA3:
             - "238bcebdfa16aa0be417a7f7a80063a9"
             - "99c071c5a5e14cc2527c9e8e0dde4a50"
           JA4:
             - "t13d1516h2_8daaf6152771_02713d6af862"
       action: [log, close]

   enabled: true

Global Settings
---------------

===================================== ==============================================
Setting                               Description
===================================== ==============================================
ip_tracking.slots                     Slots in each bounded IP table (default 50000)
blocking.duration_seconds             Duration of a block action (default 300)
trusted_ips_file                      Optional YAML file of IP ranges to bypass
log_interval_sec                      Minimum log interval per IP (default 10)
log_file                              Optional separate |TS| text log object
fingerprint_registry                  Named JAx registry used by fingerprint rules
===================================== ==============================================

Rules are evaluated in file order and the first matching rule wins.

Rule Filters
------------

===================================== ==============================================
Filter                                Description
===================================== ==============================================
max_req_rate                          Maximum requests per second
req_burst_multiplier                  Request bucket capacity multiplier
max_conn_rate                         Maximum connections per second
conn_burst_multiplier                 Connection bucket capacity multiplier
max_h2_error_rate                     Maximum HTTP/2 errors per second
h2_burst_multiplier                   HTTP/2 error bucket capacity multiplier
rate_limited_ips_file                 Optional IP list that selects this rule
fingerprints                          Map of method names to fingerprint sequences
===================================== ==============================================

Burst multipliers default to 1.0 and must be at least 1.0. Every rule has an
independent token bucket for each configured rate, so thresholds do not depend
on rule order. A rate is exceeded when its token bucket becomes negative.
Excess events add proportional token debt; the rule stops matching only after
the configured rate replenishes that debt. Entries with debt are protected
from bounded-table eviction.

All configured rate filters in a rule use AND logic. Fingerprint values use OR
logic across both values and methods, and that fingerprint result is ANDed with
the rule's rate filters. Use separate rules when OR behavior is needed between
rate limits.

ClientHello Fingerprints
------------------------

The supplied JAx methods are JA3 and JA4. Their method names are
case-insensitive. JA3 values are validated as 32 hexadecimal characters and
canonicalized to lowercase; JA4 values are validated against the 36-character
JA4 layout. Other method names and values are treated as opaque strings and
matched exactly, allowing downstream JAx builds to publish site-specific
methods.

Only methods derived entirely from a TLS ClientHello can be used. JA4H is
derived from an HTTP request and is therefore unavailable at the
TS_SSL_CLIENT_HELLO_HOOK where the plugin makes its decision.

JAx computes each fingerprint and publishes it in a versioned, read-only
registry held in a named VConn user-argument slot. Abuse Shield consumes that
result at its ClientHello hook; it does not link fingerprint algorithms or
recompute their values. The registry is an in-process array of
length-delimited method/value entries, not JSON. Its header contains a magic
value, ABI version, and structure sizes so a consumer can reject an
incompatible layout. JAx owns all registry memory for the VConn lifetime.

The JAx plugin lines must precede Abuse Shield in :file:`plugin.config` so
their hooks publish values before Abuse Shield evaluates them. A missing or
incompatible named registry is a startup error. A matching close action uses
``TSVConnReenableEx(vconn, TS_EVENT_ERROR)``, stopping processing before
ServerHello and key-exchange work.

Adding Fingerprint Methods
~~~~~~~~~~~~~~~~~~~~~~~~~~

Downstream builds add private or site-specific methods to JAx, following its
developer README, then load the method with the same export name::

   jax_fingerprint.so --method SITE_METHOD --export abuse_shield.fingerprints

Configure ``SITE_METHOD`` as a key under ``fingerprints``. Abuse Shield does
not need to know how the method is computed and compares the exported value
exactly. No JA-specific addition to ``ts.h`` or Abuse Shield provider code
is needed.

Actions
-------

=========== =================================================================
Action      Behavior
=========== =================================================================
log         Log the match, rate-limited per client IP
block       Mark the IP blocked for blocking.duration_seconds
close       Close the current connection
=========== =================================================================

For a fingerprint denylist, [log, close] rejects matching TLS connections
without changing the IP's future status. Add block when subsequent connections
from that IP should also be denied for the configured duration.

Trusted and Rate-Limited IP Files
---------------------------------

A trusted IP file bypasses all Abuse Shield checks:

.. code-block:: yaml

   trusted_ips:
     - 192.0.2.10
     - 198.51.100.0/24
     - 2001:db8::/32

A rule-specific rate tier uses rate_limited_ips_file and a rate_limited_ips
sequence:

.. code-block:: yaml

   rate_limited_ips:
     - 203.0.113.0/24
     - 2001:db8:1::/48

Single addresses, ranges, and CIDR blocks are supported. The legacy
trusted_ips key is also accepted in a rule-specific rate-limit file.

Runtime Control
===============

Reload the YAML rules without restarting |TS|::

   traffic_ctl plugin msg abuse_shield.reload

The new configuration is validated before it replaces the active
configuration. Existing table data and block expiration times are preserved,
as is the state set by ``abuse_shield.enabled``. ``ip_tracking.slots`` and
``log_file`` and ``fingerprint_registry`` are startup-only; a reload that
changes any of these settings is rejected. Fingerprint additions and removals
apply to the next ClientHello.

Other lifecycle messages are::

   traffic_ctl plugin msg abuse_shield.dump
   traffic_ctl plugin msg abuse_shield.stats
   traffic_ctl plugin msg abuse_shield.reset
   traffic_ctl plugin msg abuse_shield.enabled 0
   traffic_ctl plugin msg abuse_shield.enabled 1
   traffic_ctl plugin msg abuse_shield.trusted

Metrics
========

View metrics with traffic_ctl metric get abuse_shield.*.

================================================= =============================
Metric                                            Description
================================================= =============================
abuse_shield.rules.matched                        All rule matches
abuse_shield.actions.blocked                      Block actions
abuse_shield.actions.block_failed                 Block actions not stored
abuse_shield.actions.closed                       Close actions
abuse_shield.actions.close_failed                 Failed connection closes
abuse_shield.actions.logged                       Emitted log records
abuse_shield.connections.rejected                 Previously blocked IPs
abuse_shield.connections.reject_failed            Failed blocked-IP rejections
abuse_shield.fingerprints.matched                 Fingerprint rule matches
abuse_shield.fingerprints.rejected                ClientHello rejections
abuse_shield.fingerprints.unavailable             ClientHellos missing a configured fingerprint
abuse_shield.<tracker>.events                     Events for a tracker
abuse_shield.<tracker>.events_untracked           Events not admitted to a tracker
abuse_shield.<tracker>.scan_exhausted             Protected-slot scans that hit their bound
abuse_shield.<tracker>.slots_used                 Occupied bounded-table slots
abuse_shield.<tracker>.contests                   Table contests
abuse_shield.<tracker>.contests_won               Contests won by new IPs
abuse_shield.<tracker>.evictions                  Evicted IPs
================================================= =============================

The tracker name is txn, conn, or h2.

Memory Bound
============

Request, connection, and HTTP/2 state use separate private fixed-size tables
with ``ip_tracking.slots`` entries each. Block expirations use a separate
bounded table that never evicts an unexpired block. This keeps memory usage
bounded even when traffic contains many distinct source addresses.

New addresses compete for tracker slots. An ordinary contest loss increments
``events_untracked``. Entries with token debt are protected from eviction, and
the table probes at most 1024 candidates before incrementing
``scan_exhausted`` and leaving the event untracked. This bounds work while the
table mutex is held even if every probed entry has debt.

Connection rules are evaluated when a connection starts: TLS connections at
the TLS virtual-connection hook and plain HTTP connections at the HTTP session
hook. Request and HTTP/2 error rules are evaluated when their corresponding
event is recorded.
