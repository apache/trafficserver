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

.. default-domain:: c

TSHttpHookID
************

Synopsis
========

.. code-block:: cpp

    #include <ts/apidefs.h>

.. c:type:: TSHttpHookID

Enum typedef defining the possible :ref:`developer-plugins-hooks` for setting
up :ref:`developer-plugins-continuations` callbacks.

Enumeration Members
===================

.. c:macro:: TSHttpHookID TS_HTTP_READ_REQUEST_HDR_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_OS_DNS_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_SEND_REQUEST_HDR_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_READ_CACHE_HDR_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_READ_RESPONSE_HDR_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_SEND_RESPONSE_HDR_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_REQUEST_TRANSFORM_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_RESPONSE_TRANSFORM_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_SELECT_ALT_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_TXN_START_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_TXN_CLOSE_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_SSN_START_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_SSN_CLOSE_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_PRE_REMAP_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_POST_REMAP_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_RESPONSE_CLIENT_HOOK

.. c:macro:: TSHttpHookID TS_SSL_FIRST_HOOK

.. c:macro:: TSHttpHookID TS_VCONN_START_HOOK

.. c:macro:: TSHttpHookID TS_VCONN_OUTBOUND_START_HOOK

.. c:macro:: TSHttpHookID TS_VCONN_CLOSE_HOOK

.. c:macro:: TSHttpHookID TS_VCONN_OUTBOUND_CLOSE_HOOK

.. c:macro:: TSHttpHookID TS_SSL_CLIENT_HELLO_HOOK

.. c:macro:: TSHttpHookID TS_SSL_SNI_HOOK

.. c:macro:: TSHttpHookID TS_SSL_CERT_HOOK

.. c:macro:: TSHttpHookID TS_SSL_SERVERNAME_HOOK

.. c:macro:: TSHttpHookID TS_SSL_VERIFY_CLIENT_HOOK

.. c:macro:: TSHttpHookID TS_SSL_VERIFY_SERVER_HOOK

.. c:macro:: TSHttpHookID TS_SSL_LAST_HOOK

.. c:macro:: TSHttpHookID TS_HTTP_LAST_HOOK

Description
===========

Note that :macro:`TS_SSL_CERT_HOOK` and :macro:`TS_SSL_SNI_HOOK` correspond to the same openssl
callbacks. This is done for backwards compatibility. :macro:`TS_SSL_SNI_HOOK` is expected
to be deprecated and removed, plugins using this should change to :macro:`TS_SSL_CERT_HOOK` or
:macro:`TS_SSL_SERVERNAME_HOOK` as appropriate.

.. warning:: openssl 1.0.2 and later versions

   :macro:`TS_SSL_SERVERNAME_HOOK` is invoked for the openssl servername callback.
   :macro:`TS_SSL_SNI_HOOK` and :macro:`TS_SSL_CERT_HOOK` are invoked for the openssl certificate
   callback which is not guaranteed to be invoked for a TLS transaction.

   This is a behavior change dependent on the version of openssl. To avoid problems use
   :macro:`TS_SSL_SERVERNAME_HOOK` to get called back for all TLS transaction and
   :macro:`TS_SSL_CERT_HOOK` to get called back only to select a certificate.
