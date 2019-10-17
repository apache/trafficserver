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

.. include:: ../common.defs

Host Resolution Proposal
************************

Introduction
------------

The current mechanism for resolving host names to IP addresses for Traffic Server is contained the HostDB and DNS
libraries. These take hostnames and provide IP addresses for them.

The current implementation is generally considered inadequate, both from a functionality point of view and difficulty in
working with it in other parts of Traffic Server. As Traffic Server is used in more complex situations this inadequacy
presents increasing problems.

Goals
-----

Updating the host name resolution (currently referred to as "HostDB") has several functions goals

*  Enable additional processing layers to be easily added.
*  Enable plugins to directly access the name resolution logic
*  Enable plugins to provide name resolution
*  Asynchronous (immediate resolve or callback on block)
*  Minimize allocations -- in particular no allocations for cached resolutions
*  Simplify interactions with the resolution, particularly with regard to nameservers, origin server failover, and
   address family handling.

It is also necessary to support a number of specific features that are either currently available or strongly desired.

*  SplitDNS or its equivalent
*  Use of a hosts file (e.g. ``/etc/hosts``)
*  Simultaneous IPv4 and IPv6 queries
*  IP family control
*  Negative caching
   *  Server connection failures
   *  Query failures
   *  Nameserver failures.
*  Address validity time out control
*  Address round robin support
*  SRV record support (weighted records)
*  Nameserver round robin
*  Plugin access to nameserver data (add, remove, enumerate)
*  Plugin provision of resolvers.
*  Hooks for plugin detection / recovery from resolution events.

One issue is persistence of the cached resolutions. This creates problems for the current implementation (because of
size limits it imposes on the cached data) but also allows for quicker restarts in a busy environment.

Basics
------

The basic design is to separate the functionality into chainable layers so that a resolver with the desired attributes
can be assembled from those layers. The core interface is that of a lazy iterator. This object returns one of four
results when asked for an address

* An IP address
* Done(no more addresses are available)
* Wait(an address may be available in the future)
* Fail (no address is available and none will be so in the future)

Each layer (except the bottom) uses this API and also provides it. This enables higher level logic such as the state
machine to simply use the resolver as a list without having to backtrack states in the case of failures, or have special
cases for different resolution sources.

To perform a resolution, a client creates a query object (potentially on the stack), initializes it with the required
data (at least the hostname) and then starts the resolution. Methods on the query object allow its state and IP address
data to be accessed.

Required Resolvers
------------------

Nameserver
   A bottom level resolver that directly queries a nameserver for DNS data. This contains much of the functionality
   currently in the ``iocore/dns`` directory.

SplitDNS
   A resolver that directs requests to one of several resolvers. To emulate current behavior these would be Nameserver
   instances.

NameserverGroup
   A grouping mechanism for Nameserver instances that provides failover, round robin, and ordering capabilities. It may be
   reasonable to merge this with the SplitDNS resolver.

HostFile
   A resolver that uses a local file to resolve names.

AddressCache
   A resolver that also has a cache for resolution results. It requires another resolver instance to perform the actual
   resolution.

Preloaded
   A resolver that can contain one or more explicitly set IP addresses which are returned. When those are exhausted it
   falls back to another resolver.

Configuration
-------------

To configuration the resolution, each resolver would be assigned a tag. It is not, however, sufficient to simply provide
the list of resolver tags because some resolvers require additional configuration. Unfortunately this will likely
require a separate configuration file outside of :file:`records.config`, although we would be able to remove
:file:`splitdns.config`. In this case we would need chain start / end markers around a list of resolver tags. Each tag
would the be able to take additional resolver configuration data. For instance, for a SplitDNS resolver the nameservers.

Examples
--------

Transparent operations would benefit from the *Preloaded* resolver. This would be loaded with the origin host address
provided by the client connection. This could be done early in processing and then no more logic would be required to
skip DNS processing as it would happen without additional action by the state machine. It would handle the problem of de
facto denial of service if an origin server becomes unavailable in that configuration, as *Preloaded* would switch to
alternate addresses automatically.

Adding host file access would be easier as well, as it could be done in a much more modular fashion and then added to
the stack at configuration time. Whether such addresses were cached would be controlled by chain arrangement rather yet
more configuration knobs.

The default configuration would be *Preloaded* : *AddressCache* : *Nameserver*.

In all cases the state machine makes requests against the request object to get IP addresses as needed.

Issues
------

Request object allocation
=========================

The biggest hurdle is being able to unwind a resolver chain when a block is encountered. There are some ways to deal with this.

1) Set a maximum resolver chain length and declare the request instance so that there is storage for state for that many
resolvers. If needed and additional value of maximum storage per chain could be set as well. The expected number of
elements in a chain is expected to be limited, 10 would likely be a reasonable limit. If settable at source
configuration time this should be sufficient.

2) Embed class allocators in resolver chains and mark the top / outermost / first resolver. The maximum state size for a
resolution can be calculated when the chain is created and then the top level resolver can use an allocation pool to
efficiently allocate request objects. This has an advantage that with a wrapper class the request object can be passed
along cheaply. Whether that's an advantage in practice is unclear.

Plugin resolvers
================

If plugins can provide resolvers, how can these can integrated in to existing resolver chains for use by the HTTP SM for
instance?

Feedback
========

It should be possible for a client to provide feedback about addresses (e.g., the origin server at this address is not
available). Not all resolvers will handle feedback but some will and that must be possible.

Related to this is that caching resolvers (such as *AddressCache*) must be able to iterator over all resolved addresses
even if their client does not ask for them. In effect they must background fill the address data.
