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

TSHttpTxnErrorBodySet
*********************

Sets an error type body to a transaction.

Synopsis
========

`#include <ts/ts.h>`

.. function:: void TSHttpTxnErrorBodySet(TSHttpTxn txnp, char * buf, size_t buflength, char * mimetype)

Description
===========

Note that both string arguments must be allocated with :c:func:`TSmalloc` or
:c:func:`TSstrdup`.  The :arg:`mimetype` is optional, and if not provided it
defaults to :literal:`text/html`. Sending an empty string would prevent setting
a content type header (but that is not adviced).
