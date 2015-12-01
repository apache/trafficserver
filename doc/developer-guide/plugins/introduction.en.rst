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

.. _developer-plugins-introduction:

Plugin Development Introduction
*******************************

.. toctree::
   :maxdepth: 1

This chapter provides a foundation for designing and writing plugins.
Reading this chapter will help you to understand:

-  The asynchronous event mode. This is the design paradigm used
   throughout Traffic Server; plugins must also follow this design. It
   includes the callback mechanism for Traffic Server to "wake up" your
   plugin and put it to work.

-  Traffic Server's HTTP processing, with an overview of the HTTP state
   machine.

-  How plugins can hook onto and modify/extend Traffic Server's HTTP
   processing.

-  A :ref:`developer-plugins-roadmap` with an overview of the functionality
   provided by the Traffic Server API.

.. _developer-plugins-roadmap:

Roadmap
=======

This chapter has provided an overview of Traffic Server's HTTP
processing, API hooks, and the asynchronous event model. Next, you must
understand the capabilities of Traffic Server API functions. These are
quite broad:

-  **HTTP header manipulation functions**

   Obtain information about and manipulate HTTP headers, URLs, & MIME
   headers.

-  **HTTP transaction functions**

   Get information about and modify HTTP transactions (for example: get
   the client IP associated to the transaction; get the server IP; get
   parent proxy information)

-  **IO functions**

   Manipulate vconnections (virtual connections, used for network and
   disk I/O)

-  **Network connection functions**

   Open connections to remote servers.

-  **Statistics functions**

   Define and compute statistics for your plugin's activity.

-  **Traffic Server management functions**

   Obtain values for Traffic Server configuration and statistics
   variables.

Below are some guidelines for creating a plugin:

#. Decide what you want your plugin to do, based on the capabilities of
   the API and |TS|. The two kinds of example plugins
   provided with this SDK are HTTP-based (includes header-based and
   response transform plugins), and non-HTTP-based (a protocol plugin).
   These examples are discussed in the following chapters.

#. Determine where your plugin needs to hook on to Traffic Server's HTTP
   processing (view the :ref:`http-txn-state-diagram`.

#. Read :ref:`developer-plugins-header-based-examples` to learn the basics of
   writing plugins: creating continuations and setting up hooks. If you
   want to write a plugin that transforms data, then read
   :ref:`developer-plugins-http-transformations`.

#. Figure out what parts of the Traffic Server API you need to use and
   then read about the details of those APIs in this manual's reference
   chapters.

#. Compile and load your plugin (see :ref:`developer-plugins-getting-started`.

#. Depending on your plugin's functionality, you might start testing it
   by issuing requests by hand and checking for the desired behavior in
   Traffic Server log files. See the ***Traffic Server Administrator's
   Guide*** for information about Traffic Server logs.

Asynchronous Event Model
========================

Traffic Server is a multi-threaded process. There are two main reasons
why a server might use multiple threads:

-  To take advantage of the concurrency available with multiple CPUs and
   multiple I/O devices.

-  To manage concurrency from having many simultaneous client
   connections. For example, a server could create one thread for each
   connection, allowing the operating system (OS) to control switching
   between threads.

Traffic Server uses multiple threads for the first reason. However,
Traffic Server does not use a separate OS thread per transaction because
it would not be efficient when handling thousands of simultaneous
connections.

Instead, Traffic Server provides special event-driven mechanisms for
efficiently scheduling work: the event system and continuations. The
**event system** is used to schedule work to be done on threads. A
**continuation** is a passive, event-driven state machine that can do
some work until it reaches a waiting point; it then sleeps until it
receives notification that conditions are right for doing more work. For
example, HTTP state machines (which handle HTTP transactions) are
implemented as continuations.

Continuation objects are used throughout Traffic Server. Some might live
for the duration of the Traffic Server process, while others are created
(perhaps by other continuations) for specific needs and then destroyed.
:ref:`TSInternals` (below) shows how the major
components of Traffic Server interact. Traffic Server has several
**processors**, such as *cache processor* and *net processor*, that
consolidate cache or network I/O tasks. Processors talk to the event
system and schedule work on threads. An executing thread calls back a
continuation by sending it an event. When a continuation receives an
event, it wakes up, does some work, and either destroys itself or goes
back to sleep & waits for the next event.

**Traffic Server Internals**

.. _TSInternals:

.. figure:: /static/images/sdk/event_sys80.jpg
   :alt: Traffic Server Internals

   Traffic Server Internals

Plugins are typically implemented as continuations. All of the sample
code plugins (except ``hello-world``) are continuations that are created
when Traffic Server starts up; they then wait for events that trigger
them into activity.

**Traffic Server with Plugins**

.. _TSwithPlugins:

.. figure:: /static/images/sdk/evt_plugin120.jpg
   :alt: Traffic Server with Plugins

   Traffic Server with Plugins

A plugin may consist of just one static continuation that is called
whenever certain events happen. Examples of such plugins include
``blacklist-1.c``, ``basic-auth.c``, and ``redirect-1.c``.
Alternatively, a plugin might dynamically create other continuations as
needed. Transform plugins are built in this manner: a static parent
continuation checks all transactions to see if any are transformable;
when a transaction is transformable, the static continuation creates a
type of continuation called a **vconnection**. The vconnection lives as
long as it takes to complete the transform and then destroys itself.
This design can be seen in all of the sample transform plugins. Plugins
that support new protocols also have this architecture: a static
continuation listens for incoming client connections and then creates
transaction state machines to handle each protocol transaction.

When you write plugins, there are several ways to send events to
continuations. For HTTP plugins, there is a "hook" mechanism that
enables the Traffic Server HTTP state machine to send your plugin wakeup
calls when needed. Additionally, several Traffic Server API functions
trigger Traffic Server sub-processes to send events to plugins:
``TSContCall``, ``TSVConnRead``, ``TSCacheWrite``, and
``TSMgmtUpdateRegister``, to name a few.

Naming Conventions
==================

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
:ref:`SimplifiedHTTPTransaction`, does not
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
all transactions for transformability followed by a *transform hook*,
which is a type of transaction hook used specifically for transforms.

