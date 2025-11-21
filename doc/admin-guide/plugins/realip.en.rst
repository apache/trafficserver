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

.. _admin-plugins-realip:

Real IP Plugin
*******************

Description
===========
The `realip` plugin reads an IP address from a specified data source such as HTTP header field, and makes it available for ATS and
its plugins as the verified client address.

To use the verified client IP address for ACL, :ts:cv:`proxy.config.acl.subjects` needs to be set to `PLUGIN`.
Similary, if you want to use the IP address on :doc:`header_rewrite plugin<header_rewrite.en>`, its `INBOUND_IP_SOURCE` needs to be set to `PLUGIN`.


Configuration
=============

To enable the `realip` plugin, insert the following line in :file:`plugin.config`::

    realip.so <config.yaml>

The plugin supports 2 modes:

simple
------

The plugin reads an IP address from a specified HTTP header field. These settings below are available for this mode:


+------------------+-------------------------------------------------------------------------------------------------------+
| Setting name     | Description                                                                                           |
+==================+=======================================================================================================+
| `header`         | HTTP field name that has an IP address. The field value must be a valid IP address.                   |
|                  | You cannot use this mode for Forwarded header field () because its field value is structured.         |
+------------------+-------------------------------------------------------------------------------------------------------+
| `trustedAddress` | A list of trusted IP addresss, and/or IP address ranges.                                              |
|                  | The IP address in the specified header field will not be used if the sender's address is not trusted. |
+------------------+-------------------------------------------------------------------------------------------------------+

proxyProtocol
-------------

The plugins reads an IP address from PROXY protocol header field.
Although there is no plugin settings for this mode, `server_ports` and `proxy_protocol_allowlist` on records.yaml need to be
configured to accept PROXY protocol.


Example Configuration
=====================

Use Client-IP header, if the header is sent from `192.168.0.0/24` or `127.0.0.1`.

   .. code-block:: yaml

      simple:
        header: "Client-IP"
        trustedAddress:
          - "192.168.0.0/24"
          - "127.0.0.1"


Use PROXY protocol header.

   .. code-block:: yaml

      proxyProtocol:
