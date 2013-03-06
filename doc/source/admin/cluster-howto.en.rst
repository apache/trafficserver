:title: Traffic Server Cluster

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
cluster allowing you to improve system performance and reliability. This
chapter discusses the following topics:

[TOC]

Understanding Traffic Server Clusters
=====================================

A Traffic Server cluster consists of multiple Traffic Server nodes. The
nodes in a cluster share configuration information and can form a single
logical cache. Traffic Server detects the addition and deletion of nodes
in the cluster automatically and can detect when a node is unavailable.
Traffic Server uses its own protocol for clustering, which is multicast
for node location and heartbeat, but unicast for all data exchange
within the cluster. Traffic Server has two clustering modes:

-  Management-only mode; refer to Management-Only Clustering below.
-  Full-clustering mode; refer to Full Clustering

Management-Only Clustering
==========================

In management-only clustering mode, Traffic Server cluster nodes share
configuration information. You can administer all the nodes at the same
time. Traffic Server uses a multicast management protocol to provide a
single system image of your Traffic Server cluster. Information about
cluster membership, configuration, and exceptions is shared across all
nodes, and the ``traffic_manager`` process automatically propagates
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

   -  Using the same distribution, fx: RHEL 5.5
   -  Have same kernel, fx: 2.6.18-194.17.1.el5
   -  The same architecture, fx: ``x86_64``

-  You have the same version of Traffic Server installed
-  The same hardware
-  On the same switch or same VLAN.

Traffic Server does not apply the clustering mode change to all the
nodes in the cluster. You must change the clustering mode on each node
individually. You may following these instructions:

1. setup the same cluster name, or proxy name, e.g. MyCluster:

   ::
       traffic_line -s proxy.config.proxy_name -v MyCluster

2. enable cluster mode:

   ::
       traffic_line -s proxy.local.cluster.type -v 1
       traffic_line -s proxy.config.cluster.ethernet_interface -v eth0

   ``eth0`` should be replaced by your real interface, we recommends a
   dedicated interface here. Refer to
   ```proxy.local.cluster.type`` <../configuration-files/records.config#proxy.local.cluster.type>`_
   for a full description.

3. enable:

   ::
       traffic_line -x

4. restart:

   ::
       traffic_line -L

   The ``traffic_server`` and ``traffic_manager`` processes will need to
   restart after the change of 'proxy.local.cluster.type' and
   'proxy.config.cluster.ethernet\_interface' have taken place.

Traffic Server will join the cluster in about 10 seconds, and you can
run ``traffic_line -r proxy.process.cluster.nodes`` to check the hosts
in the cluster, or check out the
```cluster.config`` <../configuration-files/cluster.configcluster.config>`_
in the configuration directory.

After a successful join of the cluster, all changes of global
configurations on any node, will take effect on **all** nodes.

Deleting Nodes from a Cluster
=============================

To delete a node from the Traffic Server cluster, just roll back
``proxy.config.cluster.type`` to the default value 3 and reload.

Performance tweak for busy Cluster
==================================

Starting from v3.2.0, Apache Traffic Server can handle multiple internal
cluster connections, and we can tweak for the Cluster threads and
connections:

-  Increasing Cluster threads:

   In the cluster env, the current performance of the Cluster threads
   will consume the same cpu usage as net threads, so you may adapt the
   ET_NET & ET_CLUSTER at about 1:1. For example, on a 24 cores
   system, set ET_NET threads to 10, ET\_CLUSTER threads to 10.

   ::
       traffic_line -s proxy.config.cluster.threads -v 10

-  Setup the Cluster connections:

   In the Cluster, the internal connections is TCP and limited by
   ET\_CLUSTER threads and network performance, we can increase the
   connections to archive better performance.

   ::
       traffic_line -s proxy.config.cluster.num_of_cluster_connections -v 10

with these tweaks, we can archive about 10gbps traffic for the internal
cluster transfer speed.
