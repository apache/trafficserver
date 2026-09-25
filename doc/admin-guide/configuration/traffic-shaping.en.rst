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

.. default-domain:: cpp

.. _admin-traffic-shaping:

Traffic Shaping
***************

|TS| does not shape traffic itself. Instead, it marks the packets it sends,
and a separate traffic shaper acts on those marks. On Linux the shaper is
usually the ``tc`` command from `iproute2
<https://wiki.linuxfoundation.org/networking/iproute2>`_, often together with
``iptables`` or ``nftables``. On BSD systems it is ALTQ, typically configured
through ``pf``. The shaper can also be a separate router or switch that
understands DSCP. This split lets you use the full feature set of the standard
tools and shape |TS| traffic together with everything else on the host or
network.

|TS| can apply two kinds of marks:

Packet mark (``SO_MARK``)
   A 32-bit value attached to each packet inside the Linux kernel. It never
   leaves the host, so the shaper must run on the same machine as |TS|. Linux
   requires ``CAP_NET_ADMIN`` to set it. When |TS| is built with POSIX
   capability support it keeps that capability after switching to its
   unprivileged user. On platforms without ``SO_MARK`` the setting is ignored.

DSCP (``IP_TOS`` / ``IPV6_TCLASS``)
   The Differentiated Services field in the IPv4 or IPv6 header. It travels
   with the packet, so a shaper on another device can act on it.

Marks apply to packets that |TS| sends. The *client* side covers responses
sent to user agents. The *origin* side covers requests sent to origin servers.
Packets that |TS| receives, such as origin responses, carry whatever marking
the sender chose. See `Classifying received traffic`_ for how to shape
those.

Enabling marking
================

The mark and DSCP values are only applied to a socket when the matching bit is
set in the socket option flags. This is true for the configuration variables,
the plugin API and the :ref:`admin-plugins-header-rewrite` operators alike.
Both flag variables default to ``0x1`` (``TCP_NODELAY`` only), so marking is
off until you turn it on.

============================================== =========== ==========
Variable                                       PACKET_MARK PACKET_TOS
============================================== =========== ==========
:ts:cv:`proxy.config.net.sock_option_flag_in`  ``16``      ``32``
:ts:cv:`proxy.config.net.sock_option_flag_out` ``16``      ``32``
============================================== =========== ==========

The flags are a bitmask, so add these bits to the ones you already use. For
example, to keep ``TCP_NODELAY`` and enable both kinds of marking on client and
origin connections:

.. code-block:: yaml

   records:
     net:
       sock_option_flag_in: 49   # TCP_NODELAY (1) + PACKET_MARK (16) + PACKET_TOS (32)
       sock_option_flag_out: 49

:ts:cv:`proxy.config.net.sock_option_flag_in` is read when the listening
sockets are created and requires a restart.
:ts:cv:`proxy.config.net.sock_option_flag_out` is overridable, but keep the
marking bits on globally and vary only the values per transaction (see below).
|TS| only touches a socket's mark when the bit is set, so an origin connection
reused by a transaction with the bit off keeps whatever mark it already had.

Setting marks
=============

Configuration variables
-----------------------

============================================== =========== ========================
Variable                                       Applies to  Scope
============================================== =========== ========================
:ts:cv:`proxy.config.net.sock_packet_mark_in`  Client side Global, restart
:ts:cv:`proxy.config.net.sock_packet_tos_in`   Client side Global, restart
:ts:cv:`proxy.config.net.sock_packet_mark_out` Origin side Global or per transaction
:ts:cv:`proxy.config.net.sock_packet_tos_out`  Origin side Global or per transaction
============================================== =========== ========================

The ``_in`` variables set the marks on every accepted client connection. The
``_out`` variables are overridable. Override them per remap rule with
:ref:`admin-plugins-conf-remap`, with the ``set-config`` operator of
:ref:`admin-plugins-header-rewrite`, or from a plugin with
:func:`TSHttpTxnConfigIntSet` (keys ``TS_CONFIG_NET_SOCK_PACKET_MARK_OUT`` and
``TS_CONFIG_NET_SOCK_PACKET_TOS_OUT``). The origin side values are applied when
the transaction opens a new origin connection or picks up a pooled one, so set
them no later than the remap or read request header stage.

Plugin API
----------

====================================== =========== ===========
Function                               Applies to  Argument
====================================== =========== ===========
:func:`TSHttpTxnClientPacketMarkSet`   Client side Packet mark
:func:`TSHttpTxnClientPacketDscpSet`   Client side DSCP value
:func:`TSHttpTxnServerPacketMarkSet`   Origin side Packet mark
:func:`TSHttpTxnServerPacketDscpSet`   Origin side DSCP value
====================================== =========== ===========

The client side functions change the live client connection immediately. The
origin side functions change the current origin connection if there is one,
and also override the ``_out`` variable for the transaction so that a later
origin connection gets the same value.

A client side mark is a socket option, so it stays on the client connection
after the transaction ends. Later requests on the same keep-alive connection,
and other HTTP/2 streams sharing it, go out with the same marking until
something changes it again. An origin connection taken from the pool gets the
``_out`` values of the transaction that uses it, but only when that
transaction has the matching bit set in
:ts:cv:`proxy.config.net.sock_option_flag_out`. Otherwise it keeps the marking
from its previous use. To send some traffic unmarked, leave the bit on and set
the value to ``0``, which clears the mark or DSCP.

To set a socket option that |TS| does not manage, such as ``SO_PRIORITY`` for
the 802.1Q priority code point, get the descriptor with
``TSHttpTxnClientFdGet()``, ``TSHttpSsnClientFdGet()`` or
``TSHttpTxnServerFdGet()`` and call ``setsockopt()`` yourself.

The :ref:`Lua plugin <admin-plugins-ts-lua>` exposes the same four functions as
``ts.http.client_packet_mark_set``, ``ts.http.client_packet_dscp_set``,
``ts.http.server_packet_mark_set`` and ``ts.http.server_packet_dscp_set``.

header_rewrite operators
------------------------

:ref:`admin-plugins-header-rewrite` provides ``set-conn-mark`` and
``set-conn-dscp``, which call the client side functions above. They can be used
in the remap, read request header and send response header hooks. Combined
with the ``CACHE`` condition, they can mark cache hits differently from misses.
For the origin side, use ``set-config`` with the ``_out`` variables.

Choosing values
===============

The ``sock_packet_tos_*`` variables take the whole 8-bit field. The DSCP
functions and ``set-conn-dscp`` take the 6-bit DSCP code point and shift it
left by two bits. For example, DSCP ``CS1`` (8) is
``sock_packet_tos_out: 32`` (``0x20``) but ``set-conn-dscp 8``. The two low
bits of the field are used for Explicit Congestion Notification. Standard code
points are listed in the `IANA DSCP registry
<https://www.iana.org/assignments/dscp-registry/dscp-registry.xhtml>`_.

:ref:`admin-plugins-conf-remap` and ``set-config`` parse integers as decimal,
so write ``32`` rather than ``0x20`` there.

Shaping with Linux tc
=====================

The examples below assume that |TS| sends client and origin traffic out of
``eth0`` and that ``tc`` runs on the same host. Adjust interface names and
rates to your network.

Shaping client responses by packet mark
---------------------------------------

This gives bulk downloads a guaranteed 1 Gbit/s that can grow to 2 Gbit/s,
while everything else shares the rest of a 10 Gbit/s link.

Enable the packet mark on client connections in :file:`records.yaml`:

.. code-block:: yaml

   records:
     net:
       sock_option_flag_in: 17   # TCP_NODELAY (1) + PACKET_MARK (16)

Mark responses for one host with :ref:`admin-plugins-header-rewrite` in
:file:`remap.config`::

   map http://downloads.example.com/ http://origin.example.com/ \
       @plugin=header_rewrite.so @pparam=bulk.conf

where ``bulk.conf`` contains::

   set-conn-mark 2

Then send packets with mark ``2`` to a separate HTB class using the ``fw``
classifier::

   tc qdisc add dev eth0 root handle 1: htb default 10
   tc class add dev eth0 parent 1: classid 1:1 htb rate 10gbit
   tc class add dev eth0 parent 1:1 classid 1:10 htb rate 9gbit ceil 10gbit prio 0
   tc class add dev eth0 parent 1:1 classid 1:20 htb rate 1gbit ceil 2gbit prio 1
   tc filter add dev eth0 parent 1: protocol all handle 2 fw classid 1:20

Shaping origin requests by DSCP
-------------------------------

When the shaper is a separate router, use DSCP instead of the packet mark.
Enable DSCP marking on origin connections:

.. code-block:: yaml

   records:
     net:
       sock_option_flag_out: 33   # TCP_NODELAY (1) + PACKET_TOS (32)

Mark traffic to a low priority origin as ``CS1`` with
:ref:`admin-plugins-conf-remap`::

   map http://backup.example.com/ http://backup-origin.example.com/ \
       @plugin=conf_remap.so @pparam=proxy.config.net.sock_packet_tos_out=32

On a Linux shaper, match the field with the ``u32`` classifier. The mask
``0xfc`` ignores the ECN bits::

   tc filter add dev eth0 parent 1: protocol ip u32 \
       match ip tos 0x20 0xfc classid 1:20
   tc filter add dev eth0 parent 1: protocol ipv6 u32 \
       match ip6 priority 0x20 0xfc classid 1:20

Classifying received traffic
----------------------------

Most of the bytes on the origin side are responses, which |TS| receives rather
than sends, so its marks are not on them. Connection tracking can carry the
mark from the request to the response. Save the packet mark that |TS| set on
outgoing packets into the connection mark::

   iptables -t mangle -A OUTPUT -m mark ! --mark 0 -j CONNMARK --save-mark
   ip6tables -t mangle -A OUTPUT -m mark ! --mark 0 -j CONNMARK --save-mark

Incoming traffic can only be shaped after it is redirected to an ``ifb``
device. The ``connmark`` action copies the connection mark back onto each
packet before the redirect, so the same ``fw`` filters work on ``ifb0``::

   ip link add ifb0 type ifb
   ip link set dev ifb0 up
   tc qdisc add dev eth0 handle ffff: ingress
   tc filter add dev eth0 parent ffff: protocol all matchall \
       action connmark action mirred egress redirect dev ifb0

Then add an HTB tree and ``fw`` filters to ``ifb0`` as in the first example.
If the shaper is on another device, have it copy the DSCP value of the request
into its connection mark instead, for example with ``-m dscp --dscp`` and
``CONNMARK --set-mark``.

Shaping per client
------------------

Without transparency, every origin connection comes from the |TS| address, so a
shaper on the origin side cannot tell clients apart. With outbound
transparency (``tr-out`` in :ts:cv:`proxy.config.http.server_ports`), origin
connections use the client's address, so the shaper can divide bandwidth by
client. The queuing discipline has to be keyed on that address: by default
``sfq`` and ``fq_codel`` hash each connection separately, so a client with many
connections still gets more. For example, with ``sfq`` and a ``flow`` filter
keyed on the source address::

   tc qdisc add dev eth0 root handle 1: sfq divisor 1024
   tc filter add dev eth0 parent 1: protocol all handle 1 \
       flow hash keys src divisor 1024

``cake`` does the same with its ``dual-srchost`` mode. See
:ref:`transparent-proxy` for the required host setup.

See Also
========

* :file:`records.yaml`
* :ref:`ts-overridable-config`
* :ref:`admin-plugins-conf-remap`
* :ref:`admin-plugins-header-rewrite`
* :ref:`transparent-proxy`
* ``tc(8)``, ``tc-htb(8)``, ``tc-fw(8)``, ``tc-u32(8)``, ``tc-connmark(8)``,
  ``tc-mirred(8)`` and ``iptables-extensions(8)``
