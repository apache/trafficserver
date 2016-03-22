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

====================
ssl_multicert.config
====================

.. configfile:: ssl_multicert.config

The :file:`ssl_multicert.config` file lets you configure Traffic
Server to use multiple SSL server certificates to terminate the SSL
sessions. If you have a Traffic Server system with more than one
IP address assigned to it, then you can assign a different SSL
certificate to be served when a client requests a particular IP
address or host name.

At configuration time, certificates are parsed to extract the
certificate subject and all the DNS `subject alternative names
<http://en.wikipedia.org/wiki/SubjectAltName>`_.  A certificate
will be presented for connections requesting any of the hostnames
found in the certificate. Wildcard names are supported, but only
of the form `*.domain.com`, ie. where `*` is the leftmost domain
component.

Changes to :file:`ssl_multicert.config` can be applied to a running
Traffic Server using :option:`traffic_ctl config reload`.

Format
======

Each :file:`ssl_multicert.config` line consists of a sequence of
`key=value` fields that specify how Traffic Server should use a
particular SSL certificate.

ssl_cert_name=FILENAME[,FILENAME ...]
  The name of the file containing the TLS certificate. *FILENAME*
  is located relative to the directory specified by the
  :ts:cv:`proxy.config.ssl.server.cert.path` configuration variable.
  It may also include the intermediate CA certificates, sorted from
  leaf to root.  At a minimum, the file must include a leaf
  certificate.

  When running with OpenSSL 1.0.2 or later, this directive can be
  used to configure the intermediate CA chain on a per-certificate
  basis.  Multiple chain files are separated by comma character.
  For example, it is possible able to configure a ECDSA certificate
  chain and a RSA certificate chain and serve them simultaneously,
  allowing OpenSSL to determine which certificate would be used
  when the TLS session cipher suites are negotiated.  Note that the
  leaf certs in `FILENAME1` and `FILENAME2` must have the same
  subjects and alternate names. The first certificate is used to
  to match the client's SNI request.

  You can also configure multiple leaf certificates in a same chain
  with OpenSSL 1.0.1.

  This is the only field that is required to be present.

dest_ip=ADDRESS (optional)
  The IP (v4 or v6) address that the certificate should be presented
  on. This is now only used as a fallback in the case that the TLS
  SubjectNameIndication extension is not supported. If *ADDRESS* is
  `*`, the corresponding certificate will be used as the global
  default fallback if no other match can be made. The address may
  contain a port specifier, in which case the corresponding certificate
  will only match for connections accepted on the specified port.
  IPv6 addresses must be enclosed by square brackets if they have
  a port, eg, [::1]:80. Care should be taken to make each ADDRESS unique.

ssl_key_name=FILENAME (optional)
  The name of the file containing the private key for this certificate.
  If the key is contained in the certificate file, this field can
  be omitted, otherwise *FILENAME* is resolved relative to the
  :ts:cv:`proxy.config.ssl.server.private_key.path` configuration variable.

ssl_ca_name=FILENAME (optional)
  If the certificate is issued by an authority that is not in the
  system CA bundle, additional certificates may be needed to validate
  the certificate chain. *FILENAME* is resolved relative to the
  :ts:cv:`proxy.config.ssl.CA.cert.path` configuration variable.

ssl_ticket_enabled=1|0 (optional)
  Enable RFC 5077 stateless TLS session tickets. To support this,
  OpenSSL should be upgraded to version 0.9.8f or higher. This
  option must be set to `0` to disable session ticket support.

ticket_key_name=FILENAME (optional)
  The name of session ticket key file which contains a secret for
  encrypting and decrypting TLS session tickets. If *FILENAME* is
  not an absolute path, it is resolved relative to the
  :ts:cv:`proxy.config.ssl.server.cert.path` configuration variable.
  This option has no effect if session tickets are disabled by the
  ``ssl_ticket_enabled`` option.  The contents of the key file should
  be 48 random (ASCII) bytes. One way to generate this would be to run
  ``head -c48 /dev/urandom | openssl enc -base64 | head -c48 > file.ticket``.

  Session ticket support is enabled by default. If neither of the
  ``ssl_ticket_enabled`` and ``ticket_key_name`` options are
  specified, and internal session ticket key is generated. This
  key will be different each time Traffic Server is started.

