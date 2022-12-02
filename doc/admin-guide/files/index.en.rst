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

.. _admin-configuration-files:

Configuration Files
*******************

.. toctree::
   :hidden:

   cache.config.en
   hosting.config.en
   ip_allow.yaml.en
   logging.yaml.en
   parent.config.en
   plugin.config.en
   records.yaml.en
   remap.config.en
   splitdns.config.en
   ssl_multicert.config.en
   sni.yaml.en
   storage.config.en
   strategies.yaml.en
   volume.config.en
   jsonrpc.yaml.en

:doc:`cache.config.en`
   Defines if, how, and for what durations |TS| caches objects, based on
   destinations, clients, URL components, and more.

:doc:`hosting.config.en`
   Allows |TS| administrators to assign cache volumes to specific origin
   servers or domains.

:doc:`ip_allow.yaml.en`
   Controls access to the |TS| cache based on source IP addresses and networks
   including limiting individual HTTP methods.

:doc:`logging.yaml.en`
   Defines custom log file formats, filters, and processing options.

:doc:`parent.config.en`
   Configures parent proxies in hierarchical caching layouts.

:doc:`plugin.config.en`
   Control runtime loadable plugins available to |TS|, as well as their
   configurations.

:doc:`records.yaml.en`
   Contains many configuration variables affecting |TS| operation.

:doc:`remap.config.en`
   Defines mapping rules used by |TS| to properly route all incoming requests.

:doc:`splitdns.config.en`
   Configures DNS servers to use under specific conditions.

:doc:`ssl_multicert.config.en`
   Configures |TS| to use different server certificates for SSL termination
   when listening on multiple addresses or when clients employ SNI.

:doc:`sni.yaml.en`
   Configures SNI based Layer 4 routing.

:doc:`storage.config.en`
   Configures all storage devices and paths to be used for the |TS| cache.

:doc:`strategies.yaml.en`
   Configures NextHop strategies used with `remap.config`

:doc:`volume.config.en`
    Defines cache space usage by individual protocols.

:doc:`jsonrpc.yaml.en`
    Defines some of the configurable arguments of the jsonrpc endpoint.

.. note::

   Currently the YAML parsing library has a bug where line number counting
   (for error messages) ignores comment lines that start with **#**.  A
   work-around is to put a space before the **#**.
