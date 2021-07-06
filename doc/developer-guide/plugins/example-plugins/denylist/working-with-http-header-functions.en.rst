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

.. _developer-plugins-examples-denylist-http-header-functions:

Working with HTTP Header Functions
**********************************

The Denylist plugin examines the host header in every client
transaction. This is done in the ``handle_dns`` routine, using
``TSHttpTxnClientReqGet``, ``TSHttpHdrUrlGet``, and ``TSUrlHostGet``.

.. code-block:: c

   static void
   handle_dns (TSHttpTxn txnp, TSCont contp)
   {
   TSMBuffer bufp;
   TSMLoc hdr_loc;
   TSMLoc url_loc;
   const char *host;
   int i;
   int host_length;

   if (TSHttpTxnClientReqGet(txnp, &bufp, &hdr_loc) != TS_SUCCESS) {
      TSError("[denylist] Couldn't retrieve client request header");
      goto done;
   }

   if (TSHttpHdrUrlGet(bufp, hdr_loc, &url_loc) != TS_SUCCESS) {
      TSError("[denylist] Couldn't retrieve request url");
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      goto done;
   }

   host = TSUrlHostGet(bufp, url_loc, &host_length);
   if (!host) {
      TSError("[denylist] couldn't retrieve request hostname");
      TSHandleMLocRelease(bufp, hdr_loc, url_loc);
      TSHandleMLocRelease(bufp, TS_NULL_MLOC, hdr_loc);
      goto done;
   }

To access the host header, the plugin must first get the client request,
retrieve the URL portion, and then obtain the host header. See
:ref:`developer-plugins-http-headers` for more information about these calls.
See :ref:`developer-plugins-http-headers-marshal-buffers`
for guidelines on using ``TSHandleMLocRelease``.
