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

sni.yaml
**********************

.. configfile:: sni.yaml

Description
===========

This file is used to configure aspects of TLS connection handling for both inbound and outbound
connections. With the exception of ``host_sni_policy`` (see the description below), the configuration is driven by the SNI values provided by the inbound connection. The
file consists of a set of configuration items, each identified by an SNI value (``fqdn``).
When an inbound TLS connection is made, the SNI value from the TLS negotiation is matched against
the items specified by this file and if there is a match, the values specified in that item override
the defaults. This is done during the inbound connection processing; some outbound properties
can be overridden again later, such as via :file:`remap.config` or plugins.

By default this is named :file:`sni.yaml`. The filename can be changed by setting
:ts:cv:`proxy.config.ssl.servername.filename`. This file is loaded on start up and by
:option:`traffic_ctl config reload` if the file has been modified since process start.

The configuration file is YAML-based. After parsing the configuration, a list of tables will be the result.
Each table is a set of key / value pairs that create a configuration item. This configuration file accepts
wildcard entries. To apply an SNI based setting on all the server names with a common upper level domain name,
the user needs to enter the fqdn in the configuration with a ``*.`` followed by the common domain name. (``*.yahoo.com`` for example).

For some settings, there is no guarantee that they will be applied to a connection under certain conditions.
An established TLS connection may be reused for another server name if it’s used for HTTP/2. This also means that settings
for server name A may affects requests for server name B as well. See https://daniel.haxx.se/blog/2016/08/18/http2-connection-coalescing/
for a more detailed description of HTTP/2 connection coalescing.

.. _override-verify-server-policy:
.. _override-verify-server-properties:
.. _override-host-sni-policy:
.. _override-h2-properties:

========================= ========= ========================================================================================
Key                       Direction Meaning
========================= ========= ========================================================================================
fqdn                      Both      Fully Qualified Domain Name. This item is used if the SNI value matches this.

ip_allow                  Inbound   Specify a list of client IP address, subnets, or ranges what are allowed to complete
                                    the connection. This list is comma separated. IPv4 and IPv6 addresses can be specified.
                                    Here is an example list: 192.168.1.0/24,192.168.10.1-4. This would allow connections
                                    from clients in the 19.168.1.0 network or in the range from 192.168.10.1 to 192.168.1.4.

verify_server_policy      Outbound  One of the values :code:`DISABLED`, :code:`PERMISSIVE`, or :code:`ENFORCED`.

                                    By default this is :ts:cv:`proxy.config.ssl.client.verify.server.policy`.
                                    This controls how |TS| evaluated the origin certificate.

verify_server_properties  Outbound  One of the values :code:`NONE`, :code:`SIGNATURE`, :code:`NAME`, and :code:`ALL`

                                    By default this is :ts:cv:`proxy.config.ssl.client.verify.server.properties`.
                                    This controls what |TS| checks when evaluating the origin certificate.

verify_client             Outbound  One of the values :code:`NONE`, :code:`MODERATE`, or :code:`STRICT`.
                                    If ``NONE`` is specified, |TS| requests no certificate.  If ``MODERATE`` is specified
                                    |TS| will verify a certificate that is presented by the client, but it will not
                                    fail the TLS handshake if no certificate is presented.  If ``STRICT`` is specified
                                    the client must present a certificate during the TLS handshake.

                                    By default this is :ts:cv:`proxy.config.ssl.client.certification_level`.

verify_client_ca_certs    Both      Specifies an alternate set of certificate authority certs to use to verify the
                                    client cert.  The value must be either a file path, or a nested set of key /
                                    value pairs.  If the value is a file path, it must specify a file containing the
                                    CA certs.  Otherwise, there should be up to two nested pairs.  The possible keys
                                    are ``file`` and ``dir``.  The value for ``file`` must be a file path for a file
                                    containing CA certs.  The value for ``dir`` must be a file path for an OpenSSL
                                    X509 hashed directory containing CA certs.  If a given file path does not being
                                    with ``/`` , it must be relative to the |TS| configuration directory.
                                    ``verify_client_ca_certs`` can only be used with capbilities provided by
                                    OpenSSL 1.0.2 or later.

