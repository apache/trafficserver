.. include:: ../../common.defs

.. _admin-plugins-connection-exempt-list:

Connection Exempt List Plugin
******************************

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

Description
===========

:ts:cv:`proxy.config.http.per_client.connection.exempt_list` allows
administrators to set exemptions to the per-client connection limit. However,
for large networks, managing this as a comma-separated string in
:file:`records.yaml` can be cumbersome. This plugin allows administrators to set
the exemption list :ts:cv:`proxy.config.http.per_client.connection.exempt_list`
value via an external YAML file.

Plugin Configuration
====================

The plugin is configured as a global plugin and requires a path to a YAML
configuration file. Load the plugin by adding a line to the
:file:`plugin.config`:

.. code-block:: text

   connection_exempt_list.so /path/to/exempt_list.yaml

Configuration File Format
==========================

The exempt list configuration file must be in YAML format with the following
simple structure:

.. code-block:: yaml

   exempt_list:
     - 127.0.0.1
     - ::1
     - 192.168.1.0/24
     - 10.0.0.0/8

The configuration file supports the same range formats as
:ts:cv:`proxy.config.http.per_client.connection.exempt_list`.

* Individual IPv4 addresses (e.g., ``192.168.1.100``)
* Individual IPv6 addresses (e.g., ``::1``, ``2001:db8::1``)
* IPv4 CIDR ranges (e.g., ``192.168.0.0/16``)
* Ranges as a dash-separated string (e.g., ``10.0.0.0-10.0.0.255``)

Example Usage
=============

1. Create an exempt list configuration file (e.g.,
``/opt/ats/etc/trafficserver/exempt_localhost.yaml``):

   .. code-block:: yaml

      exempt_list:
        - 127.0.0.1
        - ::1

2. Enable the plugin in :file:`plugin.config`:

   .. code-block:: text

      connection_exempt_list.so /opt/ats/etc/trafficserver/exempt_localhost.yaml

3. Configure per-client connection limits in :file:`records.yaml`:

   .. code-block:: yaml

      records:
        net:
          per_client:
            max_connections_in: 300

4. Start |TS|. The plugin will load the exempt list and not apply the per-client
   connection limit to the exempted IP addresses and ranges.

See Also
========

* :ts:cv:`proxy.config.net.per_client.max_connections_in`
* :ts:cv:`proxy.config.http.per_client.connection.exempt_list`
* :doc:`../files/plugin.config.en`
