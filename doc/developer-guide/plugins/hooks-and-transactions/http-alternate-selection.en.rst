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

.. _developer-plugins-hooks-alternate-selection:

HTTP Alternate Selection
************************

The HTTP alternate selection functions provide a mechanism for hooking
into Traffic Server's alternate selection mechanism and augmenting it
with additional information. **HTTP alternate selection** refers to the
process of choosing between several alternate versions of a document for
a specific URL. Alternates arise because the HTTP 1.1 specification
allows different documents to be sent back for the same URL (depending
on the clients request). For example, a server might send back a GIF
image to a client that only accepts GIF images, and might send back a
JPEG image to a client that only accepts JPEG images.

The alternate selection mechanism is invoked when Traffic Server looks
up a URL in its cache. For each URL, Traffic Server stores a vector of
alternates. For each alternate in this vector, Traffic Server computes a
quality value between 0 and 1 that represents how "good" the alternate
is. A quality value of 0 means that the alternate is unacceptable; a
value of 1 means that the alternate is a perfect match.

If a plugin hooks onto the ``TS_HTTP_SELECT_ALT_HOOK``, then it will be
called back when Traffic Server performs alternate selection. You cannot
register locally to the hook ``TS_HTTP_SELECT_ALT_HOOK`` by using
``TSHttpTxnHookAdd`` - you can only do so by using only
``TSHttpHookAdd``. Since Traffic Server does not actually have an HTTP
transaction or an HTTP session on hand when alternate selection is
performed, it is only valid to hook onto the global list of
``TS_HTTP_SELECT_ALT_HOOK``. Traffic Server calls each of the select
alternate hooks with the ``TS_EVENT_HTTP_SELECT_ALT`` event. The
``void *edata`` argument that is passed to the continuation is a pointer
to an ``TSHttpAltInfo`` structure. It can be used later to call the HTTP
alternate selection functions listed at the end of this section. Unlike
other hooks, this alternate selection callout is non-blocking; the
expectation is that the quality value for the alternate will be changed
by a call to ``TSHttpAltInfoQualitySet``.

.. note::

   HTTP SM does not have to be reenabled using ``TSHttpTxnReenable`` or any
   other APIs; just return from the function.

The sample code below shows how to call the alternate APIs.

.. code-block:: c

   static void handle_select_alt(TSHttpAltInfo infop)
   {
      TSMBuffer client_req_buf, cache_resp_buf;
      TSMLoc client_req_hdr, cache_resp_hdr;
      
      TSMLoc accept_transform_field;
      TSMLoc content_transform_field;
      
      int accept_transform_len = -1, content_transform_len = -1;
      const char* accept_transform_value = NULL;
      const char* content_transform_value = NULL;
      int content_plugin, accept_plugin;
      
      float quality;
      
      /* get client request, cached request and cached response */
      TSHttpAltInfoClientReqGet (infop, &client_req_buf, &client_req_hdr);
      TSHttpAltInfoCachedRespGet(infop, &cache_resp_buf, &cache_resp_hdr);
      
      /* get the Accept-Transform field value from the client request */
      accept_transform_field = TSMimeHdrFieldFind(client_req_buf,
         client_req_hdr, "Accept-Transform", -1);
      if (accept_transform_field) {
         TSMimeHdrFieldValueStringGet(client_req_buf, client_req_hdr,
            accept_transform_field, 0, &accept_transform_value, &accept_transform_len);
         TSDebug(DBG_TAG, "Accept-Transform = |%s|",
            accept_transform_value);
      }
       
      /* get the Content-Transform field value from cached server response */
      content_transform_field = TSMimeHdrFieldFind(cache_resp_buf,
         cache_resp_hdr, "Content-Transform", -1);
      if (content_transform_field) {
         TSMimeHdrFieldValueStringGet(cache_resp_buf, cache_resp_hdr,
            content_transform_field, 0, &content_transform_value, &content_transform_len);
         TSDebug(DBG_TAG, "Content-Transform = |%s|",
            content_transform_value);
      }
       
      /* compute quality */
      accept_plugin = (accept_transform_value && (accept_transform_len > 0) &&
         (strncmp(accept_transform_value, "plugin",
            accept_transform_len) == 0));
      
      content_plugin = (content_transform_value && (content_transform_len >0) &&
         (strncmp(content_transform_value, "plugin",
            content_transform_len) == 0));
      
      if (accept_plugin) {
         quality = content_plugin ? 1.0 : 0.0;
      } else {
         quality = content_plugin ? 0.0 : 0.5;
      }
      
      TSDebug(DBG_TAG, "Setting quality to %3.1f", quality);
       
      /* set quality for this alternate */
      TSHttpAltInfoQualitySet(infop, quality);
       
      /* cleanup */
      if (accept_transform_field)
         TSHandleMLocRelease(client_req_buf, client_req_hdr,
            accept_transform_field);
      TSHandleMLocRelease(client_req_buf, TS_NULL_MLOC, client_req_hdr);
      
      if (content_transform_field)
         TSHandleMLocRelease(cache_resp_buf, cache_resp_hdr,
            content_transform_field);
      TSHandleMLocRelease(cache_resp_buf, TS_NULL_MLOC, cache_resp_hdr);
   }
       
   static int alt_plugin(TSCont contp, TSEvent event, void *edata)
   {
      TSHttpAltInfo infop;
      
      switch (event) {
         case TS_EVENT_HTTP_SELECT_ALT:
            infop = (TSHttpAltInfo)edata;
            handle_select_alt(infop);
            break;

         default:
            break;
      }
      
      return 0;
   }
       
   void TSPluginInit (int argc, const char *argv[])
   {
      TSHttpHookAdd(TS_HTTP_SELECT_ALT_HOOK, TSContCreate (alt_plugin,
         NULL));
   }

Traffic Server augments the alternate selection through these callouts
using the following algorithm:

1. Traffic Server computes its own quality value for the alternate,
   taking into account the quality of the accept match, the encoding
   match, and the language match.

2. Traffic Server then calls out each of the continuations on the global
   ``TS_HTTP_SELECT_ALT_HOOK``'s list.

3. It multiplies its quality value with the value returned by each
   callout. Since all of the values are clamped to be between 0 and 1,
   the final value will be between 0 and 1 as well.

4. This algorithm also ensures that a single callout can block the usage
   of a given alternate by specifying a quality value of 0.

A common usage for the alternate selection mechanism is when a plugin
transforms a document for some clients and not for others, but wants to
store both the transformed and unchanged document. The client's request
will specify whether it accepted the transformed document. The plugin
will then determine if the alternate matches this specification and then
set the appropriate quality level for the alternate.

The HTTP alternate selection functions are:

-  `TSHttpAltInfoCachedReqGet <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#af4f3a56716e3e97afd582c7fdb14bcb7>`_

-  `TSHttpAltInfoCachedRespGet <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#aff5861ae4a4a7a6ce7b2d669c113b3bb>`_

-  `TSHttpAltInfoClientReqGet <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a74d494c6442b6012d8385e92f0e14dee>`_

-  `TSHttpAltInfoQualitySet <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a978b7160a048491d5698e0f4c0c79aad>`_
