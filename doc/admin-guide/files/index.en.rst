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
   congestion.config.en
   hosting.config.en
   icp.config.en
   ip_allow.config.en
   log_hosts.config.en
   logging.config.en
   metrics.config.en
   parent.config.en
   plugin.config.en
   records.config.en
   remap.config.en
   splitdns.config.en
   ssl_multicert.config.en
   storage.config.en
   volume.config.en

:doc:`cache.config.en`
    Defines if, how, and for what durations |TS| caches objects, based on
    destinations, clients, URL components, and more.

:doc:`congestion.config.en`
    Defines network conditions under which clients will receive retry messages
    instead of |TS| contacting origin servers.

:doc:`hosting.config.en`
    Allows |TS| administrators to assign cache volumes to specific origin
    servers or domains.

:doc:`icp.config.en`
    Defines ICP peers.

:doc:`ip_allow.config.en`
    Controls access to the |TS| cache based on source IP addresses and networks
    including limiting individual HTTP methods.

:doc:`log_hosts.config.en`
    Defines origin servers for which separate logs should be maintained.

:doc:`logging.config.en`
    Defines custom log file formats, filters, and processing options.

:doc:`metrics.config.en`
    Defines custom dynamic metrics using Lua scripting.

:doc:`parent.config.en`
    Configures parent proxies in hierarchical caching layouts.

:doc:`plugin.config.en`
    Control runtime loadable plugins available to |TS|, as well as their
    configurations.

:doc:`records.config.en`
    Contains many configuration variables affecting |TS| operation, both the
    local node as well as a cluster in which the node may be a member.

:doc:`remap.config.en`
    Defines mapping rules used by |TS| to properly route all incoming requests.

:doc:`splitdns.config.en`
    Configures DNS servers to use under specific conditios.

:doc:`ssl_multicert.config.en`
    Configures |TS| to use different server certificates for SSL termination
    when listening on multiple addresses or when clients employ SNI.

:doc:`storage.config.en`
    Configures all storage devices and paths to be used for the |TS| cache.

:doc:`volume.config.en`
    Defines cache space usage by individual protocols.


