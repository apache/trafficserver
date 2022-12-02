WCCP Configuration
******************

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

.. _wccp-configuration:

.. toctree::
   :maxdepth: 2

`WCCP <http://cio.cisco.com/en/US/docs/ios/12_0t/12_0t3/feature/guide/wccp.html>`_
is de-facto semi-standard used by routers to redirect network traffic to
caches. It is available on most Cisco™ routers although it does not
appear to be officially supported by Cisco. The primary benefits of WCCP
are

-  If already have a router that supports WCCP inline you do not
   have to change your network topology.
-  WCCP fails open so that if the Traffic Server machine fails, it is
   bypassed and users continue to have Internet access.

Use of WCCP only makes sense for client side transparency [1]_
because if the clients are explicitly proxied by Traffic Server there's
no benefit to WCCP fail open, as the clients will continue to directly
access the unresponsive Traffic Server host. It would be better to
adjust the routing tables on the router for explicit proxying.

Because the router serves as the inline network element, Traffic Server
must run on a separate host. This host can be located anywhere as long
as Traffic Server is either on the same network segment or a GRE tunnel
can be maintained between the Traffic Server host and the router.

|important| This document presumes that the router is already properly
configured to handle traffic between the clients and the origin servers.
If you are not certain, verify it before attempting to configure Traffic
Server with WCCP. This is also a good state to which to revert should
the configuration go badly.

Configuration overview
======================

Setting WCCP is a three step process, first configuring the router, the
Traffic Server host, and Traffic Server.

|image1| The router will **not** respond to WCCP protocol packets unless
explicitly configured to do so. Via WCCP, the router can be made to
perform packet interception and redirection needed by Traffic Server
transparency. The WCCP protocol in effect acts as means of controlling a
rough form of policy routing with positive heartbeat cutoff.

The Traffic Server host system must also be configured using
``iptables`` to accept connections on foreign addresses. This is done
roughly the same way as the standard transparency use.

Finally Traffic Server itself must be configured for transparency and
use of WCCP. The former is again very similar to the standard use, while
WCCP configuration is specific to WCCP and uses a separate configuration
file, referred to by the :file:`records.yaml` file.

The primary concern for configuration in which of three basic topologies
are to be used.

-  Dedicated -- Traffic Server traffic goes over an interface that is
   not used for client nor origin server traffic.
-  Shared -- Traffic Server traffic shares an interface with client or
   server traffic.
-  Inside Shared -- Traffic Server and client traffic share an
   interface.
-  Outside Shared -- Traffic Server and
   origin server traffic share an interface.

In general the dedicated topology is preferred. However, if the router
has only two interfaces one of the shared topologies will be
required [2]_ Click the links above for more detailed configuration
information on a specific topology.

Shared interface issues
-----------------------

A shared interface topology has additional issues compared to a
dedicated topology that must be handled. Such a topology is required if
the router has only two interfaces, and because of these additional
issues it is normally only used in such cases, although nothing prevents
it use even if the router has three or more interfaces.

The basic concept for a shared interface is to use a tunnel to simulate
the dedicated interface case. This enables the packets to be
distinguished at layer 3. For this reason, layer 2 redirection cannot be
used because the WCCP configuration cannot distinguish between packets
returning from the origin server and packets returning from Traffic
Server as they are distinguished only by layer 2 addressing [3]_.
Fortunately the GRE tunnel used for packet forwarding and return can be
used as the simulated interface for Traffic Server.

Frequently encountered problems
-------------------------------

MTU and fragmentation
~~~~~~~~~~~~~~~~~~~~~

In most cases the basic configuration using a tunnel in any topology can
fail due to issues with fragmentation. The socket logic is unable to
know that its packets will eventually be put in to a tunnel which will
by its nature have a smaller
`MTU <http://en.wikipedia.org/wiki/Maximum_transmission_unit>`_ than the
physical interface which it uses. This can lead to pathological behavior
or outright failure as the packets sent are just a little too big. It is
not possible to solve easily by changing the MTU on the physical
interface because the tunnel interface uses that to compute its own MTU.

References
==========

-  `WCCP Router Configuration Commands - IOS
   12.2 <http://www.cisco.com/en/US/docs/ios/12_2/configfun/command/reference/frf018.html>`_








.. [1]
   Server side transparency should also be used, but it is not as
   significant. In its absence, however, origin servers may see the
   source address of connections suddenly change from the Traffic Server
   address to client addresses, which could cause problems. Further, the
   primary reason for not having server side transparency is to hide
   client addresses which is defeated if the Traffic Server host fails.

.. [2]
   If your router has only one interface, it's hardly a *router*.

.. [3]
   This is not fundamentally impossible, as the packets are distinct in
   layer

.. |important| image:: ../../../static/images/docbook/important.png
.. |image1| image:: ../../../static/images/docbook/important.png