host_sni_policy           Inbound   One of the values :code:`DISABLED`, :code:`PERMISSIVE`, or :code:`ENFORCED`.

                                    If not specified, the value of :ts:cv:`proxy.config.http.host_sni_policy` is used.
                                    This controls how policy impacting mismatches between host header and SNI values are
                                    dealt with.  For details about hos this configuration behaves, see the corresponding
                                    :ts:cv:`proxy.config.http.host_sni_policy` :file:`records.config` documentation.

                                    Note that this particular configuration will be inspected at the time the HTTP Host
                                    header field is processed. Further, this policy check will be keyed off of the Host header
                                    field value rather than the SNI in this :file:`sni.yaml` file. This is done because
                                    the Host header field is ultimately the resource that will be retrieved from the
                                    origin and the administrator will intend to guard this resource rather than the SNI,
                                    which a malicious user may alter to some other server value whose policies are more
                                    lenient than the host he is trying to access.

valid_tls_versions_in     Inbound   This specifies the list of TLS protocols that will be offered to user agents during
                                    the TLS negotiation.  This replaces the global settings in
                                    :ts:cv:`proxy.config.ssl.TLSv1`, :ts:cv:`proxy.config.ssl.TLSv1_1`,
                                    :ts:cv:`proxy.config.ssl.TLSv1_2`, and :ts:cv:`proxy.config.ssl.TLSv1_3`. The potential
                                    values are TLSv1, TLSv1_1, TLSv1_2, and TLSv1_3.  You must list all protocols that |TS|
                                    should offer to the client when using this key.  This key is only valid for OpenSSL
                                    1.1.0 and later and BoringSSL. Older versions of OpenSSL do not provide a hook early enough to update
                                    the SSL object.  It is a syntax error for |TS| built against earlier versions.

client_cert               Outbound  The file containing the client certificate to use for the outbound connection.

                                    If this is relative, it is relative to the path in
                                    :ts:cv:`proxy.config.ssl.client.cert.path`. If not set
                                    :ts:cv:`proxy.config.ssl.client.cert.filename` is used.

client_key                Outbound  The file containing the client private key that corresponds to the certificate
                                    for the outbound connection.

                                    If this is relative, it is relative to the path in
                                    :ts:cv:`proxy.config.ssl.client.private_key.path`. If not set,
                                    |TS| tries to use a private key in client_cert.  Otherwise,
                                    :ts:cv:`proxy.config.ssl.client.private_key.filename` is used.

client_sni_policy         Outbound  Policy of SNI on outbound connection.

                                    If not specified, the value of :ts:cv:`proxy.config.ssl.client.sni_policy` is used.

http2                     Inbound   Indicates whether the H2 protocol should be added to or removed from the
                                    protocol negotiation list.  The valid values are :code:`on` or :code:`off`.

http2_buffer_water_mark   Inbound   Specifies the high water mark for all HTTP/2 frames on an outoging connection.
                                    By default this is :ts:cv:`proxy.config.http2.default_buffer_water_mark`.
                                    NOTE: Connection coalescing may prevent this taking effect.

disable_h2                Inbound   Deprecated for the more general h2 setting.  Setting disable_h2
                                    to :code:`true` is the same as setting http2 to :code:`on`.

tunnel_route              Inbound   Destination as an FQDN and port, separated by a colon ``:``.
                                    Match group number can be specified by ``$N`` where N should refer to a specified group
                                    in the FQDN, ``tunnel_route: $1.domain``.

                                    This will forward all traffic to the specified destination without first terminating
                                    the incoming TLS connection.

forward_route             Inbound   Destination as an FQDN and port, separated by a colon ``:``.

                                    This is similar to tunnel_route, but it terminates the TLS connection and forwards the
                                    decrypted traffic. |TS| will not interpret the decrypted data, so the contents do not
                                    need to be HTTP.

