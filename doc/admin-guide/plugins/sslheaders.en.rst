.. _admin-plugins-ssl-headers:

SSL Headers Plugin
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

The ``sslheaders`` plugins injects SSL session information into
HTTP request headers. It can operate as a global plugin or as a
remap plugin.

Plugin Options
--------------

The following options may be specified when loading the plugin in
:file:`plugin.config` or :file:`remap.config`:

--attach=WHICH  This option specifies which HTTP request the SSL headers are
                attached to.

                ``client`` causes the headers to be injected into
                the client request. This is primarily useful if another plugin
                should inspect then. ``server`` is the default and injects the
                headers into the origin server request. ``both`` injects the
                headers into both the client request and the origin server
                request.

A list of `KEY=VALUE` pairs follows any options. The `KEY` names the HTTP
header to inject, and `VALUE` names the SSL session field.

======================  ===============================================
SSL session field       Description
======================  ===============================================
client.certificate      The client certificate in PEM format
client.subject          The client certificate subject DN
client.issuer           The client certificate issuer DN
client.serial           The client certificate serial number in hexadecimal format
client.signature        The client certificate signature in hexadecimal format
client.notbefore        The client certificate validity start time
client.notafter         The client certificate validity end time
server.certificate      The server certificate in PEM format
server.subject          The server certificate subject DN
server.issuer           The server certificate issuer DN
server.serial           The server certificate serial number in hexadecimal format
server.signature        The server certificate signature in hexadecimal format
server.notbefore        The server certificate validity start time
server.notafter         The server certificate validity end time
======================  ===============================================

The `client.certificate` and `server.certificate` fields emit
the corresponding certificate in PEM format, with newline characters
replaced by spaces.

If the ``sslheaders`` plugin activates on non-SSL connections, it
will delete all the configured HTTP header names so that malicious
clients cannot inject misleading information. If any of the SSL
fields expand to an empty string, those headers are also deleted.

Examples:
---------

In this example, the origin server is interested in the subject of
the server certificate that was used to accept a client connetion.
We can apply the ``sslheaders`` plugin to a generic remap rule to
provide this information. The :file:`remap.config` configuration
would be::

  regex_map https://*.example.com/ http://origin.example.com/ \
    @plugin=sslheaders.so @pparam=SSL-Server=server.subject

In this example, we have set :ts:cv:`proxy.config.ssl.client.certification_level`
to request SSL client certificates. We can then configure ``sslheaders``
to populate the client certificate subject globally by adding it
to :file:`plugin.config`::

  sslheaders.so SSL-Client-ID=client.subject SSL-Client-NotBefore=client.notbefore SSL-Client-NotAfter-client.notafter
