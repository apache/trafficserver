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

.. _admin-hierarchical-caching:

Hierarchical Caching
********************

.. toctree::
   :maxdepth: 2

Understanding Cache Hierarchies
===============================

A cache hierarchy consists of cache levels that communicate with each
other. Traffic Server supports several types of cache hierarchies. All
cache hierarchies recognize the concept of *parent* and *child*. A
parent cache is a cache higher up in the hierarchy, to which Traffic
Server can forward requests. A child cache is a cache for which Traffic
Server is a parent.

Traffic Server supports the following hierarchical caching options:

Parent Caching
==============

If a Traffic Server node cannot find a requested object in its cache,
then it searches a parent cache (which itself can search other caches)
before finally retrieving the object from the origin server. You can
configure a Traffic Server node to use multiple parent caches so that
if one parent is unavailable, the other parent caches will be checked in turn
until either the request is serviced properly or no further parent caches are
available and the origin server is contacted. This is called `Parent Failover`_.
Traffic Server supports parent caching for both HTTP and HTTPS requests.

If you do not want all requests to go to the parent cache, then simply configure
Traffic Server to route certain requests (such as requests containing specific
URLs) directly to the origin server. This may be achieved by setting parent
proxy rules in :file:`parent.config`.

The figure below illustrates a simple cache hierarchy with a Traffic
Server node configured to use a parent cache. In the following scenario,
a client sends a request to a Traffic Server node that is a child in the
cache hierarchy (because it's configured to forward missed requests to a
parent cache). The request is a cache miss, so Traffic Server then
forwards the request to the parent cache where it is a cache hit. The
parent sends a copy of the content to the Traffic Server, where it is
cached and then served to the client. Future requests for this content
can now be served directly from the Traffic Server cache (until the data
is stale or expired).

.. figure:: /static/images/admin/cachehrc.jpg
   :align: center
   :alt: Parent caching

   Parent caching

If the request is a cache miss on the parent, then the parent retrieves the
content from the origin server (or from another cache, depending on the parent's
configuration). The parent caches the content and then sends a copy to Traffic
Server (its child), where it is cached and served to the client.

Parent Selection Policies
-------------------------

There are several parent selection policies available to the Traffic Server child
node.

    - Consistent Hash. The child node chooses a specific parent for each URI. This effectively
      makes the parent's cache size the sum total of each of the individual parent cache
      sizes.
    - Round Robin. The child node cycles through the parents. The child can select the parent
      based on the client IP ("true"), or in a strict cycling ("strict").
    - (Unnamed). The child node chooses the first live parent listed. There are two variants to this
      strategy.  When the strategy is set to "false," the child will use the first listed parent
      (for clarity called p1) until it is marked down, when the child will switch to the second
      listed parent (p2). Then, when the p1 is retested and found live, the child
      will restart using p1. When the selection strategy is set to "latched," the child will use
      p1 until it is marked down, when it will switch to p2. When p1 is retested and found live,
      the child will continue using p2 until it is marked down.

Interaction with Remap.config
-----------------------------

If remap rules are required (:ts:cv:`proxy.config.reverse_proxy.enabled`),
when a request comes in to a child node, its :file:`remap.config` is evaluated before
parent selection. This means that the client request is translated according to the
remap rule, and therefore, any parent selection should be made against the remapped
host name. This is true regardless of pristine host headers
(:ts:cv:`proxy.config.url_remap.pristine_host_hdr`) being enabled or not. The parent node
will receive the translated request (and thus needs to be configured to accept it).

:file:`remap.config` now also allows an alternative configuration that supports all the
**Parent Selection** policies and failover mentioned here using a new :file:`remap.config` tag,
**@strategy**. This eliminates the need for a second lookup against the remapped host name
required when using :file:`parent.config`.  See using NextHop strategies with :file:`remap.config`,
:doc:`../../admin-guide/files/strategies.yaml.en`

Example
~~~~~~~
The client makes a request to Traffic Server for http://example.com. The origin server
for the request is http://origin.example.com; the parent node is ``parent1.example.com``,
and the child node is configured as a reverse proxy.

If the child's :file:`remap.config` contains

``map http://example.com http://origin.example.com``

with the child's :file:`parent.config` containing

``dest_domain=origin.example.com method=get parent="parent1.example.com:80`` )

and parent cache (parent1.example.com) would need to have a :file:`remap.config`
line similar to

``map http://origin.example.com http://origin.example.com``

With this example, if parent1.example.com is down, the child node would automatically
directly contact the ``origin.example.com`` on a cache miss.


Parent Failover
---------------

Traffic Server supports use of several parent caches. This ensures that
if one parent cache is not available, another parent cache can service
client requests.

When you configure your Traffic Server to use more than one parent
cache, Traffic Server detects when a parent is not available and sends
missed requests to another parent cache. If you specify more than two
parent caches, then the order in which the parent caches are queried
depends upon the parent proxy rules configured in the :file:`parent.config`
configuration file. By default, the parent caches are queried in the
order they are listed in the configuration file.

.. _configuring-traffic-server-to-use-a-parent-cache:

Configuring Traffic Server to Use a Parent Cache
------------------------------------------------

To configure Traffic Server to use one or more parent caches, you must perform
the configuration adjustments detailed below.

.. note::

    You need to configure the child cache only. Assuming the parent nodes are
    configured to serve the child's origin server, no additional configuration is
    needed for the nodes acting as Traffic Server parent caches.

#. Identify the parent cache you want to use to service missed requests. To
   use parent failover, you must identify more than one parent cache so that
   when a parent cache is unavailable, requests are sent to another parent
   cache.

#. Edit :file:`parent.config` to set parent proxy rules which will specify the
   parent cache to which you want missed requests to be forwarded.  Or as an
   alternative to :file:`parent.config`, edit :file:`strategies.yaml` to
   specify next hop parent proxy rules and hosts.  Then in :file:`remap.config`
   use the **@strategy** tag to select the next hop parent proxy rules. See
   :doc:`../../admin-guide/files/strategies.yaml.en`

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

Run the command :option:`traffic_ctl config reload` to apply the configuration changes.
