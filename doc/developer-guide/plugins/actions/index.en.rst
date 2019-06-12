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

.. _developer-plugins-actions:

Actions
*******

.. toctree::
   :maxdepth: 2

   hosts-lookup-api.en

An **action** is a handle to an operation initiated by a plugin that has
not yet completed. For example: when a plugin connects to a remote
server, it uses the call ``TSNetConnect`` - which takes ``TSCont`` as an
argument to call back when the connection is established.
``TSNetConnect`` might not call the continuation back immediately and
will return an ``TSAction`` structure that the caller can use to cancel
the operation. Cancelling the operation does not necessarily mean that
the operation will not occur; it simply means that the continuation
passed into the operation will not be called back. In such an example,
the connection might still occur if the action is cancelled; however,
the continuation that initiated the connection would not be called back.

In the preceding example, it is also possible that the connection will
complete and call back the continuation before ``TSNetConnect`` returns.
If that occurs, then ``TSNetConnect`` returns a special action that
causes ``TSActionDone`` to return ``1``. This specifies that the
operation has already completed, so it's pointless to try to cancel the
operation. Also note that an action will never change from non-completed
to completed. When the operation actually succeeds and the continuation
is called back, the continuation must zero out its action pointer to
indicate to itself that the operation succeeded.

The asynchronous nature of all operations in Traffic Server necessitates
actions. You should notice from the above discussion that once a call to
a function like ``TSNetConnect`` is made by a continuation and that
function returns a valid action (``TSActionDone`` returns ``0``), it is
not safe for the continuation to do anything else except return from its
handler function. It is not safe to modify or examine the continuation's
data because the continuation may have already been destroyed.

Below is an example of typical usage for an action:

.. code-block:: c

        #include <ts/ts.h>
        static int
        handler (TSCont contp, TSEvent event, void *edata)
        {
            if (event == TS_EVENT_IMMEDIATE) {
                TSAction actionp = TSNetConnect (contp, 127.0.0.1, 9999);
                if (!TSActionDone (actionp)) {
                    TSContDataSet (contp, actionp);
                } else {
                    /* We've already been called back... */
                    return 0;
                }
            } else if (event == TS_EVENT_NET_CONNECT) {
                /* Net connection succeeded */
                TSContDataSet (contp, NULL);
                return 0;
            } else if (event == TS_EVENT_NET_CONNECT_FAILED) {
                /* Net connection failed */
                TSContDataSet (contp, NULL);
                return 0;
            }
            return 0;
        }

        void
        TSPluginInit (int argc, const char *argv[])
        {
            TSCont contp;

            contp = TSContCreate (handler, TSMutexCreate ());

            /* We don't want to call things out of TSPluginInit
               directly since it's called before the rest of the
               system is initialized. We'll simply schedule an event
               on the continuation to occur as soon as the rest of
               the system is started up. */
            TSContScheduleOnPool (contp, 0, TS_THREAD_POOL_NET);
        }

The example above shows a simple plugin that creates a continuation and
then schedules it to be called immediately. When the plugin's handler
function is called the first time, the event is ``TS_EVENT_IMMEDIATE``.
The plugin then tries to open a net connection to port 9999 on
``localhost`` (127.0.0.1). The IP description was left in cider notation
to further clarify what is going on; also note that the above won't
actually compile until the IP address is modified. The action returned
from ``TSNetConnect`` is examined by the plugin. If the operation has
not completed, then the plugin stores the action in its continuation.
Otherwise, the plugin knows it has already been called back and there is
no reason to store the action pointer.

A final question might be, "why would a plugin want to cancel an
action?" In the above example, a valid reason could be to place a limit
on the length of time it takes to open a connection. The plugin could
schedule itself to get called back in 30 seconds and then initiate the
net connection. If the timeout expires first, then the plugin would
cancel the action. The following sample code implements this:

.. code-block:: c

        #include <ts/ts.h>
        static int
        handler (TSCont contp, TSEvent event, void *edata)
        {
            switch (event) {
                case (TS_EVENT_IMMEDIATE):
                    TSContScheduleOnPool (contp, 30000, TS_THREAD_POOL_NET);
                    TSAction actionp = TSNetConnect(contp, 127.0.0.1, 9999);
                    if (!TSActionDone (actionp)) {
                        TSContDataSet (contp, actionp);
                    } else {
                        /* We've already been called back ... */
                    }
                    break;

                case (TS_EVENT_TIMEOUT):
                    TSAction actionp = TSContDataGet (contp);
                    if (!TSActionDone(actionp)) {
                        TSActionCancel (actionp);
                    }
                    break;

                case (TS_EVENT_NET_CONNECT):
                    /* Net connection succeeded */
                    TSContDataSet (contp, NULL);
                    break;

                case (TS_EVENT_NET_CONNECT_FAILED):
                    /* Net connection failed */
                    TSContDataSet (contp, NULL);
                    break;

            }
            return 0;
        }

        void
        TSPluginInit (int argc, const char *argv[])
        {
            TSCont contp;

            contp = TSContCreate (handler, TSMutexCreate ());
            /* We don't want to call things out of TSPluginInit
               directly since it's called before the rest of the
               system is initialized. We'll simply schedule an event
               on the continuation to occur as soon as the rest of
               the system is started up. */
            TSContScheduleOnPool (contp, 0, TS_THREAD_POOL_NET);
        }

The action functions are:

-  :c:func:`TSActionCancel`
-  :c:func:`TSActionDone`