ssl_key_dialog=builtin|"exec:/path/to/program [args]" (optional)
  Method used to provide a pass phrase for encrypted private keys.  If the
  pass phrase is incorrect, SSL negotiation for this dest_ip will fail for
  clients who attempt to connect.
  Two options are supported: builtin and exec:

    ``builtin`` - Requests pass phrase via stdin/stdout. User will be
      provided the ssl_cert_name and be prompted for the pass phrase.
      Useful for debugging.

    ``exec:`` - Executes program /path/to/program and passes args, if
      specified, to the program and reads the output from stdout for
      the pass phrase.  If args are provided then the entire exec: string
      must be quoted with "" (see examples).  Arguments with white space
      are supported by single quoting (').  The intent is that this
      program runs a security check to ensure that the system is not
      compromised by an attacker before providing the pass phrase.

Certificate Selection
=====================

Traffic Server attempts two certificate selections during SSL
connection setup. An initial selection is made when a TCP connection
is accepted. This selection examines the IP address and port that
the client is connecting to and chooses the best certificate from
the those that have a ``dest_ip`` specification. If no matching
certificates are found, a default certificate is chosen.  The final
certificate selection is made during the SSL handshake.  At this
point, the client may use `Server Name Indication
<http://en.wikipedia.org/wiki/Server_Name_Indication>`_ to request
a specific hostname. Traffic Server will use this request to select
a certificate with a matching subject or subject alternative name.
Failing that, a wildcard certificate match is attempted. If no match
can be made, the initial certificate selection remains in force.

In all cases, Traffic Server attempts to select the most specific
match. An address specification that contains a port number will
take precedence over a specification that does not contain a port
number. A specific certificate subject will take precedence over a
wildcard certificate. In the case of multiple matching certificates
the first match will be returned to non-SNI capable clients.

Examples
========

The following example configures Traffic Server to use the SSL
certificate ``server.pem`` for all requests to the IP address
111.11.11.1 and the SSL certificate ``server1.pem`` for all requests
to the IP address 11.1.1.1. Connections from all other IP addresses
are terminated with the ``default.pem`` certificate.
Since the private key is included in the certificate files, no
private key name is specified.

::

    dest_ip=111.11.11.1 ssl_cert_name=server.pem
    dest_ip=11.1.1.1 ssl_cert_name=server1.pem
    dest_ip=* ssl_cert_name=default.pem

The following example configures Traffic Server to use the ECDSA
certificate chain ``ecdsa.pem`` or RSA certificate chain ``rsa.pem``
for all requests.

::

    dest_ip=* ssl_cert_name=ecdsa.pem,rsa.pem

The following example configures Traffic Server to use the ECDSA
certificate chain ``ecdsa.pem`` or RSA certificate chain ``rsa.pem``
for all requests, the public key and private key are in separate PEM files.
Note that the number of files in ssl_key_name must match the files in ssl_cert_name,
and they should be presented in the same order.

::

    dest_ip=* ssl_cert_name=ecdsa_pub.pem,rsa_pub.pem ssl_key_name=ecdsa_private.pem,rsa_private.pem

The following example configures Traffic Server to use the SSL
certificate ``server.pem`` and the private key ``serverKey.pem``
for all requests to port 8443 on IP address 111.11.11.1. The
``general.pem`` certificate is used for server name matches.

::

     dest_ip=111.11.11.1:8443 ssl_cert_name=server.pem ssl_key_name=serverKey.pem ssl_cert_name=general.pem

The following example configures Traffic Server to use the SSL
certificate ``server.pem`` for all requests to the IP address
111.11.11.1. Session tickets are enabled with a persistent ticket
key.

::

    dest_ip=111.11.11.1 ssl_cert_name=server.pem ssl_ticket_enabled=1 ticket_key_name=ticket.key

The following example configures Traffic Server to use the SSL
certificate ``server.pem`` and disable session tickets for all
requests to the IP address 111.11.11.1.

::

    dest_ip=111.11.11.1 ssl_cert_name=server.pem ssl_ticket_enabled=0

The following examples configure Traffic Server to use the SSL
certificate ``server.pem`` which includes an encrypted private key.
The external program /usr/bin/mypass will be called on startup with one
parameter (foo) in the first example, and with two parameters (foo)
and (ba r) in the second example, the program (mypass) will return the
pass phrase to decrypt the keys.

::

    ssl_cert_name=server1.pem ssl_key_dialog="exec:/usr/bin/mypass foo"
    ssl_cert_name=server2.pem ssl_key_dialog="exec:/usr/bin/mypass foo 'ba r'"
