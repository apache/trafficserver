.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: ../../../common.defs

.. default-domain:: c

TSHttpTxnIsCacheable
********************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSHttpTxnIsCacheable(TSHttpTxn txnp, TSMBuffer request, TSMBuffer response)

Description
===========

Determine if an upstream response is cacheable according to the current |TS| configuration and
state. All of the arguments must have be obtained via other API calls prior to calling this
function.

The :arg:`request` and :arg:`response` arguments must refer to HTTP header objects. These are
treated as the request and response for a transaction respectively. Based on the transaction state
from :arg:`txnp` and the contents of the request and response, this returns ``TS_SUCCESS`` if the
response is cacheable, ``TS_ERROR`` otherwise.
