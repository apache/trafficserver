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

==========
icp.config
==========

.. configfile:: icp.config

The :file:`icp.config` file defines ICP peers (parent and sibling caches).

.. important::

    After you modify the icp.config file, navigate to the
    Traffic Server bin directory and run the :option:`traffic_ctl config reload` command to
    apply the changes. When you apply the changes to a node in a cluster,
    Traffic Server automatically applies the changes to all other nodes in
    the cluster.

Format
======

Each line in the icp.config file contains the name and configuration
information for a single ICP peer in the following format::

    host : host_IP : ctype : proxy_port : icp_port : MC_on : MC_IP : MC_TTL :

Each field is described in the following list.

``host``
    The hostname of the ICP peer.

    This field is optional; if you do not specify the hostname of the
    ICP peer, you must specify the IP address.

``host_IP``
    The IP address of the ICP peer.

    This field is optional; if you do not specify the IP address of the
    ICP peer, you must specify the hostname.

``ctype``
    Use the following options:

    -  1 to indicate an ICP parent cache
    -  2 to indicate an ICP sibling cache
    -  3 to indicate an ICP local cache

``proxy_port``
    The port number of the TCP port used by the ICP peer for proxy
    communication.

``icp_port``
    The port number of the UDP port used by the ICP peer for ICP
    communication.

``MC_on``
    Enable or disable MultiCast:

    -  0 if multicast is disabled
    -  1 if multicast is enabled

``MC_ip``
    The MultiCast IP address.

``MC_ttl``
    The multicast time to live. Use the following options:

    -  1 if IP multicast datagrams will not be forwarded beyond a single
       subnetwork
    -  2 to allow delivery of IP multicast datagrams to more than one
       subnet (if there are one or more multicast routers attached to
       the first hop subnet).

Examples
========

The following example configuration is for three nodes: the local host,
one parent, and one sibling.

::

    localhost:0.0.0.0:3:8080:3130:0:0.0.0.0:1
    host1:123.12.1.23:1:8080:3131:0:0.0.0.0:1
    host2:123.12.1.24:2:8080:3131:0:0.0.0.0:1
