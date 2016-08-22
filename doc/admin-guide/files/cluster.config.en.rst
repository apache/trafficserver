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

.. configfile:: cluster.config

cluster.config
**************

The :file:`cluster.config` tracks |TS| cluster membership, allowing more than
one server running |TS| to coordinate and either replicate or distribute cache
operations.

For the contents of this file to reflect any clustering peers, |TS| clustering
must first be enabled by adjusting :ts:cv:`proxy.local.cluster.type`.

For information on how to enable and configure |TS| clustering, refer to the
:ref:`traffic-server-cluster` section.

.. danger::

   The :file:`cluster.config` configuration file is generated and managed by
   |TS| itself and should not be modified directly.

Format
======

The format of :file:`cluster.config` is simple. Lines beginning with ``#`` are
comments and, as such, will be ignored by |TS|. The first non-comment,
non-empty line is an integer indicating the number of |TS| cluster peers for
the current node. If the current node is not a member of a cluster, this value
will be ``0``.

Following lines provide the ``<address>:<port>`` of each peer in the cluster.
By default, |TS| uses ``8086`` for cluster communication. This may be adjusted
with :ts:cv:`proxy.config.cluster.cluster_port`. Note that this setting is in
the ``CONFIG`` scope, which means it must be set to the same value on all
cluster peers.

Examples
========

Stand-alone Proxy
-----------------

When running a single |TS| node without a cluster, the configuration file will
simply contain a zero, indicating that there are no cluster peers, as so::

    0

Because there are zero peers in the (non-existent) cluster, no address lines
follow.

Multiple Peers
--------------

In a cluster with four members (including the current node), the configuration
will appear as::

    3
    127.1.2.3:8086
    127.1.2.4:8086
    127.1.2.5:8086

Though, of course, the IP addresses will be appropriate for your network. If
you have configured your |TS| nodes to use a cluster management port other than
the default ``8086`` the port numbers will differ, as well.

The configuration will not include the current |TS| node's address, only its
peers' addresses.
