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

TSSslContextFindByName
**********************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSSslContext TSSslContextFindByName(const char * name)

Description
===========

Look for a SSL context created from :file:`ssl_multicert.config`. Use the
server :arg:`name` to search.

TSSslContextFindByAddr
**********************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSSslContext TSSslContextFindByAddr(struct sockaddr const*)

Description
===========

Look for a SSL context created from :file:`ssl_multicert.config`.  Use the
server address to search.

Type
====

.. type:: TSSslContext

Corresponds to the SSL_CTX * value in openssl.
