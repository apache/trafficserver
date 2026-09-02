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
.. default-domain:: cpp

TSSslTicketKeyUpdate
********************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSSslTicketKeyUpdate(char * ticketData, int ticketDataLength)

Description
===========

.. note::

   The session ID-based session cache and its associated APIs (``TSSslSessionGet``, ``TSSslSessionGetBuffer``,
   ``TSSslSessionInsert``, ``TSSslSessionRemove``) were removed in ATS 11.x.
   TLS session resumption is now only supported via session tickets.

:func:`TSSslTicketKeyUpdate` updates the running ATS process to use a new set of Session Ticket Encryption keys.
This behaves the same way as updating the session ticket encrypt key file with new data and reloading the
current ATS process. However, this API does not require writing session ticket encryption keys to disk.

If both the ticket key files and :func:`TSSslTicketKeyUpdate` are used to update session ticket encryption keys,
ATS will use the most recent update regardless of whether it was made by file and configuration reload or API.
