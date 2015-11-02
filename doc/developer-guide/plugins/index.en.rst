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

.. include:: ../../common.defs

.. _developer-plugins:

Plugin Development
******************

.. toctree::
   :maxdepth: 2

   introduction.en
   getting-started/index.en
   building-plugins.en
   configuration.en
   plugin-management/index.en
   actions/index.en
   hooks-and-transactions/index.en
   continuations/index.en
   mutexes.en
   io/index.en
   http-headers/index.en
   http-transformations/index.en
   new-protocol-plugins.en
   plugin-interfaces.en
   adding-statistics.en
   example-plugins/index.en

Traffic Server HTTP State Machine
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Traffic Server performs sophisticated HTTP caching and proxying.
Important features include checking for alternates and document
freshness, filtering, supporting cache hierarchies, and hosting. Traffic
Server handles thousands of client requests at a time and each request
is handled by an HTTP state machine. These machines follow a complex
state diagram that includes all of the states required to support
Traffic Server's features. The Traffic Server API provides hooks to a
subset of these states, chosen for their relevance to plugins. You can
view the API hooks and corresponding HTTP states in the
:ref:`http-txn-state-diagram`.

The example in this section (below) explains how a plugin typically
intervenes and extends Traffic Server's processing of an HTTP
transaction. Complete details about hooking on to Traffic Server
processes are provided in :ref:`developer-plugins-hooks-and-transactions`.

HTTP Transaction
^^^^^^^^^^^^^^^^

An HTTP transaction consists of a client request for a web document and
Traffic Server's response. The response could be the requested web
server content or it could be an error message. The content could come
from the Traffic Server cache or Traffic Server might fetch it from the
origin server. The following diagram shows some states in a typical
transaction - specifically, the scenario wherein content is served from
cache.

**Simplified HTTP Transaction**

.. _SimplifiedHTTPTransaction:

.. figure:: /static/images/sdk/transact75.jpg
   :alt: Simplified HTTP Transaction

   Simplified HTTP Transaction

In the diagram above, Traffic Server accepts the client connection,
reads the request headers, looks up the origin server's IP address, and
looks for the requested content in the cache. If the content is not in
the cache (a "miss"), then Traffic Server opens a connection to the
origin server and issues a request for the content. If the content is in
the cache (a "hit"), then Traffic Server checks it for freshness.

If the content is fresh, then Traffic Server sends a reply header to the
client. If the content is stale, then Traffic Server opens a connection
to the origin server and requests the content. The figure above,
:ref:`SimplifiedHTTPTransaction`, does *not*
show behavior in the event of an error. If there is an error at a any
stage, then the HTTP state machine jumps to the "send reply header"
state and sends a reply. If the reply is an error, then the transaction
closes. If the reply is not an error, then Traffic Server first sends
the response content before it closes the transaction.

**API Hooks Corresponding to States**

.. _APIHooksCorrespondingtoStates:

.. figure:: /static/images/sdk/transact_hook75.jpg
   :alt: API Hooks Corresponding to States Listed in

   API Hooks Corresponding to States Listed in

You use hooks as triggers to start your plugin. The name of a hook
reflects the Traffic Server state that was *just completed*. For
example, the "OS DNS lookup" hook wakes up a plugin right *after* the
origin server DNS lookup. For a plugin that requires the IP address of
the requested origin server, this hook is the right one to use. The
Blacklist plugin works in this manner, as shown in the :ref:`BlackListPlugin`
diagram below.

**Blacklist Plugin**

.. _BlackListPlugin:

.. figure:: /static/images/sdk/blacklist75.jpg
   :alt: Blacklist Plugin

   Blacklist Plugin

Traffic Server calls the Blacklist plugin right after the origin server
DNS lookup. The plugin checks the requested host against a list of
blacklisted servers; if the request is allowed, then the transaction
proceeds. If the host is forbidden, then the Blacklist plugin sends the
transaction into an error state. When the HTTP state machine gets to the
"send reply header" state, it then calls the Blacklist plugin to provide
the error message that's sent to the client.

Types of Hooks
^^^^^^^^^^^^^^

The Blacklist plugin's hook to the origin server DNS lookup state is a *global
hook*, meaning that the plugin is called every time there's an HTTP transaction
with a DNS lookup event. The plugin's hook to the send reply header state is a
*transaction hook*, meaning that this hook is only invoked for specified
transactions (in the :ref:`developer-plugins-examples-blacklist` example, it's
only used for requests to blacklisted servers). Several examples of setting up
hooks are provided in :ref:`developer-plugins-header-based-examples` and
:ref:`developer-plugins-http-transformations`.

*Header manipulation plugins*, such as filtering, basic authorization,
or redirects, usually have a global hook to the DNS lookup or the read
request header states. If specific actions need to be done to the
transaction further on, then the plugin adds itself to a transaction
hook. *Transformation plugins* require a global hook to check
all transactions for transformability followed by a transform hook,
which is a type of transaction hook used specifically for transforms.

