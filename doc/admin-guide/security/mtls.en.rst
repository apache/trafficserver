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

Mututal TLS
***********

In addition to the standard TLS where the server side provides a certificate
for authentication, |TS| supports mututal TLS (mTLS) where the client also
provides a certificate to the origin for verification.

mTLS from User Agent to |TS|
============================

In this scenario, |TS| is acting as the TLS server.  It can request during the TLS
handshake that the User Agent provides a certificate.  If |TS| can verify that the user
agent provided certificate is signed by a trusted CA, the TLS handshake will proceed.
Otherwise it will fail.

Case 1:  Require certificates from all User Agents
--------------------------------------------------
In this case, you must set the :ts:cv:`proxy.config.ssl.client.certification_level` setting
in :file:`records.config` to 2 to require a client certificate from all user agents.
Setting this to 0 means that no client certificate is requested.  Setting this to 0 means
that a client certificate is requested but the handshake proceeds even if one is not provided.
There may be problems with some clients and the 1 setting, so staying with values 0 or 2 may be best.

If the certificate_level is set to 2, you must also set :ts:cv:`proxy.config.ssl.CA.cert.path` and :ts:cv:`proxy.config.ssl.CA.cert.filename`
in :file:`records.config` to point to a file that contains the certificates of the CA's that
would have signed the user agent provided certificates that |TS| receives.

Case 2: Apply different certificate requirements depending on the domain requested by the User Agent
----------------------------------------------------------------------------------------------------
Often there are scenarios where |TS| must require client certificates from some user agents
(e.g. trusted parter sites) but not others (e.g. health check requests).  In that case, use the :file:`sni.yaml` file.

|TS| uses the Server Name Indication (SNI) from the TLS Client Hello to distinguish between the different
cases.  This is the FQDN value in the :file:`sni.yaml` file.  To control client certificate requirements use the
"verify_client" keyword which can take on the following values: NONE, MODERATE, or STRICT.

In the case were |TS| should require certificates from all domains except the health check domain, hc.example.com,
you should set :ts:cv:`proxy.config.ssl.client.certification_level` to 2 in :file:`records.config` and have the
following in :file:`sni.yaml`. ::

        sni:
        - fqdn: hc.example.com
          verify_client: NONE

Similarly, if you only wanted to require client certificates for super.sensitive.example.com,
you would set :ts:cv:`proxy.config.ssl.client.certification_level` to 0 in
:file:`records.config` and have the following in :file:`sni.yaml` ::

        sni:
        - fqdn: super.sensitive.example.com
          verify_client: STRICT

You can also use wildcards in the fqdn names (e.g. '*foo.com' or 'mail.*.foo.com').

Awkward healthcheck case
------------------------
Above we showed how you can exempt a health check request from needing to provide a client certificate.
That technique requires the health check requester to provide a SNI value.  Unfortunately many older
clients (including many current hardware loadbalancers), do not set the SNI value in the client
hello.  From |TS|'s perspective the SNI value is the empty string. In that case, the following
:file:`sni.yaml` should work.  It will match on all requests do not provide a SNI and turn off the
client certificate requirement.  This is a very broad rule, since it is very easy for a malicious
user to make a request without the SNI set to try to evade the requirements of your :file:`sni.yaml` policy. ::

        sni:
        - fqdn: ''
          verify_client: NONE

Specialize CA Bundle for client cert
------------------------------------
You can use the verify_client_ca_certs keyword to specialize the CA bundle name in :file:`sni.yaml`.
For example you expect all client certs to be signed by the roots in client_CA_bundle.pem except for
special.example.com where the client certs should be signed by roots in partners_bundle.pem.
Then you would set :ts:cv:`proxy.config.ssl.CA.cert.filename` to client_CA_bundle.pem in
:file:`records.config` and you would set the following in :file:`sni.yaml` ::

        sni:
        - fqdn: special.example.com
          verify_client_ca_certs: partners_bundle.pem
          verify_client: STRICT

Guidance for testing
--------------------
If you use curl to test your SNI-based Traffic Server configuration, you must make sure the
SNI value is really set in the TLS Client Hello message.  If you use the |TS| name or address
in the URL and explicitly set the host field (as shown below) to indicate the real domain, the
SNI value will not be set to the host field value.  In the example below the SNI value will
not be foo.com ::

        curl -H 'host:foo.com' -k -v https://prod123.example.com/foo

You can use the -resolve option of curl to ensure the sni value is set as shown below or update
your local /etc/hosts so the address for your designed domain is the proxy address. ::

        curl -resolve foo.com:443:1.2.3.4 -k -v https://foo.com/foo

mTLS from |TS| to Origin
=========================
In this scenario |TS| is the TLS client talking to the upstream origin.

Case 1: Provide one certificate to all potential origins that require a certificate
-----------------------------------------------------------------------------------
In this case, you would set at least :ts:cv:`proxy.config.ssl.client.cert.filename` to the name and path
of a file that includes the client certificate and the client private key.  You could also set
:ts:cv:`proxy.config.ssl.client.cert.path` to indicate the path of the file.

The private key could be stored in a separate file named by :ts:cv:`proxy.config.ssl.client.private_key.filename`
and :ts:cv:`proxy.config.ssl.client.private_key.path`.

Case 2: Provide different certificates to origins depending on the specific origin name
---------------------------------------------------------------------------------------
In this case you would again use the :file:`sni.yaml` file. The fqdn would correspond to the SNI
that |TS| passes to the origin.  Specifically you would set the client_cert and possibly the client_key
values to point to the files containing the client certificate and client keys.

When setting up this case, it is important to understand what value |TS| will be using for the SNI name
to origin.  By default the value of the Host header in the request to origin will be used for the ssl_server_name
fqdn lookup.  If :ts:cv:`proxy.config.url_remap.pristine_host_hdr` is set, this will be the same host header
value as in the user agent request.  You can use :ts:cv:`proxy.config.ssl.client.sni_policy` to change
|TS| to use the remap hostname instead as the fqdn lookup value,

Case 3: Provide different certificates to origins depending on origin name and request URL
------------------------------------------------------------------------------------------
In this case you use the conf_remap.so plugin on a remap rule to override the cient_cert definition only for
URLs that match that remap rule. You could create the following lines in your :file:`remap.config` to override
the value of :ts:cv:`proxy.config.ssl.client.cert.filename` in :file:`records.config` for specific types of
traffic.  In the example below any client traffic with a path that starts with /case1 will use the
customer-case1.pem certificate.  Any client traffic directed to the hostname bank.example.com and a path that
starts with /pci will use the pci.pem certificate. ::

   map /case1 https://server.com/case1 @plugin=conf_remap.so @pparam=proxy.config.ssl.client.cert.filename=customer-case1.pem
   map /case2 https://server.com/case2 @plugin=conf_remap.so @pparam=proxy.config.ssl.client.cert.filename=customer-case2.pem
   map https://bank.example.com/pci https://pci.server.com/ @plugin=conf_remap.so @param=proxy.config.ss.client.cert.filename=pci.pem

Guidance for testing
--------------------
You will want to verify that |TS| will accurately reload to pick up new client certificate files.
As time goes one, the life time of certificates shrink from months to weeks or days, so you will most likely need
to have |TS| reload configurations to load up new certificates without restarting the |TS| process (and interrupting
customer traffic).  The following command should cause updated client certificates and keys to be loaded into the traffic_server process.
From there you can verify via your origins that the updated certificates are being offered. ::

    traffic_ctl config reload

If the contents of the certificate files change but the names of the files do not, you may need to touch ssl_multicert.config
(for server certs) and sni.yaml  (for client certs).

