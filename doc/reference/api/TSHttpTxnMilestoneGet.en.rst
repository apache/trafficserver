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

Get a specified milestone timer value for the current transaction.

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSReturnCode TSHttpTxnMilestoneGet(TSHttpTxn txnp, TSMilestonesType milestone, TSHRTime* time)

Description
===========

:func:`TSHttpTxnMilestoneGet` will fetch a specific :arg:`milestone` timer value for the transaction :arg:`txnp`. These timers are
calculated during the lifetime of a transaction and are measured in nanoseconds from the beginning of the transaction.
:arg:`time` is used a pointer to storage to update if the call is successful.

.. type:: TSMilestonesType

=============================================== ==========
Value                                           Milestone
=============================================== ==========
:const:`TS_MILESTONE_UA_BEGIN`                  The client connection is accepted.
:const:`TS_MILESTONE_UA_READ_HEADER_DONE`       The request header from the client has been read and parsed.
:const:`TS_MILESTONE_UA_BEGIN_WRITE`            The response header write to the client starts.
:const:`TS_MILESTONE_UA_CLOSE`                  Last I/O activity on the client socket, or connection abort.
:const:`TS_MILESTONE_SERVER_FIRST_CONNECT`      First time origin server connect attempted or shared shared session attached.
:const:`TS_MILESTONE_SERVER_CONNECT`            Most recent time origin server connect attempted or shared session attached.
:const:`TS_MILESTONE_SERVER_CONNECT_END`        More recent time a connection attempt was resolved.
:const:`TS_MILESTONE_SERVER_BEGIN_WRITE`        First byte is written to the origin server connection.
:const:`TS_MILESTONE_SERVER_FIRST_READ`         First byte is read from connection to origin server.
:const:`TS_MILESTONE_SERVER_READ_HEADER_DONE`   Origin server response has been read and parsed.
:const:`TS_MILESTONE_SERVER_CLOSE`              Last I/O activity on origin server connection.
:const:`TS_MILESTONE_CACHE_OPEN_READ_BEGIN`     Initiate read of the cache.
:const:`TS_MILESTONE_CACHE_OPEN_READ_END`       Initial cache read has resolved.
:const:`TS_MILESTONE_CACHE_OPEN_WRITE_BEGIN`    Start open for cache write.
:const:`TS_MILESTONE_CACHE_OPEN_WRITE_END`      Cache has been opened for write.
:const:`TS_MILESTONE_DNS_LOOKUP_BEGIN`          Initiate host resolution in HostDB
:const:`TS_MILESTONE_DNS_LOOKUP_END`            Host resolution resolves.
:const:`TS_MILESTONE_SM_START`                  Transaction state machine is initialized.
:const:`TS_MILESTONE_SM_FINISH`                 Transaction has finished, state machine final logging has started.
=============================================== ==========

*  The server connect times predate the transmission of the ``SYN`` packet. That is, before a connection to the
   origin server is completed.

*  A connection attempt is *resolved* when no more connection related activity remains to be done, and the connection is
   either established or has failed.

*  :const:`TS_MILESTONE_UA_CLOSE` and :const:`TS_MILESTONE_SERVER_CLOSE` are updated continuously during the life of the
   transaction, every time there is I/O activity. The updating stops when the corresponding connection is closed,
   leaving the last I/O time as the final value.

*  The cache ``OPEN`` milestones time only the initial setup, the "open", not the full read or write.

Return values
=============

:const:`TS_SUCCESS` if successful and :arg:`time` was updated, otherwise :const:`TS_ERROR`.

See also
========
:manpage:`TSAPI(3ts)`
