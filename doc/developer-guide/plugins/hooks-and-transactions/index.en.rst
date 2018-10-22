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

.. _developer-plugins-hooks-and-transactions:

Hooks and Transactions
**********************

Hooks are points in Traffic Server transaction processing where plugins
can step in and do some work. Registering a plugin function for callback
amounts to "adding" the function to a hook. You can register your plugin
to be called back for every single transaction or only for specific
transactions.

This chapter contains the following sections:

.. toctree::
   :maxdepth: 2

   adding-hooks.en
   http-sessions.en
   http-transactions.en
   intercepting-http-transactions.en
   initiate-http-connection.en
   http-alternate-selection.en
   ssl-hooks.en
   ssl-session-api.en

.. _developer-plugins-hooks:

Hooks
=====

To understand hooks and transactions, you should be familiar with the
following terminology:

HTTP Transaction
----------------

A **transaction** consists of a single HTTP request from a client and
the response Traffic Server sends to that client. Thus, a transaction
begins when Traffic Server receives a request and ends when Traffic
Server sends the response.

Traffic Server uses **HTTP state machines** to process transactions. The
state machines follow a complex set of states involved in sophisticated
caching and document retrieval (taking into account, for example,
alternate selection, freshness criteria, and hierarchical caching). The
Traffic Server API provides hooks to a subset of these states, as
illustrated in the :ref:`http-txn-state-diagram` below.

Transform Hooks
---------------

The two **transform hooks**, ``TS_HTTP_REQUEST_TRANSFORM_HOOK`` and
``TS_HTTP_RESPONSE_TRANSFORM_HOOK``, are called in the course of an HTTP
transform. To see where in the HTTP transaction they are called, look
for the "set up transform" ovals in the :ref:`http-txn-state-diagram` below.

HTTP Session
------------

A **session** consists of a single client connection to Traffic Server;
it may consist of a single transaction or several transactions in
succession. The session starts when the client connection opens and ends
when the connection closes.

.. _http-txn-state-diagram:

HTTP Transaction State Diagram
------------------------------

.. graphviz::
   :alt: HTTP Transaction State Diagram

   digraph http_txn_state_diagram{
     accept -> TS_HTTP_TXN_START_HOOK;
     TS_HTTP_TXN_START_HOOK -> "read req hdrs";
     "read req hdrs" -> TS_HTTP_READ_REQUEST_HDR_HOOK;
     TS_HTTP_READ_REQUEST_HDR_HOOK -> TS_HTTP_PRE_REMAP_HOOK;
     TS_HTTP_PRE_REMAP_HOOK -> "remap request";
     "remap request" -> TS_HTTP_POST_REMAP_HOOK;
     TS_HTTP_POST_REMAP_HOOK -> "cache lookup";
     "cache lookup" -> DNS [label = "miss"];
     DNS -> TS_HTTP_OS_DNS_HOOK;
     TS_HTTP_OS_DNS_HOOK -> TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK;
     "cache lookup" -> TS_HTTP_SELECT_ALT_HOOK [label = "hit"];
     TS_HTTP_SELECT_ALT_HOOK -> "cache match";
     "cache match" -> TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK [label="no match"];
     "cache match" -> TS_HTTP_READ_CACHE_HDR_HOOK [label = "cache fresh"];
     TS_HTTP_READ_CACHE_HDR_HOOK -> "cache fresh";
     "cache fresh" -> TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK;
     TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK -> "lock URL in cache" [label = "miss"];
     TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK -> "lock URL in cache" [label = "no match  "];
     TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK -> "lock URL in cache" [label = "stale"];
     TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK -> "send cached hdrs" [label = "fresh"];
     "send cached hdrs" -> "set up transform";
     "lock URL in cache" -> "pick address";
     "pick address" -> "try connect" [label = "       "];
     "try connect" -> "pick address" [label = "fail"];
     "try connect" -> TS_SSL_VERIFY_SERVER_HOOK [label = "HTTPS connection"];
     TS_SSL_VERIFY_SERVER_HOOK -> TS_HTTP_SEND_REQUEST_HDR_HOOK [label = "success"];
     TS_SSL_VERIFY_SERVER_HOOK -> "SSL Handshake Error" [label = "fail"];
     "try connect" -> TS_HTTP_SEND_REQUEST_HDR_HOOK [label = "success"];
     TS_HTTP_SEND_REQUEST_HDR_HOOK -> "send req hdrs";
     "send req hdrs" -> "set up POST/PUT read" [label = "POST/PUT"];
     "send req hdrs" -> "read reply hdrs" [label = "GET"];
     "set up POST/PUT read" -> "set up req transform";
     "set up req transform" -> "tunnel req body";
     "tunnel req body" -> "read reply hdrs";
     "read reply hdrs" -> TS_HTTP_READ_RESPONSE_HDR_HOOK;
     TS_HTTP_READ_RESPONSE_HDR_HOOK -> "check valid";
     "check valid" -> "setup server read" [label = "yes"];
     "check valid" -> "pick address" [label = "no"];
     "setup server read" -> "set up cache write" [label = "cacheable"];
     "setup server read" -> "set up transform" [label = "uncacheable"];
     "set up cache write" -> "set up transform";
     "set up transform" -> TS_HTTP_SEND_RESPONSE_HDR_HOOK;
     TS_HTTP_SEND_RESPONSE_HDR_HOOK -> "send reply hdrs";
     "send reply hdrs" -> "tunnel response";
     "tunnel response" -> TS_HTTP_TXN_CLOSE_HOOK;
     TS_HTTP_TXN_CLOSE_HOOK -> accept;

     TS_HTTP_TXN_START_HOOK [shape=box];
     TS_HTTP_READ_REQUEST_HDR_HOOK [shape = box];
     TS_HTTP_PRE_REMAP_HOOK [shape = box];
     TS_HTTP_POST_REMAP_HOOK [shape = box];
     TS_HTTP_OS_DNS_HOOK [shape = box];
     TS_HTTP_CACHE_LOOKUP_COMPLETE_HOOK[shape = box];
     TS_HTTP_SELECT_ALT_HOOK [shape = box];
     TS_HTTP_READ_CACHE_HDR_HOOK [shape = box];
     TS_SSL_VERIFY_SERVER_HOOK [shape = box];
     TS_SSL_VERIFY_SERVER_HOOK [tooltip = "verify server certificate"];
     TS_HTTP_SEND_REQUEST_HDR_HOOK [shape = box];
     "set up req transform" [tooltip = "req transform takes place here"];
     TS_HTTP_READ_RESPONSE_HDR_HOOK [shape = box];
     "set up transform" [tooltip = "response transform takes place here"];
     TS_HTTP_SEND_RESPONSE_HDR_HOOK [shape = box];
     TS_HTTP_TXN_CLOSE_HOOK [shape = box];
   }

HTTP Transacation Timers
------------------------

For an overview of HTTP transaction timers, refer to the transaction timer diagram
below.

.. toctree::
  :maxdepth: 1

  Transaction Timers: Trafficserver Timers at different states <trafficserver-timers.en>

