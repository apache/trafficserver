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

TSHttpHookID
************

Synopsis
========

.. code-block:: cpp

    #include <ts/apidefs.h>

Enum typedef defining the possible :ref:`developer-plugins-hooks` for setting
up :ref:`developer-plugins-continuations` callbacks.

Enumeration Members
===================

.. enum:: TSHttpHookID

   .. enumerator:: TS_HTTP_READ_REQUEST_HDR_HOOK

   .. enumerator:: TS_HTTP_REQUEST_BUFFER_READ_COMPLETE_HOOK

   .. enumerator:: TS_HTTP_OS_DNS_HOOK

   .. enumerator:: TS_HTTP_SEND_REQUEST_HDR_HOOK

   .. enumerator:: TS_HTTP_READ_CACHE_HDR_HOOK

   .. enumerator:: TS_HTTP_READ_RESPONSE_HDR_HOOK

   .. enumerator:: TS_HTTP_SEND_RESPONSE_HDR_HOOK

   .. enumerator:: TS_HTTP_REQUEST_TRANSFORM_HOOK

   .. enumerator:: TS_HTTP_RESPONSE_TRANSFORM_HOOK

   .. enumerator:: TS_HTTP_SELECT_ALT_HOOK

   .. enumerator:: TS_HTTP_TXN_START_HOOK

   .. enumerator:: TS_HTTP_TXN_CLOSE_HOOK

   .. enumerator:: TS_HTTP_SSN_START_HOOK

   .. enumerator:: TS_HTTP_SSN_CLOSE_HOOK

   .. enumerator:: TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK

   .. enumerator:: TS_HTTP_PRE_REMAP_HOOK

   .. enumerator:: TS_HTTP_POST_REMAP_HOOK

   .. enumerator:: TS_HTTP_RESPONSE_CLIENT_HOOK

   .. enumerator:: TS_HTTP_REQUEST_CLIENT_HOOK

   .. enumerator:: TS_SSL_FIRST_HOOK

   .. enumerator:: TS_VCONN_START_HOOK

   .. enumerator:: TS_VCONN_OUTBOUND_START_HOOK

   .. enumerator:: TS_VCONN_CLOSE_HOOK

   .. enumerator:: TS_VCONN_OUTBOUND_CLOSE_HOOK

   .. enumerator:: TS_SSL_CLIENT_HELLO_HOOK

   .. enumerator:: TS_SSL_SNI_HOOK

   .. enumerator:: TS_SSL_CERT_HOOK

   .. enumerator:: TS_SSL_SERVERNAME_HOOK

   .. enumerator:: TS_SSL_VERIFY_CLIENT_HOOK

   .. enumerator:: TS_SSL_VERIFY_SERVER_HOOK

   .. enumerator:: TS_SSL_LAST_HOOK

   .. enumerator:: TS_HTTP_LAST_HOOK

Description
===========

Note that :enumerator:`TS_SSL_CERT_HOOK` and :enumerator:`TS_SSL_SNI_HOOK` correspond to the same OpenSSL
callbacks. This is done for backwards compatibility. :enumerator:`TS_SSL_SNI_HOOK` is expected
to be deprecated and removed, plugins using this should change to :enumerator:`TS_SSL_CERT_HOOK` or
:enumerator:`TS_SSL_SERVERNAME_HOOK` as appropriate.

.. warning:: OpenSSL 1.0.2 and later versions

   :enumerator:`TS_SSL_SERVERNAME_HOOK` is invoked for the OpenSSL servername callback.
   :enumerator:`TS_SSL_SNI_HOOK` and :enumerator:`TS_SSL_CERT_HOOK` are invoked for the OpenSSL certificate
   callback which is not guaranteed to be invoked for a TLS transaction.

   This is a behavior change dependent on the version of OpenSSL. To avoid problems use
   :enumerator:`TS_SSL_SERVERNAME_HOOK` to get called back for all TLS transaction and
   :enumerator:`TS_SSL_CERT_HOOK` to get called back only to select a certificate.
