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

.. _admin-plugins-ja4-fingerprint:

JA4 Fingerprint Plugin
**********************

Description
===========

The JA4 Fingerprint plugin generates TLS client fingerprints based on the JA4
algorithm designed by John Althouse. JA4 is the successor to the JA3
fingerprinting algorithm and provides improved client identification for TLS
connections.

A JA4 fingerprint uniquely identifies TLS clients based on characteristics of
their TLS ClientHello messages, including:

* TLS version
* ALPN (Application-Layer Protocol Negotiation) preferences
* Cipher suites offered
* TLS extensions present

This information can be used for:

* Client identification and tracking
* Bot detection and mitigation
* Security analytics and threat intelligence
* Understanding client TLS implementation patterns

How It Works
============

The plugin intercepts TLS ClientHello messages during the TLS handshake and
generates a JA4 fingerprint consisting of three sections separated by underscores:

**Section a (unhashed)**: Basic information about the client including:

  * Protocol (``t`` for TCP, ``q`` for QUIC)
  * TLS version
  * SNI (Server Name Indication) status
  * Number of cipher suites
  * Number of extensions
  * First ALPN value

**Section b (hashed)**: A SHA-256 hash of the sorted cipher suite list

**Section c (hashed)**: A SHA-256 hash of the sorted extension list

Example fingerprint::

    t13d1516h2_8daaf6152771_b186095e22b6

Key Differences from JA3
-------------------------

* Cipher suites and extensions are sorted before hashing for consistency
* SNI and ALPN information is included in the fingerprint
* More resistant to fingerprint randomization

Plugin Configuration
====================

The plugin operates as a global plugin and has no configuration options.

To enable the plugin, add the following line to :file:`plugin.config`::

    ja4_fingerprint.so

No additional parameters are required or supported.

Plugin Behavior
===============

When loaded, the plugin will:

1. **Capture TLS ClientHello**: Intercepts all incoming TLS connections during
   the ClientHello phase

2. **Generate Fingerprint**: Calculates the JA4 fingerprint from the
   ClientHello data

3. **Log to File**: Writes the fingerprint and client IP address to
   ``ja4_fingerprint.log``

4. **Add HTTP Headers**: Injects the following headers into subsequent HTTP
   requests on the same connection:

   * ``ja4``: Contains the JA4 fingerprint
   * ``x-ja4-via``: Contains the proxy name (from ``proxy.config.proxy_name``)

Log Output
==========

The plugin writes to :file:`ja4_fingerprint.log` in the Traffic Server log
directory (typically ``/var/log/trafficserver/``).

**Log Format**::

    [timestamp] Client IP: <ip_address>    JA4: <fingerprint>

**Example**::

    [Jan 29 10:15:23.456] Client IP: 192.168.1.100    JA4: t13d1516h2_8daaf6152771_b186095e22b6
    [Jan 29 10:15:24.123] Client IP: 10.0.0.50        JA4: t13d1715h2_8daaf6152771_02713d6af862

Using JA4 Headers in Origin Requests
=====================================

Origin servers can access the JA4 fingerprint through the injected HTTP header.
This allows the origin to:

* Make access control decisions based on client fingerprints
* Log fingerprints for security analysis
* Track client populations and TLS implementation patterns

The ``x-ja4-via`` header allows origin servers to track which Traffic Server
proxy handled the request when multiple proxies are deployed.

Debugging
=========

To enable debug logging for the plugin, set the following in :file:`records.yaml`::

    records:
      diags:
        debug:
          enabled: 1
          tags: ja4_fingerprint

Debug output will appear in :file:`diags.log` and includes:

* ClientHello processing events
* Fingerprint generation details
* Header injection operations

Requirements
============

* Traffic Server must be built with TLS support (OpenSSL or BoringSSL)
* The plugin operates on all TLS connections

Configuration Settings
======================

The plugin requires the ``proxy.config.proxy_name`` setting to be configured
for the ``x-ja4-via`` header. If not set, the plugin will log an error and use
"unknown" as the proxy name.

To set the proxy name in :file:`records.yaml`::

    records:
      proxy:
        config:
          proxy_name: proxy01

Limitations
===========

* The plugin only operates in global mode (no per-remap configuration)
* Logging cannot be disabled
* Raw (unhashed) cipher and extension lists are not logged
* Non-TLS connections do not generate fingerprints

See Also
========

* JA4 Technical Specification: https://github.com/FoxIO-LLC/ja4/blob/main/technical_details/JA4.md
* JA4 is licensed under the BSD 3-Clause license

Example Configuration
=====================

Complete example configuration for enabling JA4 fingerprinting:

**plugin.config**::

    ja4_fingerprint.so

**records.yaml**::

    records:
      proxy:
        config:
          proxy_name: proxy-01
      diags:
        debug:
          enabled: 1
          tags: ja4_fingerprint

After restarting Traffic Server, the plugin will begin fingerprinting TLS
connections and logging to ``ja4_fingerprint.log``.
