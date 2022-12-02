Inline on a Linux router
************************

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

The routed set up presumes the set of clients are on distinct networks
behind a single physical interface. For the purposes of this example
will we presume

-  The clients are on network 172.28.56.0/24
-  The router connects the networks 172.28.56.0/24 and 192.168.1.0/24
-  Interface ``eth0`` is on the network 192.168.1.0/24
-  Interface ``eth1`` is on the network 172.28.56.0/24
-  The router is already configured to route traffic correctly for the
   clients.

In this example we will intercept port 80 (HTTP) traffic that traverses
the router. The first step is to use ``iptables`` to handle IP packets
appropriately.

::

    # reflow client web traffic to TPROXY
    iptables -t mangle -A PREROUTING -i eth1 -p tcp -m tcp --dport 80 -j TPROXY \
       --on-ip 0.0.0.0 --on-port 8080 --tproxy-mark 1/1
    # Let locally directed traffic pass through.
    iptables -t mangle -A PREROUTING -i eth0 --source 192.168.1.0/24 -j ACCEPT
    iptables -t mangle -A PREROUTING -i eth0 --destination 192.168.1.0/24 -j ACCEPT
    # Mark presumed return web traffic
    iptables -t mangle -A PREROUTING -i eth0 -p tcp -m tcp --sport 80 -j MARK --set-mark 1/1

We mark packets so that we can use policy routing on them. For inbound
packets we use ``TPROXY`` to make it possible to accept packets sent to
foreign IP addresses. For returning outbound packets there will be a
socket open bound to the foreign address, we need only force it to be
delivered locally. The value for ``--on-ip`` is 0 because the target
port is listening and not bound to a specific address. The value for
``--on-port`` must match the Traffic Server server port. Otherwise its
value is arbitrary. ``--dport`` and ``--sport`` specify the port from
the point of view of the clients and origin servers. The middle two
lines exempt local web traffic from being marked for Traffic Server --
these rules can be tightened or loosened as needed. They server by
matching traffic and exiting the ``iptables`` processing via ``ACCEPT``
before the last line is checked.

Once the flows are marked we can force them to be delivered locally via
the loopback interface via a policy routing table.

::

    ip rule add fwmark 1/1 table 1
    ip route add local 0.0.0.0/0 dev lo table 1

The marking used is arbitrary but it must be consistent between
``iptables`` and the routing rule. The table number must be in the range
1..253.

To configure Traffic Server set the following values in
:file:`records.yaml`

``proxy.config.http.server_ports``
    ``STRING``
    Default: *value from* ``--on-port``

``proxy.config.reverse_proxy.enabled``
    ``INT``
    Default: ``1``

``proxy.config.url_remap.remap_required``
    ``INT``
    Default: ``0``

