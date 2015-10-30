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

.. include:: ../../../../common.defs

.. _developer-plugins-examples-blacklist:

Blacklist Plugin
****************

The sample blacklisting plugin included in the Traffic Server SDK is
``blacklist-1.c``. This plugin checks every incoming HTTP client request
against a list of blacklisted web sites. If the client requests a
blacklisted site, then the plugin returns an ``Access forbidden``
message to the client.

The flow of HTTP processing with the blacklist plugin is illustrated in
the figure titled :ref:`BlackListPlugin`.
This example also contains a simple configuration management interface.
It can read a list of blacklisted sites from a file (``blacklist.txt``)
that can be updated by a Traffic Server administrator. When the
configuration file is updated, Traffic Server sends an event to the
plugin that wakes it up to do some work.

Creating the Parent Continuation
================================

You create the static parent continuation in the mandatory
``TSPluginInit`` function. This parent continuation effectively **is**
the plugin: the plugin executes only when this continuation receives an
event from Traffic Server. Traffic Server passes the event as an
argument to the continuation's handler function. When you create
continuations, you must create and specify their handler functions.

You can specify an optional mutex lock when you create continuations.
The mutex lock protects data shared by asynchronous processes. Because
Traffic Server has a multi-threaded design, race conditions can occur if
several threads try to access the same continuation's data.

Here is how the static parent continuation is created in
``blacklist-1.c``:

.. code-block:: c

   void
   TSPluginInit (int argc, const char *argv[])
   {
      // ...
      TSCont contp;
          
      contp = TSContCreate (blacklist_plugin, NULL);
      // ...
   }

The handler function for the plugin is ``blacklist_plugin``, and the
mutex is null. The continuation handler function's job is to handle the
events that are sent to it; accordingly, the ``blacklist_plugin``
routine consists of a switch statement that covers each of the events
that might be sent to it:

.. code-block:: c

   static int
   blacklist_plugin (TSCont contp, TSEvent event, void *edata)
   {
      TSHttpTxn txnp = (TSHttpTxn) edata;
      switch (event) {
         case TS_EVENT_HTTP_OS_DNS:
            handle_dns (txnp, contp);
            return 0;
         case TS_EVENT_HTTP_SEND_RESPONSE_HDR:
            handle_response (txnp);
            return 0;
         default:
            TSDebug ("blacklist_plugin", "This event was unexpected: %d\n", );
            break;
      }
      return 0;
   }

When you write handler functions, you have to anticipate any events that
might be sent to the handler by hooks or by other functions. In the
Blacklist plugin, ``TS_EVENT_OS_DNS`` is sent because of the global hook
established in ``TSPluginInit``, ``TS_EVENT_HTTP_SEND_RESPONSE_HDR`` is
sent because the plugin contains a transaction hook
(see :ref:`developer-plugins-examples-blacklist-txn-hook`).
It is good practice to have a default case in your switch statements.

.. toctree::
   :maxdepth: 2

   setting-a-global-hook.en
   accessing-the-transaction-being-processed.en
   setting-up-a-transaction-hook.en
   working-with-http-header-functions.en
   source-code.en

