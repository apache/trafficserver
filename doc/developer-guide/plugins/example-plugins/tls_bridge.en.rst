.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: ../../../common.defs

.. highlight:: text
.. default-domain:: cpp
.. |Name| replace:: TLS Bridge

|Name|
**********

This plugin is used to provide TLS tunnels for connections between a Client and a Service via two
gateway |TS| instances using explicit proxying. By configuring the |TS| instances the level of
security in the tunnel can be easily controlled for all communications across the tunnels without
having to update the client or service.

Description
===========

The tunnel is sustained by two instances of |TS|.

.. uml::
   :align: center

   hide empty members

   cloud "Cloud\nUntrusted\nNetworks" as Cloud
   node "Ingress ATS"
   node "Peer ATS"

   [Client] <--> [Ingress ATS] : Unsecure
   [Ingress ATS] <-> [Cloud] : Secure
   [Cloud] <-> [Peer ATS] : Secure
   [Peer ATS] <-u-> [Service] : Unsecure

   [Ingress ATS] ..> [tls_bridge\nPlugin] : Uses


The ingress |TS| accepts an HTTP ``CONNECT`` request from the Client. This connection gets
intercepted by the |Name| plugin inside |TS| if the destination matches one of the configured
destinations. The plugin then makes a TLS connection to the peer |TS| using the configured level of
security. The original ``CONNECT`` request from the Client to the ingress |TS| is then sent to the
peer |TS| to create a connection from the peer |TS| to the Service. After this the Client has a
virtual circut to the Service and can use any TCP based communication (including TLS). Effectively
the plugin causes the explicit proxy to work as if the Client had done the ``CONNECT`` directly to
the peer |TS|. Note this means the DNS lookup for the Service is done by the peer |TS|, not the
ingress |TS|.

The plugin is configured with a mapping of Service names to peer |TS| instances. The Service names
are URLs which will be in the original HTTP request made by the Client after connecting to the
ingress |TS|. This means the FQDN for the Service is resolved in the environment of the peer
|TS| and not the ingress |TS|.

Configuration
=============

|Name| requires at least two instances of |TS| (Ingress and Peer). The client connects to the
ingress |TS|, and the peer |TS| connects to the service.

#. Disable caching on |TS| in ``records.config``::

      CONFIG proxy.config.http.cache.http INT 0

#. Configure the ports.

   *  The Peer |TS| must be listening on an SSL enabled proxy port. For instance, if the proxy port
      for the Peer is 4443, then configuration in ``records.config`` would have::

         CONFIG proxy.config.http.server_ports STRING 4443:ssl

   *  The Ingress |TS| must allow ``CONNECT`` to the Peer proxy port. This would be set in
      ``records.config`` by::

         CONFIG proxy.config.http.connect_ports STRING 4443

      The Ingress |TS| also needs ``proxy.config.http.server_ports`` configured to have proxy ports
      to which the Client can connect.

#. Remap on the ingress is not required, however, |TS| requires remap in order to accept the
   request. This can be done by disabling the remap requirement::

      CONFIG proxy.config.url_remap.remap_required INT 0

   In this case |TS| will act as an open proxy which is unlikely to be a good idea, therefore if
   this approach is used |TS| will need to run in a restricted environment or use access control
   (via ``ip_allow.config`` or ``iptables``).

   If this is unsuitable then an identity remap rule can be added for the peer |TS|. If the peer
   |TS| was named "peer.ats" and it listens on port 4443, then the remap rule would be ::

      remap https://peer.ats:4443 https://peer.ats:4443

   Remapping will be disabled for the user agent connection and so it will not need a rule.

