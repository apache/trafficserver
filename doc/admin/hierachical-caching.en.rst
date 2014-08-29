.. _hierarchical-caching:

Hierarchical Caching
********************

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


.. toctree::
   :maxdepth: 2

Understanding Cache Hierarchies
===============================

A cache hierarchy consists of cache levels that communicate with each
other. Traffic Server supports several types of cache hierarchies. All
cache hierarchies recognize the concept of **parent** and **child**. A
parent cache is a cache higher up in the hierarchy, to which Traffic
Server can forward requests. A child cache is a cache for which Traffic
Server is a parent.

Traffic Server supports the following hierarchical caching options:

Parent Caching
==============

If a Traffic Server node cannot find a requested object in its cache,
then it searches a parent cache (which itself can search other caches)
before finally retrieving the object from the origin server. You can
configure a Traffic Server node to use one or more parent caches so that
if one parent is unavailable, then another parent is availale to service
requests. This is called `Parent Failover`_. Traffic
Server will support parent caching for HTTP and HTTPS requests.

**Note:** If you do not want all requests to go to the parent cache,
then simply configure Traffic Server to route certain requests (such as
requests containing specific URLs) directly to the origin server. Simply
set parent proxy rules in :file:`parent.config`

The figure below illustrates a simple cache hierarchy with a Traffic
Server node configured to use a parent cache. In the following scenario,
a client sends a request to a Traffic Server node that is a child in the
cache hierarchy (because it's configured to forward missed requests to a
parent cache). The request is a cache miss, so Traffic Server then
forwards the request to the parent cache, where it is a cache hit. The
parent sends a copy of the content to the Traffic Server, where it is
cached and then served to the client. Future requests for this content
can now be served directly from the Traffic Server cache (until the data
is stale or expired).

.. figure:: ../static/images/admin/cachehrc.jpg
   :align: center
   :alt: Parent caching

   Parent caching

**Note:** If the request is a cache miss on the parent, then the parent
retrieves the content from the origin server (or from another cache,
depending on the parent’s configuration). The parent caches the content
and then sends a copy to Traffic Server (its child), where it is cached
and served to the client.

Parent Failover
---------------

Traffic Server supports use of several parent caches. This ensures that
if one parent cache is not available, another parent cache can service
client requests.

When you configure your Traffic Server to use more than one parent
cache, Traffic Server detects when a parent is not available and sends
missed requests to another parent cache. If you specify more than two
parent caches, then the order in which the parent caches are queried
depends upon the parent proxy rules configured in the file:`parent.config`
configuration file. By default, the parent caches are queried in the
order they are listed in the configuration file.

.. _configuring-traffic-server-to-use-a-parent-cache:

Configuring Traffic Server to Use a Parent Cache
------------------------------------------------

To configure Traffic Server to use one or more parent caches, you must
complete the following steps:

-  Enable the parent caching option.
-  Identify the parent cache you want to use to service missed requests.
   To use **parent failover**, you must identify more than one parent
   cache so that when a parent cache is unavailable, requests are sent
   to another parent cache.

.. note:: You need to configure the child cache only. No additional configuration is needed for the Traffic Server parent cache.

Configure Traffic Server to use a parent cache by editing the following
variable :ts:cv:`proxy.config.http.parent_proxy_routing_enable` in :file:`records.config` file.

Edit the :file:`parent.config` file located in the Traffic Server ``config`` directory to set parent
proxy rules to specify the parent cache to which you want missed
requests to be forwarded;

The following example configures Traffic Server to route all requests
containing the regular expression ``politics`` and the path
``/viewpoint`` directly to the origin server (bypassing any parent
hierarchies): ::

    url_regex=politics prefix=/viewpoint go_direct=true

The following example configures Traffic Server to direct all missed
requests with URLs beginning with ``http://host1`` to the parent cache
``parent1``. If ``parent1`` cannot serve the requests, then requests are
forwarded to ``parent2``. Because ``round-robin=true``, Traffic Server
goes through the parent cache list in a round-robin based on client IP
address.::

    dest_host=host1 scheme=http parent="parent1;parent2" round-robin=strict

Run the command :option:`traffic_line -x` to apply the configuration changes.

.. XXX As of yet, this is unsupported.

.. # ICP Peering # {#ICPPeering}

.. The Internet Cache Protocol (ICP) is used by proxy caches to exchange information 
   about their content. ICP query messages ask other caches if they are storing 
   a particular URL; ICP response messages reply with a hit or miss answer. A 
   cache exchanges ICP messages only with specific **ICP peers**, which are neighboring 
   caches that can receive ICP messages. An ICP peer can be a **sibling cache 
   **(which is at the same level in the hierarchy) or a **parent cache** (which 
   is one level up in the hierarchy). 

.. If Traffic Server has ICP caching enabled, then it sends ICP queries to its 
   ICP peers when the HTTP request is a cache miss. If there are no hits but parents 
   exist, then a parent is selected using a round-robin policy. If no ICP parents 
   exist, then Traffic Server forwards the request to its HTTP parents. If there 
   are no HTTP parent caches established, then Traffic Server forwards the request 
   to the origin server. 

.. If Traffic Server receives a hit message from an ICP peer, then Traffic Server 
   sends the HTTP request to that peer. However, it might turn out to be a cache 
   miss because the original HTTP request contains header information that is 
   not communicated by the ICP query. For example, the hit might not be the requested 
   alternate. If an ICP hit turns out to be a miss, then Traffic Server forwards 
   the request to either its HTTP parent caches or to the origin server. 

.. To configure a Traffic Server node to be part of an ICP cache hierarchy, you 
   must perform the following tasks: 

.. * Determine if the Traffic Server can receive ICP messages only, or if it can send _and_ receive ICP messages. 
   * Determine if Traffic Server can send messages directly to each ICP peer or send a single message on a specified multicast channel. 
   * Specify the port used for ICP messages. 
   * Set the ICP query timeout. 
   * Identify the ICP peers (siblings and parents) with which Traffic Server can communicate.

.. To configure Traffic Server to use an ICP cache hierarchy edit the following variables in :file:`records.config` file:

.. * :ts:cv:`proxy.config.icp.enabled`
   * :ts:cv:`proxy.config.icp.icp_port`
   * :ts:cv:`proxy.config.icp.multicast_enabled`
   * :ts:cv:`proxy.config.icp.query_timeout`

.. Edit :file:`icp.config` file located in the Traffic Server `config` directory: 
   For each ICP peer you want to identify, enter a separate rule in the :file:`icp.config` file.

.. Run the command :option:`traffic_line -x` to apply the configuration changes.

