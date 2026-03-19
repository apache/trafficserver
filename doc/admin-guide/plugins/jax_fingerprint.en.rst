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

.. _admin-plugins-jax-fingerprint:

JAx Fingerprint Plugin
**********************

Description
===========

The JAx Fingerprint plugin generates TLS client fingerprints based on the JA4 or JA3 algorithm designed by John Althouse.

Fingerprints can be used for:

* Client identification and tracking
* Bot detection and mitigation
* Security analytics and threat intelligence
* Understanding client implementation patterns


Plugin Configuration
====================

You can use the plugin as a global pugin, a remap plugin, or both.

To use the plugin as a global plugin, add the following line to :file:`plugin.config`::

    jax_fingerprint.so --standalone

To use the plugin as a remap plugin, append the following line to a remap rule on :file:`remap.config`::

    @plugin=jax_fingerprint.so @pparam --standalone

To use the plugin as both global and remap plugin (hybrid setup), have the both without `--standalone` option.


.. option:: --standalone

This option enables you to use the plugin as either a global plugin, or a remap plugin. In other
words, the option needs to be specified if you do not use the hybrid setup.

.. option:: --method <JA4|JA3>

Fingerprinting method (e.g. JA4, JA3, etc.) to use.

.. option:: --mode <overwrite|keep|append>

This option specifies what to do if requests from clients have the header names that are specified
by `--header` and/or `via-header`. Available setting values are "overwrite", "keep" and "append".

.. option:: --servernames <servername1,servername2,...>

This option specifies server name(s) for which the plugin generates fingerprints.
The value must be provided as a single comma separated value (no space) of server names.

.. option:: --header <header_name>

This option specifies the name of the header field where the plugin stores the generated fingerprint value. If not specified, header generation will be suppressed.

.. option:: --via-header <via_header_name>

This option specifies the name of the header field where the plugin stores the generated fingerprint-via value. If not specified, header generation will be suppressed.

.. option:: --log-filename <filename>

This option specifies the filename for the plugin log file. If not specified, log output will be suppressed.


Plugin Behavior
===============

Global plugin setup
-------------------

Global plugin setup is the best if you:
 * Need a fingerprint on every request

Remap plugin setup
------------------

Remap plugin setup is the best if you:
 * Need a fingerprint only on specific paths, or
 * Cannot use Global plugin setup

.. note:: For JA3 and JA4, fingerprints are always generated at the beginning of connections. Using remap plugin setup only reduces the overhead of adding HTTP headers and loggingg.

Hybrid setup
------------

Hybrid setup is the best if you:
 * Need a fingerprint only for specific server names (in TLS SNI extension), and
 * Need a fingerprint only on specific paths


Log Output
==========

The plugin output a log file in the Traffic Server log directory (typically ``/var/log/trafficserver/``) if a log filename is
specified by `--log-filename` option.

**Log Format**::

    [timestamp] Client: <address>    <method_name>: <fingerprint>

**Example**::

    [Jan 29 10:15:23.456] Client: 192.168.1.100    JA4: t13d1516h2_8daaf6152771_b186095e22b6
    [Jan 29 10:15:24.123] Client: 10.0.0.50        JA4: t13d1715h2_8daaf6152771_02713d6af862


Using HTTP Headers in Origin Requests
=====================================

Origin servers can access the generated fingerprint through the injected HTTP header.
This allows the origin to:

* Make access control decisions based on client fingerprints
* Log fingerprints for security analysis
* Track client populations and TLS implementation patterns

The fingerprint-via header allows origin servers to track which Traffic Server proxy handled the request when multiple proxies are deployed.


Debugging
=========

To enable debug logging for the plugin, set the following in :file:`records.yaml`::

    records:
      diags:
        debug:
          enabled: 1
          tags: jax_fingerprint


Requirements
============

* Traffic Server must be built with TLS support (OpenSSL or BoringSSL) if you use JA3 or JA4


See Also
========
* JA3 Technical Specification: https://github.com/FoxIO-LLC/ja3
* JA4+ Technical Specification: https://github.com/FoxIO-LLC/ja4


Example Configuration
=====================

Enable JA4 fingerprinting by hybrid (global + remap) setup
----------------------------------------------------------

This configuration adds x-my-ja4 header if a connection is established for either abc.example or xyz.example.

**plugin.config**::

    jax_fingerprint.so --method JA4 --servernames abc.example,xyz.example

**remap.config**::

    map / http://origin.example/ @plugin=jax_fingerprint.so @pparam=--method=JA4 @pparam=--header=x-my-ja4
