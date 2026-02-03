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

.. _admin-plugins-abuse_shield:

Abuse Shield Plugin
*******************

Description
===========

The ``abuse_shield`` plugin provides consolidated abuse protection for Apache
Traffic Server using **token bucket rate limiting** for accurate per-second
rate control.

Key features:

* **Token Bucket algorithm** for accurate per-second rate limiting
* **Per-IP request rate limiting** - Block IPs exceeding requests/second
* **Per-IP connection rate limiting** - Block IPs opening too many connections/second
* **Per-IP H2 error rate limiting** - Block IPs generating too many HTTP/2 errors/second
* YAML based configuration, dynamically reloadable via ``traffic_ctl``
* IP allow-listing for trusted IPs (such as in-network IPs)
* Configurable actions: logging, blocking, connection closing

Algorithm
=========

The plugin uses the "Udi King of the Hill" algorithm for IP tracking. This is an
implementation of the Uid algorithm described in the now expired US Patent 7533414B1:

https://patents.google.com/patent/US7533414B1

This algorithm uses a table, the size of which is configurable. See the
``slots`` configuration below.

Installation
============

Build with CMake::

    cmake .. -DENABLE_ABUSE_SHIELD=ON
    cmake --build . --target abuse_shield

Configuration
=============

To enable the plugin, add it to :file:`plugin.config`::

    abuse_shield.so abuse_shield.yaml

Create the configuration file :file:`abuse_shield.yaml` in the config directory.

Configuration File Format
-------------------------

The configuration uses YAML format with the following structure:

.. code-block:: yaml

    global:
      ip_tracking:
        slots: 10000              # Number of IP tracking slots per tracker

      blocking:
        duration_seconds: 300     # How long to block abusive IPs (seconds)

      trusted_ips_file: /etc/trafficserver/abuse_shield_trusted.yaml

      log_interval_sec: 10        # Rate limit logging per IP (seconds, default: 10)

      log_file: abuse_shield      # Optional: separate log file for LOG actions


    rules:
      # Block IPs exceeding 50 requests/second
      - name: "request_rate_limit"
        filter:
          max_req_rate: 50
        action: [log, block]

      # Block IPs opening more than 10 connections/second
      - name: "connection_rate_limit"
        filter:
          max_conn_rate: 10
        action: [log, block]

      # Block IPs generating more than 5 H2 errors/second
      - name: "h2_error_rate_limit"
        filter:
          max_h2_error_rate: 5
        action: [log, block]

    enabled: true

Rule Filters (Token Bucket)
---------------------------

Each rule has a ``filter`` section that defines rate limits using **token bucket**
algorithm. A rule matches when tokens go negative (rate exceeded).

========================== ===========================================================
Filter                     Description
========================== ===========================================================
``max_req_rate``           Maximum requests per second (0 = disabled)
``req_burst_multiplier``   Burst multiplier for requests (default: 1.0, must be >= 1.0)
``max_conn_rate``          Maximum connections per second (0 = disabled)
``conn_burst_multiplier``  Burst multiplier for connections (default: 1.0, must be >= 1.0)
``max_h2_error_rate``      Maximum HTTP/2 errors per second (0 = disabled)
``h2_burst_multiplier``    Burst multiplier for H2 errors (default: 1.0, must be >= 1.0)
========================== ===========================================================

**How Token Bucket Works:**

* Each IP gets a bucket initialized with tokens equal to ``rate * burst_multiplier``.
* Each event consumes 1 token.
* Tokens replenish continuously at the configured rate.
* When tokens go negative, the rate limit is exceeded.

**Burst Multiplier:**

The burst multiplier controls how many tokens the bucket can hold relative to the
rate. This allows clients to temporarily exceed the rate limit if they have
accumulated tokens.

* ``1.0`` (default): Burst capacity equals the rate (no extra tolerance)
* ``2.0``: Burst capacity is 2x the rate (allows brief spikes up to 2x normal)
* Values less than ``1.0`` are invalid and will cause plugin initialization to fail

For example, with ``max_req_rate: 100`` and ``req_burst_multiplier: 2.0``, the
bucket holds up to 200 tokens, allowing short bursts of up to 200 requests
while still enforcing an average rate of 100 requests/second.

Actions
-------

Each rule has an ``action`` list with one or more actions:

============== ============================================================
Action         Description
============== ============================================================
``log``        Log the rate limit violation with token counts
``block``      Block the IP for ``blocking.duration_seconds``
``close``      Immediately close the connection
============== ============================================================

Log Rate Limiting
~~~~~~~~~~~~~~~~~

To prevent log flooding when an IP is generating many requests that exceed
rate limits, the ``log`` action is rate-limited per IP. By default, each IP
will be logged at most once every 10 seconds, regardless of how many requests
trigger the rule during that interval.

This interval is configurable via the ``log_interval_sec`` global setting:

.. code-block:: yaml

    global:
      log_interval_sec: 10  # Log at most once per 10 seconds per IP (default: 10)

