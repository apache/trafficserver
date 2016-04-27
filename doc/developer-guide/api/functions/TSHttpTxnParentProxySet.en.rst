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

TSHttpTxnParentProxySet
***********************

Sets the parent proxy :arg:`hostname` and :arg:`port`.

Synopsis
========

`#include <ts/ts.h>`

.. function:: void TSHttpTxnParentProxySet(TSHttpTxn txnp, const char * hostname, int port)

Description
===========

The string :arg:`hostname` is copied into the :c:type:`TSHttpTxn`. You can
modify or delete the string after calling :c:func:`TSHttpTxnParentProxySet`.
