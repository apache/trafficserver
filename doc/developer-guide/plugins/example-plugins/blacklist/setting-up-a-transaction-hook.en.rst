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

.. _developer-plugins-examples-blacklist-txn-hook:

Setting Up a Transaction Hook
*****************************

The Blacklist plugin sends "access forbidden" messages to clients if
their requests are directed to blacklisted hosts. Therefore, the plugin
needs a transaction hook so it will be called back when Traffic Server's
HTTP state machine reaches the "send response header" event. In the
Blacklist plugin's ``handle_dns`` routine, the transaction hook is added
as follows:

.. code-block:: c

   TSMutexLock (sites_mutex);
   for (i = 0; i < nsites; i++) {
      if (strncmp (host, sites[i], host_length) == 0) {
         printf ("blacklisting site: %s\n", sites[i]);
         TSHttpTxnHookAdd (txnp,
            TS_HTTP_SEND_RESPONSE_HDR_HOOK,
            contp);
         TSHandleMLocRelease (bufp, hdr_loc, url_loc);
         TSHandleMLocRelease (bufp, TS_NULL_MLOC, hdr_loc);
         TSHttpTxnReenable (txnp, TS_EVENT_HTTP_ERROR);
         TSMutexUnlock (sites_mutex);
         return;
      }
   }
   TSMutexUnlock (sites_mutex);
   done:
   TSHttpTxnReenable (txnp, TS_EVENT_HTTP_CONTINUE);

This code fragment shows some interesting features. The plugin is
comparing the requested site to the list of blacklisted sites. While the
plugin is using the blacklist, it must acquire the mutex lock for the
blacklist to prevent configuration changes in the middle of a
blacklisting operation. If the requested site is blacklisted, then the
following things happen:

#. A transaction hook is added with ``TSHttpTxnHookAdd``; the plugin is
   called back at the "send response header" event (i.e., the plugin
   sends an Access forbidden message to the client). You can see that in
   order to add a transaction hook, you need a handle to the transaction
   being processed.

#. The transaction is reenabled using ``TSHttpTxnReenable`` with
   ``TS_EVENT_HTTP_ERROR`` as its event argument. Reenabling with an
   error event tells the HTTP state machine to stop the transaction and
   jump to the "send response header" state. Notice that if the
   requested site is not blacklisted, then the transaction is reenabled
   with the ``TS_EVENT_HTTP_CONTINUE`` event.

#. The string and ``TSMLoc`` data stored in the marshal buffer ``bufp`` is
   released by ``TSHandleMLocRelease`` (see
   :ref:`developer-plugins-http-headers-marshal-buffers`). Release these
   handles before re-enabling the transaction.

In general, whenever the plugin is doing something to a transaction, it
must reenable the transaction when it is finished. In other words: every
time your handler function handles a transaction event, it must call
``TSHttpTxnReenable`` when it is finished. Similarly, after your plugin
handles session events (``TS_EVENT_HTTP_SSN_START`` and
``TS_EVENT_HTTP_SSN_CLOSE``), it must reenable the session with
``TSHttpSsnReenable``. Reenabling the transaction twice in the same
plugin routine is a bad error.
