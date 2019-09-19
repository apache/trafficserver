.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. default-domain:: c

Session Properties
*******************

Retrieve session properties.

Synopsis
========

`#include <ts/ts.h>`

.. function:: int64_t TSHttpSsnIdGet(TSHttpSsn ssnp)
.. function:: int TSHttpSsnTransactionCount(TSHttpSsn ssnp)
.. function:: TSHRTime TSHttpSsnStartTime(TSHttpSsn ssnp)
.. function:: TSReturnCode TSHttpSsnClientFdGet(TSHttpTxn txnp, int *fdp)

Description
===========

:func:`TSHttpSsnIdGet` returns the unique identifier for the session. This is unique across all
sessions of the same type. E.g., for all inbound (client) sessions, or all outbound (upstream)
sessions.

:func:`TSHttpSsnTransactionCount` returns the number of transactions that have been started on this
session.

:func:`TSHttpSsnStartTime` returns the time at which the session started, using the same clock as
the milestones (see :func:`TSHttpTxnMilestoneGet`).

:func:`TSHttpSsnClientFdGet` retrieves the underlying file descriptor for the session and places it
in :arg:`fdp`. It returns :macro:`TS_SUCCESS` if :arg:`fdp` was updated with a valid file descriptor,
:macro:`TS_ERROR` if not.

See also
========

:manpage:`TSAPI(3ts)`
