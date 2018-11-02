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

ssl_server_name.yaml
**********************

.. configfile:: ssl_server_name.yaml

Description
===========

This file is used to configure aspects of TLS connection handling for both inbound and outbound
connections. The configuration is driven by the SNI values provided by the inbound connection. The
file consists of a set of configuration items, each identified by an SNI value (``fqdn``).
When an inbound TLS connection is made, the SNI value from the TLS negotiation is matched against
the items specified by this file and if there is a match, the values specified in that item override
the defaults. This is done during the inbound connection processing and be some outbound properties
can be overridden again later, such as via :file:`remap.config` or plugins.

By default this is named :file:`ssl_server_name.yaml`. The file can be changed by settting
:ts:cv:`proxy.config.ssl.servername.filename`. This file is loaded on start up and by
:option:`traffic_ctl config reload` if the file has been modified since process start.

The configuration file is yaml-based. After parsing the configuration, a list of tables will be the result.
Each table is a set of key / value pairs that create a configuration item. This configuration file accepts
wildcard entries. To apply an SNI based setting on all the servernames with a common upper level domain name,
the user needs to enter the fqdn in the configuration with a ``*.`` followed by the common domain name. (``*.yahoo.com`` for e.g.,).

.. _override-verify-origin-server:
.. _override-verify-server-policy:
.. _override-verify-server-properties:

========================= ==============================================================================
Key                       Meaning
========================= ==============================================================================
fqdn                      Fully Qualified Domain Name. This item is used if the SNI value matches this.

verify_server_policy      One of the values :code:`DISABLED`, :code:`PERMISSIVE`, or :code:`ENFORCED`.

                          By default this is :ts:cv:`proxy.config.ssl.client.verify.server.policy`.
                          This controls how Traffic Server evaluated the origin certificate.

verify_server_properties  One of the values :code:`NONE`, :code:`SIGNATURE`, :code:`NAME`, and :code:`ALL`

                          By default this is :ts:cv:`proxy.config.ssl.client.verify.server.properties`.
                          This controls what Traffic Server checks when evaluating the origin certificate.

verify_origin_server      Deprecated.  Use verify_server_policy and verify_server_properties instead.
                          One of the values :code:`NONE`, :code:`MODERATE`, or :code:`STRICT`.
                          By default this is :ts:cv:`proxy.config.ssl.client.verify.server`.

verify_client             One of the values :code:`NONE`, :code:`MODERATE`, or :code:`STRICT`.

                          By default this is :ts:cv:`proxy.config.ssl.client.certification_level`.

client_cert               The file containing the client certificate to use for the outbound connection.

                          If this is relative it is relative to the path in
                          :ts:cv:`proxy.config.ssl.server.cert.path`. If not set
                          :ts:cv:`proxy.config.ssl.client.cert.filename` is used.

client_key                The file containing the client private key that corresponds to the certificate
                          for the outbound connection.

                          If this is relative it is relative to the path in
                          :ts:cv:`proxy.config.ssl.server.private_key.path`. If not set,
                          |TS| tries to use a private key in client_cert.  Otherwise, 
                          :ts:cv:`proxy.config.ssl.client.private_key.filename` is used.


disable_h2                :code:`true` or :code:`false`.

                          If :code:`false` then HTTP/2 is removed from
                          the valid next protocol list. It is not an error to set this to :code:`false`
                          for proxy ports on which HTTP/2 is not enabled.

tunnel_route              Destination as an FQDN and port, separated by a colon ``:``.
========================= ==============================================================================

Client verification, via ``verify_client``, correponds to setting
:ts:cv:`proxy.config.ssl.client.certification_level` for this connection as noted below.

:code:`NONE` -- ``0``
   Do not request a client certificate, ignore it if one is provided.

:code:`MODERATE` - ``1``
   Request a client certificate and do verification if one is provided. The connection is denied if the verification of the client provided certificate fails.
:code:`STRICT` - ``2``
   Request a client certificate and require one to be provided and verified.
   If the verification fails the failure is logged to :file:`diags.log` and the connection is
   denied.

Upstream (server) verification, via ``verify_server_policy`` and ``verify_server_properties``, is similar to client verification
except there is always an upstream certificate. This is equivalent to setting
:ts:cv:`proxy.config.ssl.client.verify.server.policy` and :ts:cv:`proxy.config.ssl.client.verify.server.properties` for this connection.

``verify_server_policy`` specifies how Traffic Server will enforce the server certificate verification.

:code:`DISABLED` 
   Do not verify the upstream server certificate.

:code:`PERMISSIVE`
   Do verification of the upstream certificate but do not enforce. If the verification fails the
   failure is logged in :file:`diags.log` but the connection is allowed.

:code:`ENFORCED` 
   Do verification of the upstream certificate. If verification fails, the failure is
   logged in :file:`diags.log` and the connection is denied.

In addition ``verify_server_properties`` specifies what Traffic Server will check when performing the verification.

:code:`NONE`
  Do not check anything in the standard Traffic Server verification routine.  Rely entirely on the ``TS_SSL_VERIFY_SERVER_HOOK`` for evaluating the origin's certificate.

:code:`SIGNATURE`
  Check the signature of the origin certificate.

:code:`NAME`
  Verify that the SNI is in the origin certificate.

:code:`ALL`
  Verify both the signature and the SNI in the origin certificate.


If ``tunnel_route`` is specified, none of the certificate verification will be done because the TLS
negotiation will be tunneled to the upstream target, making those values irrelevant for that
configuration item. This option is explained in more detail in :ref:`sni-routing`.

Examples
========

Disable HTTP/2 for ``no-http2.example.com``.

.. code-block:: yaml

   - fqdn: no-http2.example.com
     disable_h2: true

Require client certificate verification for ``example.com`` and any server name ending with ``.yahoo.com``. Therefore, client request for a server name ending with yahoo.com (for e.g., def.yahoo.com, abc.yahoo.com etc.) will cause |TS| require and verify the client certificate. By contrast, |TS| will allow a client certficate to be provided for ``example.com`` and if it is, |TS| will require the certificate to be valid.

.. code-block:: yaml

   - fqdn: example.com
     verify_client: MODERATE
   - fqdn: '*.yahoo.com'
     verify_client: STRICT

Disable outbound server certificate verification for ``trusted.example.com`` and require a valid
client certificate.

.. code-block:: yaml

   - fqdn: trusted.example.com
     verify_server_policy: DISABLED
     verify_client: STRICT

See Also
========

:ref:`sni-routing`
