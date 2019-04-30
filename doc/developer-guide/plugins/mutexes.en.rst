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

.. include:: ../../common.defs

.. _developer-plugins-mutexes:

Mutexes
*******

.. toctree::
   :maxdepth: 1

A *mutex* is the basic synchronization method used within Traffic
Server to protect data from simultaneous access by multiple threads. A
mutex acts as a lock that protects data in one program thread from being
accessed by another thread.

The Traffic Server API provides two functions that attempt to access and
lock the data: :c:func:`TSMutexLockTry` and :c:func:`TSMutexLock`.
``TSMutexLock`` is a blocking call - if you use it, you can slow
Traffic Server performance because transaction processing pauses until
the mutex is unlocked. It should be used only on threads created by the
plugin ``TSContThreadCreate``. Never use it on a continuation handler
called back by the Cache, Net, or Event Processor. Even if the critical
section is very small, do not use it. If you need to update a flag, then
set a variable and/or use atomic operations. If :c:func:`TSMutexLock` is used
in any case other than the one recommended above, then the result will
be a serious performance impact.

``TSMutexLockTry``, on the other hand, attempts to lock the mutex
only if it is unlocked (i.e., not being used by another thread). It
should be used in all cases other than the above mentioned
``TSMutexLock`` case. If the ``TSMutexLockTry`` attempt fails, then you
can schedule a future attempt (which must be at least 10 milliseconds
later).

In general, you should use ``TSMutexLockTry`` instead of
``TSMutexLock``.

-  ``TSMutexLockTry`` is required if you are tying to lock Traffic
   Server internal or system resources (such as the network, cache,
   event processor, HTTP state machines, and IO buffers).

-  ``TSMutexLockTry`` is required if you are making any blocking calls
   (such as network, cache, or file IO calls).

-  ``TSMutexLock`` might not be necessary if you are not making
   blocking calls and if you are only accessing local resources.

The Traffic Server API uses the ``TSMutex`` type for a mutex. There are
two typical uses of mutex. One use is for locking global data or data
shared by various continuations. The other typical usage is for locking
data associated with a continuation (i.e., data that might be accessed
by other continuations).

Locking Global Data
===================

The :ref:`blacklist-1.c` sample plugin implements a mutex that locks global
data. The blacklist plugin reads its blacklisted sites from a
configuration file; file read operations are protected by a mutex
created in :c:func:`TSPluginInit`. The :ref:`blacklist-1.c` code uses
:c:func:`TSMutexLockTry` instead of :c:func:`TSMutexLock`. For more detailed
information, see the :ref:`blacklist-1.c` code;
start by looking at the :c:func:`TSPluginInit` function.

General guidelines for locking shared data are as follows:

1. Create a mutex for the shared data with
   :c:func:`TSMutexCreate`.

2. Whenever you need to read or modify this data, first lock it by
   calling
   :c:func:`TSMutexLockTry`;
   then read or modify the data.

3. When you are done with the data, unlock it with
   :c:func:`TSMutexUnlock`.
   If you are unlocking data accessed during the processing of an HTTP
   transaction, then you must unlock it before calling
   :c:func:`TSHttpTxnReenable`.

Protecting a Continuation's Data
================================

You must create a mutex to protect a continuation's data if it might be
accessed by other continuations or processes. Here's how:

1. | Create a mutex for the continuation using ``TSMutexCreate``.
   | For example:

   .. code-block:: c

       TSMutex mutexp;
       mutexp = TSMutexCreate ();

2. | When you create the continuation, specify this mutex as the
     continuation's mutex.
   | For example:

   .. code-block:: c

       TSCont contp;
       contp = TSContCreate (handler, mutexp);

If any other functions want to access ``contp``'s data, then it is up to
them to get ``contp``'s mutex (using, for example, ``TSContMutexGet``)
to lock it. For usage, see the sample Protocol plugin.

How to Associate a Continuation With Every HTTP Transaction
===========================================================

There could be several reasons why you'd want to create a continuation
for each HTTP transaction that calls back your plugin.