partial_blind_route       Inbound   Destination as an FQDN and port, separated by a colon ``:``.

                                    This is similar to forward_route in that |TS| terminates the incoming TLS connection.
                                    In addition partial_blind_route creates a new TLS connection to the specified origin.
                                    It does not interpret the decrypted data before passing it to the origin TLS
                                    connection, so the contents do not need to be HTTP.

tunnel_alpn               Inbound   List of ALPN Protocol Ids for Partial Blind Tunnel.

                                    ATS negotiates application protocol with the client on behalf of the origin server.
                                    This only works with ``partial_blind_route``.
========================= ========= ========================================================================================

Pre-warming TLS Tunnel
----------------------

=============================== ========================================================================================
Key                             Meaning
=============================== ========================================================================================
tunnel_prewarm                  Override :ts:cv:`proxy.config.tunnel.prewarm` in records.config.

tunnel_prewarm_srv              Enable SRV record lookup on pre-warming. Default is ``false``.

tunnel_prewarm_rate             Rate of how many connections to pre-warm. Default is ``1.0``.

tunnel_prewarm_min              Minimum number of pre-warming queue size (per thread). Default is ``0``.

tunnel_prewarm_max              Maximum number of pre-warming queue size (per thread). Default is ``-1`` (unlimited).

tunnel_prewarm_connect_timeout  Timeout for TCP/TLS handshake (in seconds).

tunnel_prewarm_inactive_timeout Inactive timeout for connections in the pool (in seconds).
=============================== ========================================================================================

Client verification, via ``verify_client``, corresponds to setting
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

``verify_server_policy`` specifies how |TS| will enforce the server certificate verification.

:code:`DISABLED`
   Do not verify the upstream server certificate.

:code:`PERMISSIVE`
   Do verification of the upstream certificate but do not enforce. If the verification fails the
   failure is logged in :file:`diags.log` but the connection is allowed.

:code:`ENFORCED`
   Do verification of the upstream certificate. If verification fails, the failure is
   logged in :file:`diags.log` and the connection is denied.

In addition ``verify_server_properties`` specifies what |TS| will check when performing the verification.

:code:`NONE`
  Do not check anything in the standard |TS| verification routine.  Rely entirely on the ``TS_SSL_VERIFY_SERVER_HOOK`` for evaluating the origin's certificate.

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

   sni:
   - fqdn: no-http2.example.com
     http2: off

Require client certificate verification for ``foo.com`` and any server name ending with ``.yahoo.com``. Therefore, client
request for a server name ending with yahoo.com (e.g., def.yahoo.com, abc.yahoo.com etc.) will cause |TS| require and verify
the client certificate.

For ``foo.com``, if the user agent sets the host header to foo.com but the SNI to some other value, |TS| will warn about the
mismatch but continue to process the request.  Mismatches for the other domains will cause |TS| to warn and return 403.

|TS| will allow a client certificate to be provided for ``example.com`` and if it is, |TS| will require the
certificate to be valid.

.. code-block:: yaml

   sni:
   - fqdn: example.com
     verify_client: MODERATE
   - fqdn: 'foo.com'
     verify_client: STRICT
     host_sni_policy: PERMISSIVE
   - fqdn: '*.yahoo.com'
     verify_client: STRICT

Disable outbound server certificate verification for ``trusted.example.com`` and require a valid
client certificate.

.. code-block:: yaml

   sni:
   - fqdn: trusted.example.com
     verify_server_policy: DISABLED
     verify_client: STRICT

Use FQDN captured group to match in ``tunnel_route``.

.. code-block:: yaml

   sni:
   - fqdn: '*.foo.com'
     tunnel_route: '$1.myfoo'
   - fqdn: '*.bar.*.com'
     tunnel_route: '$2.some.$1.yahoo'

FQDN ``some.foo.com`` will match and the captured string will be replaced in the ``tunnel_route`` which will end up being
``some.myfoo``.
Second part is using multiple groups, having ``bob.bar.example.com`` as FQDN, ``tunnel_route`` will end up being
``bar.some.bob.yahoo``.

See Also
========

:ref:`sni-routing`
