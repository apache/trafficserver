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

TSVConnSslVerifyCTXGet
***********************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSSslVerifyCTX TSVConnSslVerifyCTXGet(TSVConn svc)

Description
===========

Get the TSSslVerifyCTX object that corresponds to the certificates being verified for the SSL connection 
corresponding to :arg:`svc`.

This value is only meaningful during the peer certificate verification callbacks, specifically during callbacks
invoked from the TS_SSL_VERIFY_SERVER_HOOK and TS_SSL_VERIFY_CLIENT_HOOK.

Types
=====

.. type:: TSSslVerifyCTX

        The SSL object that corresponds to the peer certificates being verified.  This is an
        opaque type that can be cast to the appropriate implementation type (:code `X509_STORE_CTX *` for the OpenSSL library).
