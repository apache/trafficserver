.. _security-options:

Security Options
****************

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

Traffic Server provides a number of security features.

.. _controlling-client-access-to-cache:

Controlling Client Access to the Proxy Cache
============================================

You can configure Traffic Server to allow only certain clients to use
the proxy cache by editing a configuration file.

#. Add a line in :file:`ip_allow.config` for each IP address or
   range of IP addresses allowed to access Traffic Server.
#. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

.. _configuring-dns-server-selection-split-dns:

Configuring DNS Server Selection (Split DNS)
============================================

The **Split DNS** option enables you to configure Traffic Server to use
multiple DNS servers, as dictated by your security requirements. For
example, you might configure Traffic Server to use one set of DNS
servers to resolve hostnames on your internal network, while allowing
DNS servers outside the firewall to resolve hosts on the Internet. This
maintains the security of your intranet, while continuing to provide
direct access to sites outside your organization.

To configure Split DNS, you must do the following:

-  Specify the rules for performing DNS server selection based on the
   destination domain, the destination host, or a URL regular
   expression.
-  Enable the **Split DNS** option.

To do this, we

#. Add rules to :file:`splitdns.config`.
#. In :file:`records.config` set the variable
   :ts:cv:`proxy.config.dns.splitDNS.enabled` to ``1`` to enable split DNS.
#. Run the command :option:`traffic_line -x` to apply the configuration
   changes.

.. _configuring-ssl-termination:

Using SSL Termination
=====================

The Traffic Server **SSL termination** option enables you to secure
connections in reverse proxy mode between a client and a Traffic Server
and/or Traffic Server and an origin server.

The following sections describe how to enable and configure the SSL
termination option.

-  Enable and configure SSL termination for client/Traffic Server
   connections: :ref:`client-and-traffic-server-connections`
-  Enable and configure SSL termination for Traffic Server/origin server
   connections: :ref:`traffic-server-and-origin-server-connections`
-  Enable and configure SSL termination for both client/Traffic Server
   and Traffic Server/origin server connections:  :ref:`client-and-traffic-server-connections`
   :ref:`traffic-server-and-origin-server-connections`, respectively.

.. _client-and-traffic-server-connections:

Client and Traffic Server Connections
-------------------------------------

The figure below illustrates communication between a client and Traffic
Server (and between Traffic Server and an origin server) when the SSL
termination option is enabled & configured for **client/Traffic
Server connections only**.

.. figure:: ../static/images/admin/ssl_c.jpg
   :align: center
   :alt: Client and Traffic Server communication using SSL termination

   Client and Traffic Server communication using SSL termination

The figure above depicts the following:

#. The client sends an HTTPS request for content. Traffic Server receives the request and performs the SSL 'handshake' to authenticate the client (depending on the authentication options configured) and determine the encryption method that will be used. If the client is allowed access, then Traffic Server checks its cache for the requested content.  

#. If the request is a cache hit and the content is fresh, then Traffic Server encrypts the content and sends it to the client. The client decrypts the content (using the method determined during the handshake) and displays it.

#. If the request is a cache miss or cached content is stale, then Traffic Server communicates with the origin server via HTTP and obtains a plain text version of the content. Traffic Server saves the plain text version of the content in its cache, encrypts the content, and sends it to the client. The client decrypts and displays the content.

To configure Traffic Server to use the SSL termination option for
client/Traffic Server connections, you must do the following:

-  Obtain and install an SSL server certificate from a recognized
   certificate authority. The SSL server certificate contains
   information that enables the client to authenticate Traffic Server
   and exchange encryption keys.
-  Configure SSL termination options:

   -  Set the port number used for SSL communication using :ts:cv:`proxy.config.http.server_ports`.
   -  Edit :file:`ssl_multicert.config` to specify the filename and location of the
      SSL certificates and private keys.
   -  (Optional) Configure the use of client certificates: Client
      certificates are located on the client. If you configure Traffic
      Server to require client certificates, then Traffic Server
      verifies the client certificate during the SSL handshake that
      authenticates the client. If you configure Traffic Server to *not*
      require client certificates, then access to Traffic Server is
      managed through other Traffic Server options that have been set
      (such as rules in :file:`ip_allow.config`).
   -  (Optional) Configure the use of Certification Authorities (CAs).
      CAs add security by verifying the identity of the person
      requesting a certificate.

