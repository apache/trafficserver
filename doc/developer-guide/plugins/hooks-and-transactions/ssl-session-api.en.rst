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

.. _developer-plugins-ssl-session-hooks:

.. default-domain:: cpp

TLS Session Ticket Key Plugin API
**********************************

This interface enables a plugin to update the session ticket encryption keys used for TLS session resumption.

.. note::

   The session ID-based session cache and its associated APIs (``TSSslSessionGet``, ``TSSslSessionGetBuffer``,
   ``TSSslSessionInsert``, ``TSSslSessionRemove``, and ``TS_SSL_SESSION_HOOK``) were removed in ATS 11.x.
   TLS session resumption is now only supported via session tickets.

Utility Functions
*****************

* :func:`TSSslTicketKeyUpdate`

Example Use Case
****************

Consider deploying a set of ATS servers as a farm behind a layer 4 load balancer. To enable TLS session
ticket-based resumption across all servers, they need to share the same session ticket encryption keys.

A plugin can engage in a protocol to periodically update the session ticket encryption key and communicate
the new key to its peers. The plugin calls :func:`TSSslTicketKeyUpdate` to update the local ATS process
with the newest keys and the last N keys.
