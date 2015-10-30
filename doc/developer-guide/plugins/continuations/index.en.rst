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

.. _developer-plugins-continuations:

Continuations
*************

.. toctree::
   :maxdepth: 2

   activating-continuations.en
   writing-handler-functions.en

The continuation interface is Traffic Server's basic callback mechanism.
**Continuations** are instances of the opaque data type ``TSCont``. In
its basic form, a continuation represents a handler function and a
mutex.

This chapter covers the following topics:

-  `Mutexes and Data`_

-  :ref:`developer-plugins-continuations-activate`

-  :ref:`developer-plugins-continuations-handler-functions`

Mutexes and Data
================

A continuation must be created with a mutex if your continuation does
one of the following:

-  is registered globally (``TSHttpHookAdd`` or ``TSHttpSsnHookAdd``) to
   an HTTP hook and uses ``TSContDataSet/Get``

-  is registered locally (``TSHttpTxnHookAdd``), but for multiple
   transactions uses ``TSContDataSet/Get``

-  uses ``TSCacheXXX``, ``TSNetXXX``, ``TSHostLookup``, or
   ``TSContSchedule`` APIs

Before being activated, a caller must grab the continuation's mutex.
This requirement makes it possible for a continuation's handler function
to safely access its data and to prevent multiple callers from running
it at the same time (see the :ref:`about-the-sample-protocol` for usage). The
data protected by the mutex is any global or continuation data
associated to the continuation by ``TSContDataSet``. This does not
include the local data created by the continuation handler function. A
typical example of continuations created with associated data structures
and mutexes is the transaction state machine created in the sample
Protocol plugin (see :ref:`one-way-to-implement-a-transaction-state-machine`).

A reentrant call occurs when the continuation passed as an argument to
the API can be called in the same stack trace as the function calling
the API. For example, if you call ``TSCacheRead`` (``contp, mykey``), it
is possible that ``contp``'s handler will be called directly and then
``TSCacheRead`` returns.

Caveats that could cause issues include the following:

-  a continuation has data associated with it (``TSContDataGet``).

-  the reentrant call passes itself as a continuation to the reentrant
   API. In this case, the continuation should not try to access its data
   after calling the reentrant API. The reason for this is that data may be
   modified by the section of code in the continuation's handler that
   handles the event sent by the API. It is recommended that you always
   return after a reentrant call to avoid accessing something that has been
   deallocated.

Below is an example, followed by an explanation.

.. code-block:: c

    continuation_handler (TSCont contp, TSEvent event, void *edata) {
        switch (event) {
            case event1:
                TSReentrantCall (contp);
                /* Return right away after this call */
                break;
            case event2:
                TSContDestroy (contp);
                break;
        }
    }

The above example first assumes that the continuation is called back
with ``event1``; it then does the first reentrant call that schedules
the continuation to receive ``event2``. Because the call is reentrant,
the processor calls back the continuation right away with ``event2`` and
the continuation is destroyed. If you try to access the continuation or
one of its members after the reentrant call, then you might access
something that has been deallocated. To avoid accessing something that
has been deallocated, never access the continuation or any of its
members after a reentrant call - just exit the handler.

**Note:** Most HTTP transaction plugin continuations do not need
non-null mutexes because they're called within the processing of an HTTP
transaction, and therefore have the transaction's mutex.

It is also possible to specify a continuation's mutex as ``NULL``. This
should be done only when registering a continuation to a global hook, by
a call to ``TSHttpHookAdd``. In this case, the continuation can be
called simultaneously by different instances of HTTP SM running on
different threads. Having a mutex here would slow and/or hinder Traffic
Server performance, since all the threads will try to lock the same
mutex. The drawback of not having a mutex is that such a continuation
cannot have data associated with it (i.e., ``TSContDataGet/Set`` cannot
be used).

When using a ``NULL`` mutex it is dangerous to access the continuation's
data, but usually continuations with ``NULL`` mutexes have no data
associated with them anyway. An example of such a continuation is one
that gets called back every time an HTTP request is read, and then
determines from the request alone if the request should go through or be
rejected. An HTTP transaction gives its continuation data to the
``contp``.

