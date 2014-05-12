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

=============
TSHttpHookAdd
=============

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
transaction. :func:`TSHttpHookAdd` is typically called from
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

Return values
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
            TSHttpTxnHookAdd(ssnp, TS_HTTP_READ_REQUEST_HDR_HOOK, contp);
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

See also
========
:manpage:`TSAPI(3ts)`, :manpage:`TSContCreate(3ts)`, :manpage:`TSLifecycleHookAdd(3ts)`