#. If remap is required on the peer to enable the outbound connection from the peer to the service
   (it is not explicitly disabled) the destination port must be
   explicitly stated [#]_. E.g. ::

      map https://service:4443 https://service:4443

   Note this remap rule cannot alter the actual HTTP transactions between the client and service
   because those happen inside what is effectively a tunnel between the client and service, supported
   by the two |TS| instances. This rule just allows the ``CONNECT`` sent from the ingress to cause a
   tunnel connection from the peer to the service.

#. Configure the Ingress |TS| to verify the Peer server certificate::

      CONFIG proxy.config.ssl.client.verify.server.policy STRING ENFORCED

#. Configure Certificate Authority used by the Ingress |TS| to verify the Peer server certificate. If this
   is a directory all of the certificates in the directory are treated as Certificate Authorites. ::

      CONFIG proxy.config.ssl.client.CA.cert.filename STRING </path/to/CA_certificate_file_name>

#. Configure the Ingress |TS| to provide a client certificate::

      CONFIG proxy.config.ssl.client.cert.path STRING </path/to/certificate/dir>
      CONFIG proxy.config.ssl.client.cert.filename STRING <server_certificate_file_name>

#. Configure the Peer |TS| to verify the Ingress client certificate::

      CONFIG proxy.config.ssl.client.certification_level INT 2

#. Enable the |Name| plugin in ``plugin.config``. The plugin is configured by arguments in
   ``plugin.config``. These are arguments are in pairs of a *destination* and a *peer*. The
   destination is an anchored regular expression which is matched against the host name in the Client
   ``CONNECT``. The destinations are checked in order and the first match is used to select the peer
   |TS|. The peer should be an FQDN or IP address with an optional port. For the example above, if
   the Peer |TS| was named "peer.ats" on port 4443 and the Service at ``*.service.com``, the
   peer argument would be "peer.ats:4443". In ``plugin.config`` this would be::

      tls_bridge.so .*[.]service[.]com peer.ats:4443

   Note the '.' characters are escaped with brackets so that, for instance, "someservice.com" does
   not match the rule.

   If there was another service, "\*.altsvc.ats", via a different peer "altpeer.ats" on port 4443,
   the configuration would be ::

      tls_bridge.so .*[.]service[.]com peer.ats:4443 .*[.]altsvc.ats altpeer.ats:4443

   Mappings can also be specified in an external file. For instance, if the file named "bridge.config" in the default |TS|
   configuration directory contained mappings, the ``plugin.config`` configuration line could look
   like ::

      tls_bridge.so .*[.]service[.]com peer.ats:4443 --file bridge.config

   or

      tls_bridge.so --file bridge.config .*[.]service[.]com peer.ats:4443

   These are not identical - direct mappings and file mappings are processed in order. This means in
   the first example, the direct mapping is checked before any mappping in "bridge.config", and in
   the latter example the mappings in "bridge.config" are checked before the direct mapping. There
   can be multiple "--file" arguments, which are processed in the order they appear in
   "plugin.config". The file name can be absolute, or relative. If the file name is relative, it is
   relative to the |TS| configuration directory. Therefore, in these examples, "bridge.config" must
   be in the same directory as ``plugin.config``.

   The contents of "bridge.config" must be one mapping per line, with a regular expression separated
   by white space from the destination service. This is identical to the format in ``plugin.config``
   except there is only one pair per line. E.g., valid content for "bridge.config" could be ::

      # Primary service location.
      .*[.]service[.]com peer.ats:4443

      # Secondary.
      .*[.]altsvc.ats      altpeer.ats:4443

   Leading whitespace on a line is ignored, and if the first non-whitespace character is '#' then
   the entire line is ignored. Therefore if that is the content of "bridge.config", these two
   lines in "plugin.config" would behave identically ::

      tls_bridge.so --file bridge.config

      tls_bridge.so .*[.]service[.]com peer.ats:4443     .*[.]altsvc.ats altpeer.ats:4443

Notes
=====

|Name| is distinct from more basic Layer 4 Routing available in |TS|. For the latter there is no
intercept or change of the TLS exchange between the Client and the Service. The exchange looks like
this

.. uml::
   :align: center

   actor Client
   participant "Ingress TS" as Ingress
   participant Service

   Client <-[#green]> Ingress : //TCP Connect//
   Client -[#blue]-> Ingress : <font color="blue">TLS: ""CLIENT HELLO""</font>
   note over Ingress : Map SNI to upstream Service
   Ingress <-[#green]> Service : //TCP Connect//
   Ingress -[#blue]-> Service : <font color="blue">TLS: ""CLIENT HELLO""</font>
   note right : Duplicate of data from Client.
   note over Ingress : Forward bytes between Client <&arrow-thick-left> <&arrow-thick-right> Service
   Client <--> Service

The key points are

*  |TS| does no TLS negotiation at all. The properties of the connection between the Ingress |TS|
   and the Service are completely determined by the Client and Server negotation.
*  No packets are modified, the ""CLIENT HELLO"" sent by the Ingress |TS| is an exact copy of that
   sent to the Ingress |TS| by the Client. It is only examined for the SNI data in order to select
   the Service.

Implementation
==============

The |Name| plugin uses :code:`TSHttpTxnIntercept` to gain control of the ingress Client session.
If the session is valid then a separate connection to the peer |TS| is created using
:code:`TSHttpConnect`.

After the ingress |TS| connects to the peer |TS| it sends a duplicate of the Client ``CONNECT``
request. This is processed by the peer |TS| to connect to the Service. After this both |TS|
instances then tunnel data between the Client and the Service, in effect becoming a transparent
tunnel.

The overall exchange looks like the following:

.. uml::
   :align: center

   @startuml

   box "Client Network" #DDFFDD
   actor Client
   entity "User Agent\nVConn" as lvc
   participant "Ingress ATS" as ingress
   entity "Upstream\nVConn" as rvc
   end box
   box "Corporate Network" #DDDDFF
   participant "Peer ATS" as peer
   database Service
   end box

   Client -> ingress : TCP or TLS connect
   activate lvc
   Client -> ingress : HTTP CONNECT
   ingress -> lvc : Intercept Transaction
   ingress -> peer : TLS connect
   activate rvc
   note over ingress,peer : Secure Tunnel
   ingress -> peer : HTTP CONNECT
   note over peer : DNS for Service is\ndone here.
   peer -> Service : TCP Connect

   note over Client, Service : At this point data can flow between the Client and Server\nover the secure link as a virtual connection, including any TLS handshake.
   Client <--> Service
   lvc <-> ingress : <&arrow-thick-left> Move data <&arrow-thick-right>
   ingress <-> rvc : <&arrow-thick-left> Move data <&arrow-thick-right>
   note over ingress : Plugin explicitlys moves this data.

   @enduml


A detailed view of the plugin operation.

.. image:: ../../../uml/images/TLS-Bridge-Plugin.svg
   :align: center

A sequence diagram focusing on the request / response data flow. There is a :code:`NetVConn` for the
connection to the Peer |TS| which is omitted for clarity.

*  Blue dotted lines are request or response data
*  Green lines are network connections.
*  Red lines are programmatic interactions.
*  Black lines are hook call backs.

The :code:`200 OK` sent from the Peer |TS| is parsed and consumed by the plugin. An non-:code:`200` response
means there was an error and the tunnel is shut down. To deal with the Client response clean up the
response code is stored and used later during cleanup.

.. image:: ../../../uml/images/TLS-Bridge-Messages.svg
   :align: center

A restartable state machine is used to recognize the end of the Peer |TS| response. The initial part
of the response is easy because all that is needed is to wait until there is sufficient data for a
minimal parse. The end can be an arbitrary distance in to the stream and may not all be in the same
socket read.

.. uml::
   :align: center

   @startuml
   [*] -r> State_0
   State_0 --> State_1 : CR
   State_1 --> State_0 : *
   State_1 --> State_1 : CR
   State_1 --> State_2 : LF
   State_2 --> State_3 : CR
   State_2 --> State_0 : *
   State_3 -r> [*] : LF
   State_3 --> State_1 : CR
   State_3 --> State_0 : *
   @enduml

Debugging
---------

Debugging messages for the plugin can be enabled with the "tls_bridge" debug tag.


.. rubric:: Footnotes

.. [#] This is likely due to a bug in |TS|, currently under investigation.
