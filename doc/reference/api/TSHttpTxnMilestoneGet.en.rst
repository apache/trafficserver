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

.. default-domain:: c

=====================
TSHttpTxnMilestoneGet
=====================

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSHttpTxnMilestoneGet(TSHttpTxn txnp, TSMilestonesType milestone, TSHRTime * time)

Description
===========

:func:`TSHttpTxnMilestoneGet` will fetch a specific milestone timer
value for the current request. These timers are calculated during
the lifetime of a transaction, and are all in :type:`TSHRTime` units
(nanoseconds), measured from the beginning of the transaction. The
:data:`time` argument is a pointer to a valid :type:`TSHRtime`
storage, and is set upon success.

The supported :type:`TSMilestonesType` milestone types are:

|
|
| :data:`TS_MILESTONE_UA_BEGIN`
| :data:`TS_MILESTONE_UA_READ_HEADER_DONE`
| :data:`TS_MILESTONE_UA_BEGIN_WRITE`
| :data:`TS_MILESTONE_UA_CLOSE`
| :data:`TS_MILESTONE_SERVER_FIRST_CONNECT`
| :data:`TS_MILESTONE_SERVER_CONNECT`
| :data:`TS_MILESTONE_SERVER_CONNECT_END`
| :data:`TS_MILESTONE_SERVER_BEGIN_WRITE`
| :data:`TS_MILESTONE_SERVER_FIRST_READ`
| :data:`TS_MILESTONE_SERVER_READ_HEADER_DONE`
| :data:`TS_MILESTONE_SERVER_CLOSE`
| :data:`TS_MILESTONE_CACHE_OPEN_READ_BEGIN`
| :data:`TS_MILESTONE_CACHE_OPEN_READ_END`
| :data:`TS_MILESTONE_CACHE_OPEN_WRITE_BEGIN`
| :data:`TS_MILESTONE_CACHE_OPEN_WRITE_END`
| :data:`TS_MILESTONE_DNS_LOOKUP_BEGIN`
| :data:`TS_MILESTONE_DNS_LOOKUP_END`
| :data:`TS_MILESTONE_SM_START`
| :data:`TS_MILESTONE_SM_FINISH`
| :data:`TS_MILESTONE_LAST_ENTRY`

Return values
=============

:data:`TS_SUCCESS` or :data:`TS_ERROR`.

See also
========
:manpage:`TSAPI(3ts)`
