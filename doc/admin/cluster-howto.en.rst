.. _traffic-server-cluster:

Traffic Server Cluster
**********************

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



Traffic Server scales from a single node to multiple nodes that form a
cluster allowing you to improve system performance and reliability.

.. toctree::
   :maxdepth: 1

Understanding Traffic Server Clusters
=====================================

A Traffic Server cluster consists of multiple Traffic Server nodes. The
nodes in a cluster share configuration information and can form a single
logical cache. Traffic Server detects the addition and deletion of nodes
in the cluster automatically and can detect when a node is unavailable.
Traffic Server uses its own protocol for clustering, which is multicast
for node location and heartbeat, but unicast for all data exchange
within the cluster. Traffic Server has two clustering modes:

-  Management-only mode; refer to `Management-Only Clustering`_ below.
-  Full-clustering mode; refer to `Full Clustering`_

Management-Only Clustering
==========================

In management-only clustering mode, Traffic Server cluster nodes share
configuration information. You can administer all the nodes at the same
time. Traffic Server uses a multicast management protocol to provide a
single system image of your Traffic Server cluster. Information about
cluster membership, configuration, and exceptions is shared across all
nodes, and the :program:`traffic_manager` process automatically propagates
configuration changes to all the nodes.

Full Clustering
===============

In full-clustering mode, as well as sharing configuration information, a
Traffic Server cluster distributes its cache across its nodes into a
single, virtual object store, rather than replicating the cache node by
node. Traffic Server can provide an enormous aggregate cache size and
can maximize cache hit rate by storing objects only once across the
entire cluster.

A fully clustered Traffic Server maps objects to specific nodes in the
cluster. When a node receives a request, it checks to see if the request
is a hit somewhere in the cluster. If the request is a hit on a
different node, the node handling the request obtains the object from
the hit node and serves it to the client. Traffic Server uses its own
communication protocol to obtain an object from sibling cluster nodes.

If a node fails or is shut down and removed, Traffic Server removes
references to the missing node on all nodes in the cluster.

Full clustering recommends a dedicated network interface for cluster
communication to get better performance.

Enabling Clustering Mode
========================

Before you put a node into a cluster, please make sure the following
things are in order:

-  You are using the same operation system on all nodes:

   -  Using the same distribution, e.g.: RHEL 5.5
   -  Have same kernel, e.g.: 2.6.18-194.17.1.el5
   -  The same architecture, e.g.: ``x86_64``

-  You have the same version of Traffic Server installed
-  The same hardware
-  On the same switch or same VLAN.

Traffic Server does not apply the clustering mode change to all the
nodes in the cluster. You must change the clustering mode on each node
individually. You may following these instructions:

1. setup the same cluster name, with :ts:cv:`proxy.config.proxy_name`, e.g. MyCluster.

2. Set :ts:cv:`proxy.local.cluster.type` to ``1``, to enable cluster mode.

3. Setup a :ts:cv:`proxy.config.cluster.ethernet_interface`, e.g.: ``eth0``.
   This should be replaced by your real interface; we recommends a
   dedicated interface here. Refer to :ts:cv:`proxy.local.cluster.type` for a full description.

3. enable: ::

       traffic_line -x

4. restart: ::

       traffic_line -L

   The :program:`traffic_server` and :program:`traffic_manager` processes will need to
   restart after the change of :ts:cv:`proxy.local.cluster.type` and
   :ts:cv:`proxy.config.cluster.ethernet_interface` have taken place.

Traffic Server will join the cluster in about 10 seconds, and you can
run :option:`traffic_line -r` `proxy.process.cluster.nodes` to check the hosts
in the cluster, or check out the :file:`cluster.config` in the configuration directory.

.. XXX:: cluster.config is not documented.

After a successful join of the cluster, all changes of global
configurations on any node, will take effect on **all** nodes.

Deleting Nodes from a Cluster
=============================

To delete a node from the Traffic Server cluster, just roll back
:ts:cv:`proxy.config.cluster.type` to the default value 3 and reload.

Performance tweak for busy Cluster
==================================

Starting from v3.2.0, Apache Traffic Server can handle multiple internal
cluster connections, and we can tweak for the Cluster threads and each of
the thread will keep one connection to all of the cluster machines:

-  Increasing Cluster threads:

   In the cluster env, the current performance of the Cluster threads
   will consume the same cpu usage as net threads, so you may adapt the
   ET_NET & ET_CLUSTER at about 1:1. For example, on a 24 cores
   system, set ET_NET threads & ET_CLUSTER threads to 10, by setting :ts:cv:`proxy.config.cluster.threads` to ``10``.

.. XXX::  ET_NET and ET_CLUSTER should be documented some place. Right now, this means nothing to me.

   ::
       traffic_line -s proxy.config.cluster.threads -v 10

with these tweaks, we can archive about 10gbps traffic for the internal
cluster transfer speed.