Setting ``log_interval_sec`` to 0 disables log rate limiting, causing every
rule match to be logged (not recommended for production).

HTTP/2 Error Codes
------------------

The plugin tracks all HTTP/2 error codes. Client-caused errors are typically
indicative of abuse, while server-caused errors usually indicate server issues.

============ ====================== ============= ==================================
Code         Name                   Typical Cause CVEs
============ ====================== ============= ==================================
0x01         PROTOCOL_ERROR         Client        CVE-2019-9513, CVE-2019-9518
0x02         INTERNAL_ERROR         Server
0x03         FLOW_CONTROL_ERROR     Client        CVE-2019-9511, CVE-2019-9517
0x04         SETTINGS_TIMEOUT       Client
0x05         STREAM_CLOSED          Client
0x06         FRAME_SIZE_ERROR       Client
0x07         REFUSED_STREAM         Server
0x08         CANCEL (RST_STREAM)    Client        CVE-2023-44487 (Rapid Reset)
0x09         COMPRESSION_ERROR      Client        CVE-2016-1544 (HPACK bomb)
0x0a         CONNECT_ERROR          Either
0x0b         ENHANCE_YOUR_CALM      Server
0x0c         INADEQUATE_SECURITY    Either
0x0d         HTTP_1_1_REQUIRED      Server
============ ====================== ============= ==================================

Rate Limiting
-------------

The plugin uses **token bucket algorithm** for accurate per-second rate limiting.
This provides smooth rate control with burst tolerance.

**Request Rate Limiting** (``max_req_rate``):

Tracks HTTP requests per IP per second. Useful for detecting aggressive scrapers,
API abuse, or application-layer DDoS.

**Connection Rate Limiting** (``max_conn_rate``):

Tracks new SSL/TLS connections per IP per second. Useful for detecting connection
floods. Note: Only works for HTTPS connections (VCONN_START hook).

**H2 Error Rate Limiting** (``max_h2_error_rate``):

Tracks HTTP/2 protocol errors per IP per second. Useful for detecting protocol
abuse attacks like CVE-2023-44487 (Rapid Reset).

Example rate limiting rules:

.. code-block:: yaml

    rules:
      # Block IPs exceeding 100 requests/second, with 2x burst tolerance
      - name: "high_request_rate"
        filter:
          max_req_rate: 100
          req_burst_multiplier: 2.0    # Allow burst up to 200 requests
        action: [log, block]

      # Block IPs opening more than 20 connections/second
      - name: "high_connection_rate"
        filter:
          max_conn_rate: 20
          conn_burst_multiplier: 2.5   # Allow burst up to 50 connections
        action: [log, block]

      # Block IPs generating more than 10 H2 errors/second
      - name: "h2_attack_detection"
        filter:
          max_h2_error_rate: 10
        action: [log, block]

Trusted IPs
-----------

Create a YAML file containing a sequence of trusted IP addresses under the
``trusted_ips`` key. The following IP formats are supported:

======================= ===========================================================
Example                 Effect
======================= ===========================================================
``10.0.2.123``          Exempt a single IP Address.
``10.0.3.1-10.0.3.254`` Exempt a range of IP addresses.
``10.0.4.0/24``         Exempt a range of IP addresses specified by CIDR notation.
======================= ===========================================================

Example :file:`abuse_shield_trusted.yaml`:

.. code-block:: yaml

    trusted_ips:
      # Localhost
      - 127.0.0.1
      - "::1"

      # Internal networks
      - 10.0.0.0/8
      - 192.168.0.0/16

      # Monitoring servers
      - 203.0.113.50

      # Range example
      - 172.16.0.1-172.16.0.100

Log File
--------

By default, the ``log`` action writes to the Traffic Server ``diags.log`` file.
You can configure a separate log file for abuse_shield output
by setting ``log_file`` in the global configuration:

.. code-block:: yaml

    global:
      log_file: abuse_shield

This creates a log file named ``abuse_shield.log`` in the Traffic Server log
directory. The log file includes timestamps and contains entries for each rule
match with the LOG action enabled. The format is::

    Rule "<name>" matched for IP=<ip>: actions=[<actions>] req_tokens=<n> conn_tokens=<n> h2_tokens=<n>

Runtime Control
===============

The plugin supports runtime control via ``traffic_ctl plugin msg``:

Reload Configuration
--------------------

The plugin supports dynamic configuration reload without requiring an ATS restart.
Reload the YAML configuration at runtime::

    traffic_ctl plugin msg abuse_shield.reload

This reloads all settings including rules, blocking duration, and trusted IPs.
Tracked IP data and current block states are preserved across reloads.

Dump Tracked IPs
----------------

Dump all currently tracked IPs and their token bucket states to the ``diags.log`` file::

    traffic_ctl plugin msg abuse_shield.dump

The dump output includes three trackers:

