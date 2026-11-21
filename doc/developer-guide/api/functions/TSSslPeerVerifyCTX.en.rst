.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: cpp

TSSslPeerVerifyCTX
******************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSVConn TSSslPeerVerifyCTXVConnGet(TSSslPeerVerifyCTX ctx)
.. function:: TSSslPeerCredType TSSslPeerVerifyCTXCredTypeGet(TSSslPeerVerifyCTX ctx)
.. function:: TSSslVerifyCTX TSSslPeerVerifyCTXStoreCTXGet(TSSslPeerVerifyCTX ctx)
.. function:: EVP_PKEY *TSSslPeerVerifyCTXPeerRPKGet(TSSslPeerVerifyCTX ctx)
.. function:: int TSSslPeerVerifyCTXPreverifyGet(TSSslPeerVerifyCTX ctx)

Description
===========

These functions provide access to peer credentials and verification state during TLS peer verification.
An instance of :type:`TSSslPeerVerifyCTX` is passed as the ``edata`` argument to plugin callbacks
registered on :enumerator:`TS_SSL_PEER_VERIFY_CLIENT_HOOK` (event :enumerator:`TS_EVENT_SSL_PEER_VERIFY_CLIENT`)
and :enumerator:`TS_SSL_PEER_VERIFY_SERVER_HOOK` (event :enumerator:`TS_EVENT_SSL_PEER_VERIFY_SERVER`).

These hooks are invoked for both traditional X.509 certificate chains and RFC 7250 Raw Public Keys (RPK).

Accessors
=========

.. function:: TSVConn TSSslPeerVerifyCTXVConnGet(TSSslPeerVerifyCTX ctx)

   Returns the :type:`TSVConn` associated with the TLS connection being verified.

.. function:: TSSslPeerCredType TSSslPeerVerifyCTXCredTypeGet(TSSslPeerVerifyCTX ctx)

   Returns the type of credential presented by the peer:

   * :enumerator:`TS_SSL_PEER_CRED_X509`: The peer presented an X.509 certificate chain.
   * :enumerator:`TS_SSL_PEER_CRED_RPK`: The peer presented an RFC 7250 Raw Public Key.
   * :enumerator:`TS_SSL_PEER_CRED_NONE`: No peer credentials were provided.

.. function:: TSSslVerifyCTX TSSslPeerVerifyCTXStoreCTXGet(TSSslPeerVerifyCTX ctx)

   If the peer presented an X.509 certificate (:enumerator:`TS_SSL_PEER_CRED_X509`), returns the
   :type:`TSSslVerifyCTX` (which can be cast to ``X509_STORE_CTX *``). If the peer authenticated
   with a Raw Public Key or presented no credentials, returns ``nullptr``.

.. function:: EVP_PKEY *TSSslPeerVerifyCTXPeerRPKGet(TSSslPeerVerifyCTX ctx)

   If the peer presented an RFC 7250 Raw Public Key (:enumerator:`TS_SSL_PEER_CRED_RPK`), returns
   the peer's public key as an ``EVP_PKEY *``. If the peer presented an X.509 certificate or no
   credentials, returns ``nullptr``.

   .. note::

      The returned pointer is borrowed from the underlying TLS connection. The plugin must not
      free this pointer. If the plugin needs to retain the key beyond the execution of the callback,
      it must increment its reference count via ``EVP_PKEY_up_ref()``.

.. function:: int TSSslPeerVerifyCTXPreverifyGet(TSSslPeerVerifyCTX ctx)

   Returns the preverification result from the TLS library and Traffic Server before the plugin
   hook was invoked (``1`` for success / pin match, ``0`` for failure).

Hook Processing and Verdict
===========================

Because TLS libraries execute certificate and peer verification synchronously within the handshake
callback, plugins cannot pause or delay processing asynchronously during this hook.

* Calling :func:`TSVConnReenable` allows the TLS handshake to proceed, subject to the preverification
  outcome and the configured verification policy.
* Calling :func:`TSVConnReenableEx` with :enumerator:`TS_EVENT_ERROR` immediately rejects the peer and
  fails the TLS handshake.

Types
=====

.. type:: TSSslPeerVerifyCTX

   An opaque pointer to the peer verification context, valid only for the duration of the hook callback.

.. type:: TSSslPeerCredType

   An enumeration indicating the type of credential presented by the peer:

   .. enumerator:: TS_SSL_PEER_CRED_NONE

      No peer credentials.

   .. enumerator:: TS_SSL_PEER_CRED_X509

      X.509 certificate chain.

   .. enumerator:: TS_SSL_PEER_CRED_RPK

      RFC 7250 Raw Public Key.

See Also
========

:func:`TSVConnSslVerifyCTXGet`,
:doc:`TSHttpHookID <../types/TSHttpHookID.en>`
