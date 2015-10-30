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

.. _developer-plugins-io-vios:

VIOs
****

A **VIO**, or **virtual IO**, is a description of an IO operation that's
currently in progress. The VIO data structure is used by vconnection
users to determine how much progress has been made on a particular IO
operation and to re-enable an IO operation when it stalls due to buffer
space issues. VIOs are used by vconnection implementors to determine the
buffer for an IO operation, how much work to do on the IO operation, and
which continuation to call back when progress on the IO operation is
made.

The ``TSVIO`` data structure itself is opaque, but it could be defined
as follows:

.. code-block:: c

    typedef struct {
        TSCont continuation;
        TSVConn vconnection;
        TSIOBufferReader reader;
        TSMutex mutex;
        int nbytes;
        int ndone;
    } *TSVIO;

The VIO functions below access and modify various parts of the data
structure.

-  :c:func:`TSVIOBufferGet`
-  :c:func:`TSVIOVConnGet`
-  :c:func:`TSVIOContGet`
-  :c:func:`TSVIOMutexGet`
-  :c:func:`TSVIONBytesGet`
-  :c:func:`TSVIONBytesSet`
-  :c:func:`TSVIONDoneGet`
-  :c:func:`TSVIONDoneSet`
-  :c:func:`TSVIONTodoGet`
-  :c:func:`TSVIOReaderGet`
-  :c:func:`TSVIOReenable`