In order to accomplish this, we

#. Edit the following variables in the :ref:`records-config-ssl-termination` section of
   :file:`records.config`

   -  :ts:cv:`proxy.config.http.server_ports`
   -  :ts:cv:`proxy.config.ssl.client.certification_level`
   -  :ts:cv:`proxy.config.ssl.server.cert.path`
   -  :ts:cv:`proxy.config.ssl.server.private_key.path`
   -  :ts:cv:`proxy.config.ssl.CA.cert.path`

#. Run the command :option:`traffic_line -L` to restart Traffic Server on the
   local node or :option:`traffic_line -M` to restart Traffic Server on all
   the nodes in a cluster.


.. This numbering is ridiculous.

.. _traffic-server-and-origin-server-connections:

Traffic Server and Origin Server Connections
--------------------------------------------

The figure below illustrates communication between Traffic Server and an
origin server when the SSL termination option is enabled for **Traffic
Server/origin server connections**.

.. figure:: ../static/images/admin/ssl_os.jpg
   :align: center
   :alt: Traffic Server and origin server communication using SSL termination

   Traffic Server and origin server communication using SSL termination

The figure above depicts the following:

**Step 1:** If a client request is a cache miss or is stale, then
Traffic Server sends an HTTPS request for the content to the origin
server. The origin server receives the request and performs the SSL
handshake to authenticate Traffic Server and determine the encryption
method to be used.

**Step 2:** If Traffic Server is allowed access, then the origin server
encrypts the content and sends it to Traffic Server, where it is
decrypted (using the method determined during the handshake). A plain
text version of the content is saved in the cache.

**Step 3:** If SSL termination is enabled for client /Traffic Server
connections, then Traffic Server re-encrypts the content and sends it to
the client via HTTPS, where it is decrypted and displayed. If SSL
termination is not enabled for client/Traffic Server connections, then
Traffic Server sends the plain text version of the content to the client
via HTTP.

To configure Traffic Server to use the SSL termination option for
Traffic Server and origin server connections, you must do the following:

-  Obtain and install an SSL client certificate from a recognized
   certificate authority. The SSL client certificate contains
   information that allows the origin server to authenticate Traffic
   Server (the client certificate is optional).
-  Configure SSL termination options:
-  Enable the SSL termination option.

   -  Set the port number used for SSL communication.
   -  Specify the filename and location of the SSL client certificate
      (if you choose to use a client certificate).
   -  Specify the filename and location of the Traffic Server private
      key (if the private key is not located in the client certificate
      file). Traffic Server uses its private key during the SSL
      handshake to decrypt the session encryption keys. The private key
      must be stored and protected against theft.
   -  Configure the use of CAs. CAs allow the Traffic Server that's
      acting as a client to verify the identity of the server with which
      it is communicating, thereby enabling exchange of encryption keys.

In order to accomplish this, we:

.. This numbering is ridiculous. I need to re-read this doc with a fresh mind and re(number|order) it.

1. Edit the following variables in the :ref:`records-config-ssl-termination` section of
   :file:`records.config`:

   -  :ts:cv:`proxy.config.ssl.auth.enabled`
   -  :ts:cv:`proxy.config.http.server_ports`
   -  :ts:cv:`proxy.config.ssl.client.verify.server`
   -  :ts:cv:`proxy.config.ssl.client.cert.filename`
   -  :ts:cv:`proxy.config.ssl.client.cert.path`
   -  :ts:cv:`proxy.config.ssl.client.private_key.filename`
   -  :ts:cv:`proxy.config.ssl.client.private_key.path`
   -  :ts:cv:`proxy.config.ssl.client.CA.cert.filename`
   -  :ts:cv:`proxy.config.ssl.client.CA.cert.path`

2. Run the command :option:`traffic_line -L` to restart Traffic Server on the
   local node or :option:`traffic_line -M` to restart Traffic Server on all
   the nodes in a cluster.

