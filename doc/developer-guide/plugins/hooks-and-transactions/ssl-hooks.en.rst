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

.. include:: ../../../common.defs

.. _developer-plugins-ssl-hooks:

TLS User Agent Hooks
********************

In addition to the HTTP oriented hooks, a plugin can add hooks to trigger code
during the TLS handshake with the user agent.  This TLS handshake occurs well before
the HTTP transaction is available, so a separate state machine is required to track the
TLS hooks.

TLS Hooks
---------

In all cases, the hook callback has the following signature.

.. function:: int SSL_callback(TSCont contp, TSEvent event, void * edata)

The edata parameter is a TSVConn object.

The following actions are valid from these callbacks.

  * Fetch the SSL object associated with the connection - :c:func:`TSVConnSslConnectionGet`
  * Set a connection to blind tunnel - :c:func:`TSVConnTunnel`
  * Reenable the ssl connection - :c:func:`TSSslVConnReenable`
  * Find SSL context by name - :c:func:`TSSslContextFindByName`
  * Find SSL context by address - :c:func:`TSSslContextFindByAddr`
  * Determine whether the TSVConn is really representing a SSL connection - :c:func:`TSVConnIsSsl`

TS_VCONN_START_HOOK
------------------------

This hook is invoked after the client has connected to ATS and before the SSL handshake is started, i.e., before any bytes have been read from the client. The data for the callback is a TSVConn instance which represents the client connection. There is no HTTP transaction as no headers have been read.

In theory this hook could apply and be useful for non-SSL connections as well, but at this point this hook is only called in the SSL sequence.

The TLS handshake processing will not proceed until :c:func:`TSSslVConnReenable()` is called either from within the hook
callback or from another piece of code.

TS_VCONN_CLOSE_HOOK
------------------------

This hook is invoked after the SSL handshake is done and when the IO is closing. The TSVConnArgs should be cleaned up here.

TS_SSL_SERVERNAME_HOOK
----------------------

This hook is called if the client provides SNI information in the SSL handshake. If called it will always be called after TS_VCONN_START_HOOK.

The Traffic Server core first evaluates the settings in the ssl_multicert.config file based on the server name. Then the core SNI callback executes the plugin registered SNI callback code. The plugin callback can access the servername by calling the openssl function SSL_get_servername().

Processing will continue regardless of whether the hook callback executes :c:func:`TSSslVConnReenable()` since the openssl
implementation does not allow for pausing processing during the openssl servername callback.

TS_SSL_CERT_HOOK
----------------

This hook is called as the server certificate is selected for the TLS handshake. The plugin callback can execute
code to create or select the certificate that should be used for the TLS handshake.  This will override the default
Traffic Server certificate selection.

If you are running with openssl 1.0.2 or later, you can control whether the TLS handshake processing will
continue after the certificate hook callback execute by calling :c:func:`TSSslVConnReenable()` or not.  The TLS
handshake processing will not proceed until :c:func:`TSSslVConnReenable()` is called.

It may be useful to delay the TLS handshake processing if other resources must be consulted to select or create
a certificate.

TS_SSL_VERIFY_CLIENT_HOOK
-------------------------

This hook is called when a client connects to Traffic Server and presents a 
client certificate in the case of a mutual TLS handshake.  The callback can
get the SSL object from the TSVConn argument and use that to access the client
certificate and make any additional checks.

Processing will continue regardless of whether the hook callback executes
:c:func:`TSSslVConnReenable()` since the openssl implementation does not allow
for pausing processing during the certificate verify callback.

TS_SSL_VERIFY_SERVER_HOOK
-------------------------

This hooks is called when a Traffic Server connects to an origin and the origin
presents a certificate.  The callback can get the SSL object from the TSVConn
argument and use that to access the origin certificate and make any additional checks.

Processing will continue regardless of whether the hook callback executes
:c:func:`TSSslVConnReenable()` since the openssl implementation does not allow
for pausing processing during the certificate verify callback.

TLS Hook State Diagram
----------------------

.. graphviz::
   :alt: TLS Hook State Diagram

   digraph tls_hook_state_diagram{
     HANDSHAKE_HOOKS_PRE -> TS_VCONN_START_HOOK;
     HANDSHAKE_HOOKS_PRE -> TS_SSL_VERIFY_CLIENT_HOOK;
     HANDSHAKE_HOOKS_PRE -> TS_SSL_CERT_HOOK;
     HANDSHAKE_HOOKS_PRE -> TS_SSL_SERVERNAME_HOOK;
     HANDSHAKE_HOOKS_PRE -> HANDSHAKE_HOOKS_DONE;
     TS_SSL_VERIFY_CLIENT_HOOK -> HANDSHAKE_HOOKS_PRE;
     TS_VCONN_START_HOOK -> HANDSHAKE_HOOKS_PRE_INVOKE;
     HANDSHAKE_HOOKS_PRE_INVOKE -> TSSslVConnReenable;
     TSSslVConnReenable -> HANDSHAKE_HOOKS_PRE;
     TS_SSL_SERVERNAME_HOOK -> HANDSHAKE_HOOKS_SNI;
     HANDSHAKE_HOOKS_SNI -> TS_SSL_SERVERNAME_HOOK;
     HANDSHAKE_HOOKS_SNI -> TS_SSL_CERT_HOOK;
     HANDSHAKE_HOOKS_SNI -> HANDSHAKE_HOOKS_DONE;
     HANDSHAKE_HOOKS_CERT -> TS_SSL_CERT_HOOK;
     TS_SSL_CERT_HOOK -> HANDSHAKE_HOOKS_CERT_INVOKE;
     HANDSHAKE_HOOKS_CERT_INVOKE -> TSSslVConnReenable2;
     TSSslVConnReenable2 -> HANDSHAKE_HOOKS_CERT;
     HANDSHAKE_HOOKS_CERT -> HANDSHAKE_HOOKS_DONE;
     HANDSHAKE_HOOKS_DONE -> TS_VCONN_CLOSE_HOOK;

     HANDSHAKE_HOOKS_PRE [shape=box];
     TS_VCONN_START_HOOK [shape=box];
     TS_SSL_VERIFY_CLIENT_HOOK [shape=box];
     HANDSHAKE_HOOKS_PRE_INVOKE [shape=box];
     HANDSHAKE_HOOKS_SNI [shape=box];
     HANDSHAKE_HOOKS_CERT [shape=box];
     HANDSHAKE_HOOKS_CERT_INVOKE [shape=box];
     HANDSHAKE_HOOKS_DONE [shape=box];
   }


