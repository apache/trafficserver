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
communication to achieve better performance.

Enabling Clustering Mode
========================

Before a node is added into a cluster, please ensure that the following
conditions are being met:

-  All nodes are running the same operating system:

   -  The same distribution, e.g.: RHEL 6.6
   -  The same kernel, e.g.: 2.6.32-504.23.4.el6
   -  The same architecture, e.g.: x86_64

-  All nodes have the same version of Traffic Server installed
-  All nodes are composed of the same hardware
-  All nodes are on the same switch or same VLAN.

Traffic Server does not apply the clustering mode change to all the
nodes in the cluster. You must change the clustering mode on each node
individually. You may following these instructions:

1. Setup the same cluster name, with :ts:cv:`proxy.config.proxy_name`, e.g. ``MyCluster``.

2. Set :ts:cv:`proxy.local.cluster.type` to ``1`` to enable cluster mode. The
   following values of this configuration are valid:

================= ==================================================
Value             Description
================= ==================================================
1                 Full-Clustering mode
2                 Management-Only mode
3                 No clustering (*default*)
================= ==================================================

3. Configure :ts:cv:`proxy.config.cluster.ethernet_interface`, e.g.: ``eth0``.
   This should be replaced with the node's real interface. We recommends a
   dedicated physical interface here. Refer to :ts:cv:`proxy.local.cluster.type`
   for a full description.

4. Enable configuration changes::

       traffic_ctl config reload

5. Restart traffic server::

       traffic_ctl server restart

   The :program:`traffic_server` and :program:`traffic_manager` processes will need to
   restart after the change of :ts:cv:`proxy.local.cluster.type` and
   :ts:cv:`proxy.config.cluster.ethernet_interface` have taken place.

Traffic Server will join the cluster in about 10 seconds. To verify the hosts in the
cluster, you can run::

    traffic_ctl metric get proxy.process.cluster.nodes

Cluster node status is also tracked in ``cluster.config`` in the configuration
directory. This configuration is generated by the system and should not be
edited. It contains a list of the machines that are currently members of the
cluster, for example::

    # 3
    127.1.2.3:80
    127.1.2.4:80
    127.1.2.5:80

After successfully joining a cluster, all changes of global configurations
performed on any node in that cluster will take effect on all nodes, removing
the need to manually duplicate configuration changes across each node individually.

Deleting Nodes from a Cluster
=============================

To delete a node from the Traffic Server cluster, return the setting
:ts:cv:`proxy.local.cluster.type` to the default value ``3`` and reload.

Common issues for Cluster setup
===============================

1. The Cluster member auto discovery is built upon multi-casting UDP, and as such
   is impossible to setup where multi-casting is not avaliable, such as AWS EC2.

2. The Cluster will depend on ports 8088, 8089, and 8086. These ports must not be
   blocked by any network configurations or firewalls on the network used for
   internal cluster communication.

Performance Tuning for Busy Clusters
====================================

Beginning with version 3.2.0, Apache Traffic Server can handle multiple internal
cluster connections and the number of Cluster Threads is configurable. Each
of the threads will keep one connection open to all peering cluster nodes.

Increasing Cluster Threads
--------------------------

In the cluster environment, the current performance of the cluster threads
will consume the same cpu usage as a normal network thread. It's reasonable
to keep roughly the same number of cluster threads as network threads. For
example, if you are running a system with 10 network processing threads,
you can set the number of cluster threads by modifying
:ts:cv:`proxy.config.cluster.threads` to ``10``::

    traffic_ctl config set proxy.config.cluster.threads 10