.. code-block:: text

    # abuse_shield dump (token bucket rate limiting)
    # Negative tokens indicate rate exceeded

    # Transaction (Request) tracker
    # slots_used: 2 / 10000
    # contests: 2 (won: 2)
    # evictions: 0
    10.0.1.2    tokens=-448    count=502    blocked=91011139
    127.0.0.1   tokens=49      count=1      blocked=0

    # Connection tracker
    # slots_used: 1 / 10000
    10.0.1.2    tokens=9       count=13     blocked=91011139

    # H2 Error tracker
    # slots_used: 0 / 10000

**Interpreting the output:**

* ``tokens=-448``: Negative = rate exceeded, IP is blocked
* ``tokens=49``: Positive = within rate limit
* ``count=502``: Debug counter - total events seen (not used for blocking)
* ``blocked=91011139``: Block expiration timestamp (steady_clock ms, 0 = not blocked)

List Trusted IPs
----------------

List all currently loaded trusted IP ranges::

    traffic_ctl plugin msg abuse_shield.trusted

The output appears in ``diags.log`` and includes the count of loaded ranges:

.. code-block:: text

    Trusted IP ranges (5 total):
      127.0.0.1
      ::1
      10.0.0.0-10.255.255.255
      192.168.0.0-192.168.255.255
      203.0.113.50

Enable/Disable
--------------

Enable or disable the plugin at runtime::

    traffic_ctl plugin msg abuse_shield.enabled 1
    traffic_ctl plugin msg abuse_shield.enabled 0

Metrics
=======

The plugin exposes ATS statistics for monitoring. View with::

    traffic_ctl metric get abuse_shield.*

Available metrics:

======================================= =========================================================
Metric                                  Description
======================================= =========================================================
``abuse_shield.rules.matched``          Total times any rule filter condition was true
``abuse_shield.actions.blocked``        Total times block action executed (IP added to blocklist)
``abuse_shield.actions.closed``         Total times close action executed (connection shutdown)
``abuse_shield.actions.logged``         Total times log action executed
``abuse_shield.connections.rejected``   Connections rejected at start (previously blocked IPs)
======================================= =========================================================

These metrics are useful for:

* Monitoring attack detection in production
* Alerting on sudden spikes in blocked IPs
* Verifying rules are triggering as expected
* Measuring the effectiveness of abuse protection

Example Configuration
=====================

Basic protection using token bucket rate limiting:

.. code-block:: yaml

    global:
      ip_tracking:
        slots: 10000

      blocking:
        duration_seconds: 300       # 5 minute block

      trusted_ips_file: /etc/trafficserver/abuse_shield_trusted.yaml

      log_file: abuse_shield        # Optional: separate log file

    rules:
      # Block IPs exceeding 100 requests/second with 2x burst tolerance
      - name: "request_rate_limit"
        filter:
          max_req_rate: 100
          req_burst_multiplier: 2.0   # Allow burst up to 200
        action: [log, block]

      # Block IPs opening more than 20 connections/second
      - name: "connection_rate_limit"
        filter:
          max_conn_rate: 20
        action: [log, block]

      # Block IPs generating more than 5 H2 errors/second
      # Catches CVE-2023-44487 (Rapid Reset), CVE-2019-9513, etc.
      - name: "h2_error_limit"
        filter:
          max_h2_error_rate: 5
        action: [log, block]

    enabled: true

Memory Usage
============

Memory is bounded by the ``slots`` configuration:

======= =========
Slots   Memory
======= =========
10,000  ~1.6 MB
50,000  ~8.0 MB
100,000 ~16.0 MB
======= =========

Each slot is approximately 128 bytes and includes all tracking counters.

Comparison with Other Plugins
=============================

The ``abuse_shield`` plugin combines features from both ``block_errors`` and
``rate_limit`` plugins, providing a unified abuse protection solution:

================================ ============== ============== ==============
Feature                          abuse_shield   block_errors   rate_limit
================================ ============== ============== ==============
**Error Tracking**
HTTP/2 error codes               All 16         Only 2         No
Client vs server errors          Yes            No             No
Pure attack detection            Yes            No             No
**Rate Limiting**
Per-IP request rate              Yes            No             No
Per-IP connection rate           Yes            No             No
Per-remap/SNI limits             No             No             Yes
Request queuing                  No             No             Yes
**IP Management**
Per-IP tracking                  Yes            Yes            Yes (IP rep)
IP blocking with duration        Yes            Yes            Yes
Trusted IP bypass                Yes            No             Yes
**Configuration**
YAML configuration               Yes            No             Yes
Dynamic reload                   Yes            Partial        Yes
Memory bounded                   Yes (Udi)      No             Yes (LRU)
================================ ============== ============== ==============

**When to use which plugin:**

* Use ``abuse_shield`` for HTTP/2 attack protection and per-IP abuse detection
* Use ``rate_limit`` for per-service (remap/SNI) rate limiting with queuing
* Use ``block_errors`` for simple HTTP/2 error blocking (legacy)

See Also
========

* :ref:`admin-plugins-block_errors`
* :ref:`admin-plugins-rate_limit`
