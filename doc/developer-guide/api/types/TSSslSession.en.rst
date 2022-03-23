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
**************

Synopsis
========

.. code-block:: cpp

    #include <ts/apidefs.h>

.. type:: TSSslSessionID

   .. member:: size_t len

   .. member:: char bytes[TS_SSL_MAX_SSL_SESSION_ID_LENGTH]

.. type:: TSSslSession

Description
===========

:type:`TSSslSessionID` represents the SSL session ID as a buffer and length.  The ``TS_SSL_MAX_SSL_SESSION_ID_LENGTH`` is the same value
as the OpenSSL constant ``SSL_MAX_SSL_SESSION_ID_LENGTH``. The plugin has direct access to this object since creating and
manipulating session IDs seems like a fairly common operation (rather than providing an API to access the data via an
opaque TS object type).


:type:`TSSslSession` references the SSL session object.  It can be cast to the OpenSSL type ``SSL_SESSION``.