Some potential scenarios are listed below.

-  You want to register hooks locally with the new continuation instead
   of registering them globally to the continuation plugin.

-  You want to store data specific to each HTTP transaction that you
   might need to reuse across various hooks.

-  You're using APIs (like ``TSHostLookup``) that will call back the
   continuation with a certain event.

How to Add the New Continuation
===============================

A typical way of adding the new continuation is by registering the
plugin continuation to be called back by HTTP transactions globally when
they reach ``TS_HTTP_TXN_START_HOOK``. Refer to the example below, which
uses a transaction-specific continuation called ``txn_contp``.

.. code-block:: c

           void TSPluginInit(int argc, const char *argv[])
           {
               /* Plugin continuation */
               TSCont contp;
               if ((contp = TSContCreate (plugin_cont_handler, NULL)) == TS_ERROR_PTR) {
                   LOG_ERROR("TSContCreate");
               } else {
                   if (TSHttpHookAdd (TS_HTTP_TXN_START_HOOK, contp) == TS_ERROR) {
                       LOG_ERROR("TSHttpHookAdd");
                   }
               }
           }

In the plugin continuation handler, create the new continuation
``txn_contp`` and then register it to be called back at
``TS_HTTP_TXN_CLOSE_HOOK``:

.. code-block:: c

           static int plugin_cont_handler(TSCont contp, TSEvent event, void *edata)
           {
               TSHttpTxn txnp = (TSHttpTxn)edata;
               TSCont txn_contp;

               switch (event) {
                   case TS_EVENT_HTTP_TXN_START:
                       /* Create the HTTP txn continuation */
                       txn_contp = TSContCreate(txn_cont_handler, NULL);

                       /* Register txn_contp to be called back when txnp reaches TS_HTTP_TXN_CLOSE_HOOK */
                       if (TSHttpTxnHookAdd (txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp) == TS_ERROR) {
                           LOG_ERROR("TSHttpTxnHookAdd");
                       }

                       break;

                   default:
                       TSAssert(!"Unexpected Event");
                       break;
               }

               if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
                   LOG_ERROR("TSHttpTxnReenable");
               }

               return 0;
           }

Remember that the ``txn_contp`` handler must destroy itself when the
HTTP transaction is closed. If you forget to do this, then your plugin
will have a memory leak.

.. code-block:: c


           static int txn_cont_handler(TSCont txn_contp, TSEvent event, void *edata)
           {
               TSHttpTxn txnp;
               switch (event) {
                   case TS_EVENT_HTTP_TXN_CLOSE:
                       txnp = (TSHttpTxn) edata;
                       TSContDestroy(txn_contp);
                       break;

                   default:
                       TSAssert(!"Unexpected Event");
                       break;
               }

               if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
                   LOG_ERROR("TSHttpTxnReenable");
               }

               return 0;
           }

How to Store Data Specific to Each HTTP Transaction
===================================================

For the example above, store the data in the ``txn_contp`` data
structure - this means that you'll create your own data structure. Now
suppose you want to store the state of the HTTP transaction:

.. code-block:: c

       typedef struct {
             int state;
         } ContData;

You need to allocate the memory and initialize this structure for each
HTTP ``txnp``. You can do that in the plugin continuation handler when
it is called back with ``TS_EVENT_HTTP_TXN_START``

.. code-block:: c

           static int plugin_cont_handler(TSCont contp, TSEvent event, void *edata)
           {
               TSHttpTxn txnp = (TSHttpTxn)edata;
               TSCont txn_contp;
               ContData *contData;

               switch (event) {
                   case TS_EVENT_HTTP_TXN_START:
                       /* Create the HTTP txn continuation */
                       txn_contp = TSContCreate(txn_cont_handler, NULL);

                       /* Allocate and initialize the txn_contp data */
                       contData = (ContData*) TSmalloc(sizeof(ContData));
                       contData->state = 0;
                       if (TSContDataSet(txn_contp, contData) == TS_ERROR) {
                           LOG_ERROR("TSContDataSet");
                       }

                       /* Register txn_contp to be called back when txnp reaches TS_HTTP_TXN_CLOSE_HOOK */
                       if (TSHttpTxnHookAdd (txnp, TS_HTTP_TXN_CLOSE_HOOK, txn_contp) == TS_ERROR) {
                           LOG_ERROR("TSHttpTxnHookAdd");
                       }

                       break;

                   default:
                       TSAssert(!"Unexpected Event");
                       break;
               }

               if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
                   LOG_ERROR("TSHttpTxnReenable");
               }

               return 0;
           }

