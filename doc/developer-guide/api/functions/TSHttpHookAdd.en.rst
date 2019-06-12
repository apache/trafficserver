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

TSHttpHookAdd
*************

Intercept Traffic Server events.

Synopsis
========

`#include <ts/ts.h>`

.. function:: void TSHttpHookAdd(TSHttpHookID id, TSCont contp)
.. function:: void TSHttpSsnHookAdd(TSHttpSsn ssnp, TSHttpHookID id, TSCont contp)
.. function:: void TSHttpTxnHookAdd(TSHttpTxn txnp, TSHttpHookID id, TSCont contp)

Description
===========

Hooks are points in Apache Traffic Server transaction HTTP processing
where plugins can step in and do some work. Registering a plugin
function for callback amounts to adding the function to a hook. You
can register your plugin to be called back for every single
transaction, or for specific transactions only.

HTTP :term:`transaction` hooks are set on a global basis using the function
:func:`TSHttpHookAdd`. This means that the continuation specified
as the parameter to :func:`TSHttpHookAdd` is called for every
transaction. :func:`TSHttpHookAdd` must only be called from
:func:`TSPluginInit` or :func:`TSRemapInit`.

:func:`TSHttpSsnHookAdd` adds :arg:`contp` to
the end of the list of HTTP :term:`session` hooks specified by :arg:`id`.
This means that :arg:`contp` is called back for every transaction
within the session, at the point specified by the hook ID. Since
:arg:`contp` is added to a session, it is not possible to call
:func:`TSHttpSsnHookAdd` from the plugin initialization routine;
the plugin needs a handle to an HTTP session.

:func:`TSHttpTxnHookAdd` adds :arg:`contp`
to the end of the list of HTTP transaction hooks specified by
:arg:`id`. Since :arg:`contp` is added to a transaction, it is
not possible to call :func:`TSHttpTxnHookAdd` from the plugin
initialization routine but only when the plugin has a handle to an
HTTP transaction.

A single continuation can be attached to multiple hooks at the same time.
It is good practice to conserve resources by reusing hooks in this way
when possible.

When a continuation on a hook is triggered, the name of the event passed to
the continuation function depends on the name of the hook.  The naming
convention is that, for hook TS_xxx_HOOK, the event passed to the continuation
function will be TS_EVENT_xxx.  For example, when a continuation attached to
TS_HTTP_READ_REQUEST_HDR_HOOK is triggered, the event passed to the continuation
function will be TS_EVENT_HTTP_READ_REQUEST_HDR.

When a continuation is triggered by a hook, the actual type of the event data
(the void pointer passed as the third parameter to the continuation function) is
determined by which hook it is.  For example, for the hook ID TS_HTTP_TXN_CLOSE_HOOK,
the event data is of type :type:`TSHttpTxn`.  This is the case regardless of whether the
continuation was added to the hook using :func:`TSHttpTxnHookAdd`, :func:`TSHttpSsnHookAdd`
or :func:`TSHttpHookAdd`.  If the event data is of type :type:`TSHttpTxn`, :type:`TSHttpSsn` or
:type:`TSVConn`, the continuation function can assume the mutex of the indicated
event data object is locked.  (But the continuation function must not unlock it.)

Return Values
=============

None. Adding hooks is always successful.

Examples
========

The following example demonstrates how to add global, session and
transaction hooks::

    #include <ts/ts.h>

    static int
    handler(TSCont contp, TSEvent event, void *edata)
    {
        TSHttpSsn ssnp;
        TSHttpTxn txnp;

        switch (event){
        case TS_EVENT_HTTP_SSN_START:
            ssnp = (TSHttpSsn) edata;
            // Add a session hook ...
            TSHttpSsnHookAdd(ssnp, TS_HTTP_TXN_START_HOOK, contp);
            TSHttpSsnReenable(ssnp, TS_EVENT_HTTP_CONTINUE);
            return 0;
        case TS_EVENT_HTTP_TXN_START:
            txnp = (TSHttpTxn) edata;
            // Add a transaction hook ...
            TSHttpTxnHookAdd(txnp, TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
            TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
            return 0;
        case TS_EVENT_HTTP_READ_REQUEST_HDR:
            txnp = (TSHttpTxn) edata;
            // ...
            TSHttpTxnReenable(txnp, TS_EVENT_HTTP_CONTINUE);
            return 0;
        default:
             break;
        }

        return 0;
    }

    void
    TSPluginInit (int argc, const char *argv[])
    {
        TSCont contp;
        contp = TSContCreate(handler, NULL);
        TSHttpHookAdd(TS_HTTP_SSN_START_HOOK, contp);
    }

For more example code using hooks, see the test_hooks plugin in tests/tools/plugins (used by the test_hooks.test.py
Gold test).

See Also
========

:manpage:`TSAPI(3ts)`,
:manpage:`TSContCreate(3ts)`,
:manpage:`TSLifecycleHookAdd(3ts)`
