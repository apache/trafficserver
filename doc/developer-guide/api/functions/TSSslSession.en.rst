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

.. include:: ../../../common.defs
.. default-domain:: c

TSSslSession
************

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSSslSession TSSslSessionGet(const TSSslSessionID * sessionid)
.. function:: int TSSslSessionGetBuffer(const TSSslSessionID * sessionid, char * buffer, int * len_ptr)
.. function:: TSReturnCode TSSslSessionInsert(const TSSslSessionID * sessionid, TSSslSession addSession)
.. function:: TSReturnCode TSSslSessionRemove(const TSSslSessionID * sessionid)
.. function:: void TSSslTicketKeyUpdate(char * ticketData, int ticketDataLength)

Description
===========

These functions work with the internal ATS session cache.  These functions are only useful if the ATS internal
session cache is enabled by setting :ts:cv:`proxy.config.ssl.session_cache` has been set to 2.

These functions tend to be used with the :macro:`TS_SSL_SESSION_HOOK`.

The functions work with the :type:`TSSslSessionID` object to identify sessions to retrieve, insert, or delete.

The functions also work with the :type:`TSSslSession` object which can be cast to a pointer to the openssl SSL_SESSION object.

These functions perform the appropriate locking on the session cache to avoid errors.

The :func:`TSSslSessionGet` and :func:`TSSslSessionGetBuffer` functions retrieve the :type:`TSSslSession` object that is identified by the
:type:`TSSslSessionID` object.  If there is no matching session object, :func:`TSSslSessionGet` returns NULL and :func:`TSSslSessionGetBuffer`
returns 0.

:func:`TSSslSessionGetBuffer` returns the session information serialized in a buffer that can be shared between processes.
When the function is called len_ptr should point to the amount of space
available in the buffer parameter.  The function returns the amount of data really needed to encode the session.  len_ptr is updated with the amount of data actually stored in the buffer.

:func:`TSSslSessionInsert` inserts the session specified by the addSession parameter into the ATS session cache under the sessionid key.
If there is already an entry in the cache for the session id key, it is first removed before the new entry is added.

:func:`TSSslSessionRemove` removes the session entry from the session cache that is keyed by sessionid.

:func:`TSSslTicketKeyUpdate` updates the running ATS process to use a new set of Session Ticket Encryption keys.  This behaves the same way as
updating the session ticket encrypt key file with new data and reloading the current ATS process.  However, this API does not
require writing session ticket encryption keys to disk.

If both the ticket key files and :func:`TSSslTicketKeyUpdate` are used to update session ticket encryption keys, ATS will use the most recent update
regardless if whether it was made by file and configuration reload or API.
