Writing Handler Functions
*************************

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

The handler function is the key component of a continuation. It is
supposed to examine the event and event data, and then do something
appropriate. The probable action might be to schedule another event for
the continuation to received, to open up a connection to a server, or
simply to destroy itself.

The continuation's handler function is a function of type
``TSEventFunc``. Its arguments are a continuation, an event, and a
pointer to some data (this data is passed to the continuation by the
caller - do not confuse this data with the continuation's own data,
associated by ``TSContDataSet``). When the continuation is called back,
the continuation and an event are passed to the handler function. The
continuation is a handle to the same continuation that is invoked. The
handler function typically has a switch statement to handle the events
it receives:

.. code-block:: c

   static int some_handler (TScont contp, TSEvent event, void *edata)
   {
      // .....
      switch(event) {
         case TS_EVENT_SOME_EVENT_1:
            do_some_thing_1;
            return;
         case TS_EVENT_SOME_EVENT_2:
            do_some_thing_2;
            return;
         case TS_EVENT_SOME_EVENT_3:
            do_some_thing_3;
            return;
         default: break;
      }
      return 0;
   }

.. caution::

   You might notice that a continuation cannot determine if more events are
   "in flight" toward it. Do not use ``TSContDestroy`` to delete a
   continuation before you make sure that all incoming events, such as
   those sent because of ``TSHttpTxnHookAdd``, have been handled.

The following table lists events and the corresponding type of
``void* data`` passed to handler functions:

======================================== ======================================= ======================
Event                                    Event Sender                            Data Type
======================================== ======================================= ======================
``TS_EVENT_HTTP_READ_REQUEST_HDR``       ``TS_HTTP_READ_REQUEST_HDR_HOOK``       ``TSHttpTxn``
``TS_EVENT_HTTP_OS_DNS``                 ``TS_HTTP_OS_DNS_HOOK``                 ``TSHttpTxn``
``TS_EVENT_HTTP_SEND_REQUEST_HDR``       ``TS_HTTP_SEND_REQUEST_HDR_HOOK``       ``TSHttpTxn``
``TS_EVENT_HTTP_READ_CACHE_HDR``         ``TS_HTTP_READ_CACHE_HDR_HOOK``         ``TSHttpTxn``
``TS_EVENT_HTTP_READ_RESPONSE_HDR``      ``TS_HTTP_READ_RESPONSE_HDR_HOOK``      ``TSHttpTxn``
``TS_EVENT_HTTP_SEND_RESPONSE_HDR``      ``TS_HTTP_SEND_RESPONSE_HDR_HOOK``      ``TSHttpTxn``
``TS_EVENT_HTTP_SELECT_ALT``             ``TS_HTTP_SELECT_ALT_HOOK``             ``TSHttpTxn``
``TS_EVENT_HTTP_TXN_START``              ``TS_HTTP_TXN_START_HOOK``              ``TSHttpTxn``
``TS_EVENT_HTTP_TXN_CLOSE``              ``TS_HTTP_TXN_CLOSE_HOOK``              ``TSHttpTxn``
``TS_EVENT_HTTP_SSN_START``              ``TS_HTTP_SSN_START_HOOK``              ``TSHttpSsn``
``TS_EVENT_HTTP_SSN_CLOSE``              ``TS_HTTP_SSN_CLOSE_HOOK``              ``TSHttpSsn``
``TS_EVENT_NONE``
``TS_EVENT_CACHE_LOOKUP_COMPLETE``       ``TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK``  ``TSHttpTxn``
``TS_EVENT_IMMEDIATE``                   ``TSVConnClose``
                                         ``TSVIOReenable``
                                         ``TSContSchedule``
``TS_EVENT_IMMEDIATE``                   ``TS_HTTP_REQUEST_TRANSFORM_HOOK``
``TS_EVENT_IMMEDIATE``                   ``TS_HTTP_RESPONSE_TRANSFORM_HOOK``
``TS_EVENT_CACHE_OPEN_READ``             ``TSCacheRead``                         Cache VC
``TS_EVENT_CACHE_OPEN_READ_FAILED``      ``TSCacheRead``                         TS_CACHE_ERROR code
``TS_EVENT_CACHE_OPEN_WRITE``            ``TSCacheWrite``                        Cache VC
``TS_EVENT_CACHE_OPEN_WRITE_FAILED``     ``TSCacheWrite``                        TS_CACHE_ERROR code
``TS_EVENT_CACHE_REMOVE``                ``TSCacheRemove``
``TS_EVENT_CACHE_REMOVE_FAILED``         ``TSCacheRemove``                       TS_CACHE_ERROR code
``TS_EVENT_NET_ACCEPT``                  ``TSNetAccept``                         ``NetVConnection``
                                         ``TSHttpTxnServerIntercept``
                                         ``TSHttpTxnIntercept``
``TS_EVENT_NET_ACCEPT_FAILED``           ``TSNetAccept``
                                         ``TSHttpTxnServerIntercept``
                                         ``TSHttpTxnIntercept``
``TS_EVENT_HOST_LOOKUP``                 ``TSHostLookup``                        ``TSHostLookupResult``
``TS_EVENT_TIMEOUT``                     ``TSContSchedule``
``TS_EVENT_ERROR``
``TS_EVENT_VCONN_READ_READY``            ``TSVConnRead``                         ``TSVConn``
``TS_EVENT_VCONN_WRITE_READY``           ``TSVConnWrite``                        ``TSVConn``
``TS_EVENT_VCONN_READ_COMPLETE``         ``TSVConnRead``                         ``TSVConn``
``TS_EVENT_VCONN_WRITE_COMPLETE``        ``TSVConnWrite``                        ``TSVConn``
``TS_EVENT_VCONN_EOS``                   ``TSVConnRead``                         ``TSVConn``
``TS_EVENT_NET_CONNECT``                 ``TSNetConnect``                        ``TSVConn``
``TS_EVENT_NET_CONNECT_FAILED``          ``TSNetConnect``                        ``TSVConn``
``TS_EVENT_HTTP_CONTINUE``
``TS_EVENT_HTTP_ERROR``
``TS_EVENT_MGMT_UPDATE``                 ``TSMgmtUpdateRegister``
======================================== ======================================= ======================

The continuation functions are listed below:

-  ``TSContCall``
-  ``TSContCreate``
-  ``TSContDataGet``
-  ``TSContDataSet``
-  ``TSContDestroy``
-  ``TSContMutexGet``
-  ``TSContSchedule``
