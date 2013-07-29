HTTP Hooks and Transactions
***************************

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

Hooks are points in Traffic Server transaction processing where plugins
can step in and do some work. Registering a plugin function for callback
amounts to "adding" the function to a hook. You can register your plugin
to be called back for every single transaction or only for specific
transactions.

This chapter contains the following sections:

.. toctree::
   :maxdepth: 2

   http-hooks-and-transactions/adding-hooks.en
   http-hooks-and-transactions/http-sessions.en
   http-hooks-and-transactions/http-transactions.en
   http-hooks-and-transactions/intercepting-http-transactions.en
   http-hooks-and-transactions/initiate-http-connection.en
   http-hooks-and-transactions/http-alternate-selection.en

The Set of Hooks
----------------

To understand hooks and transactions, you should be familiar with the
following terminology:

***HTTP Transaction***

A **transaction** consists of a single HTTP request from a client and
the response Traffic Server sends to that client. Thus, a transaction
begins when Traffic Server receives a request and ends when Traffic
Server sends the response.

Traffic Server uses **HTTP state machines** to process transactions. The
state machines follow a complex set of states involved in sophisticated
caching and document retrieval (taking into account, for example,
alternate selection, freshness criteria, and hierarchical caching). The
Traffic Server API provides hooks to a subset of these states, as
illustrated in the `HTTP Transaction State
Diagram <#HHTTPTxStateDiag>`__ below.

***Transform hooks***

The two **transform hooks**, ``TS_HTTP_REQUEST_TRANSFORM_HOOK`` and
``TS_HTTP_RESPONSE_TRANSFORM_HOOK``, are called in the course of an HTTP
transform. To see where in the HTTP transaction they are called, look
for the "set up transform" ovals in the `HTTP Transaction State
Diagram <#HHTTPTxStateDiag>`__ below.

***HTTP session***

A **session** consists of a single client connection to Traffic Server;
it may consist of a single transaction or several transactions in
succession. The session starts when the client connection opens and ends
when the connection closes.

**HTTP Transaction State Diagram (*not yet updated*)**
{#HHTTPTxStateDiag}

.. figure:: /static/images/sdk/http_state2.jpg
   :alt: HTTP Transaction State Diagram

   HTTP Transaction State Diagram