For accessing this data from anywhere, use TSContDataGet:

.. code-block:: c

           TSCont txn_contp;
           ContData *contData;

           contData = TSContDataGet(txn_contp);
           if (contData == TS_ERROR_PTR) {
               LOG_ERROR("TSContDataGet");
           }
           contData->state = 1;

Remember to free this memory before destroying the continuation:

.. code-block:: c

           static int txn_cont_handler(TSCont txn_contp, TSEvent event, void *edata)
           {
               TSHttpTxn txnp;
               ContData *contData;
               switch (event) {
                   case TS_EVENT_HTTP_TXN_CLOSE:
                       txnp = (TSHttpTxn) edata;
                       contData = TSContDataGet(txn_contp);
                       if (contData == TS_ERROR_PTR) {
                           LOG_ERROR("TSContDataGet");
                       } else {
                           TSfree(contData);
                       }
                       TSContDestroy(txn_contp);
                       break;

                   default:
                       TSAssert(!"Unexpected Event");
                       break;
               }

               if (TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE) == TS_ERROR) {
                   LOG_ERROR("TSHttpTxnReenable");
               }

               return 0;
           }

Using Locks
===========

You do not need to use locks when a continuation has registered itself
to be called back by HTTP hooks and it only uses the HTTP APIs. In the
example above, the continuation ``txn_contp`` has registered itself to
be called back at HTTP hooks and it only uses the HTTP APIs. In this
case only, it's safe to access data shared between ``txnp`` and
``txn_contp`` without grabbing a lock. In the example above,
``txn_contp`` is created with a ``NULL`` mutex. This works because the
HTTP transaction ``txnp`` is the only one that will call back
``txn_contp``, and you are guaranteed that ``txn_contp`` will be called
back only one hook at a time. After processing is finished,
``txn_contp`` will reenable ``txnp``.

In all other cases, you should create a mutex with the continuation. In
general, a lock is needed when you're using iocore APIs or any other API
where ``txn_contp`` is scheduled to be called back by a processor (such
as the cache processor, the DNS processor, etc.). This ensures that
``txn_contp`` is called back sequentially and not simultaneously. In
other words, you need to ensure that ``txn_contp`` will not be called
back by both ``txnp`` and the cache processor at the same time, since
this will result in a situation wherein you're executing two pieces of
code in conflict.

Special Case: Continuations Created for HTTP Transactions
=========================================================

If your plugin creates a new continuation for each HTTP transaction,
then you probably don't need to create a new mutex for it because each
HTTP transaction (``TSHttpTxn`` object) already has its own mutex.

In the example below, it's not necessary to specify a mutex for the
continuation created in ``txn_handler``:

.. code-block:: c

    static void
    txn_handler (TSHttpTxn txnp, TSCont contp) {
        TSCont newCont;
        ....
            newCont = TSContCreate (newCont_handler, NULL);
        //It's not necessary to create a new mutex for newCont.

        ...

            TSHttpTxnReenable (txnp, TS_EVENT_HTTP_CONTINUE);
    }

   static int
   test_plugin (TSCont contp, TSEvent event, void *edata) {
       TSHttpTxn txnp = (TSHttpTxn) edata;

       switch (event) {
           case TS_EVENT_HTTP_READ_REQUEST_HDR:
               txn_handler (txnp, contp);
               return 0;
           default:
               break;
       }
       return 0;
   }

The mutex functions are listed below:

-  :c:func:`TSMutexCreate`
-  :c:func:`TSMutexLock`
-  :c:func:`TSMutexLockTry`

