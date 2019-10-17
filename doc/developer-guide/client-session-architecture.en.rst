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

.. include:: ../common.defs

.. _client-session-architecture:

An Overview Client Sessions and Transactions
********************************************


The User Agent interacts with ATS by creating a session with the ATS server and
submitting sequences of requests over the session. ATS supports several session protocols including
HTTP/1.x and HTTP/2. A HTTP State Machine is created for each request to process the request.

ATS uses the generic classes ProxySession and ProxyTransaction to hide the details of
the underlying protocols from the HTTP State Machine.

Classes
=======

ProxySession
------------------

.. figure:: /static/images/sessions/session_hierarchy.png
   :align: center
   :alt: ProxySession hierarchy

The ProxySession class abstracts the key features of a client session.  It contains zero or more ProxyTransaction objects.  It also has a reference to the associated NetVC (either UnixNetVConnection or SSLNetVConnection).  The session class is responsible for interfacing with the user agent protocol.

At this point there are two concrete subclasses: Http1ClientSession and Http2ClientSession.  The Http1ClientSession
only has at most one transaction active at a time.  The HTTP/2 protocol allows for multiple simultaneous active
transactions

ProxyTransaction
----------------------

.. figure:: /static/images/sessions/transaction_hierarchy.png
   :align: center
   :alt: ProxyTransaction hierarchy

The ProxyTransaction class abstracts the key features of a client transaction.  It has a reference to its
parent ProxySession.  One HttpSM is created for each ProxyTransaction.

There are two concrete subclasses: Http1Transaction and Http2Stream.

Session Object Relationships
============================

HTTP/1.x Objects
----------------

.. figure:: /static/images/sessions/http1_session_objects.png
   :align: center
   :alt: HTTP1 session objects

This diagram shows the relationships between objects created as part of a HTTP/1.x session.  A NetVC
object performs the basic network level protocols.  The Http1ClientSession object has a reference to the
associated NetVC object.  The NetVC object is available via the :code:`ProxySession::get_netvc()` method.

The Http1ClientSession object contains a Http1Transaction object.  For each HTTP request, it calls
the :code:`ProxySession::new_transaction()` method to instantiate the Http1Transaction object.  With the HTTP/1.x
protocol at most one transaction can be active at a time.

When the Http1Transaction object is instantiated via :code:`ProxyTransaction::new_transaction()` it allocates a
new HttpSM object, initializes it, and calls :code:`HttpSM::attach_client_session()` to associate the
Http1Transaction object with the new HttpSM.

The ProxyTransaction object refers to the HttpSM via the _sm member variable.  The HttpSM object
refers to ProxyTransaction via the ua_session member variable (session in the member name is
historical because the HttpSM used to refer directly to the ClientSession object).

HTTP/2 Objects
--------------

.. figure:: /static/images/sessions/http2_session_objects.png
   :align: center
   :alt: HTTP/2 session objects

This diagram shows the relationships between objects created as part of a HTTP/2 session.  It is very similar
to the HTTP/1.x case.  The Http2ClientSession object interacts with the NetVC.  The Http2Stream object creates
a HttpSM object object when :code:`ProxyClient::new_transaction()` is called.

One difference is that the Http/2 protocol allows for multiple simultaneous transactions, so the Http2ClientSession
object must be able to manage multiple streams. From the HttpSM perspective it is interacting with a
ProxyTransaction object, and there is no difference between working with a Http2Stream and a Http1Transaction.

Transaction and Session Shutdown
================================

One of the trickiest bits of managing sessions and transactions is cleaning things up accurately and in a timely fashion.
In addition, the TXN_CLOSE hooks and SSN_CLOSE hooks must be executed accurately.  The TXN_CLOSE hooks must be
executed exactly once.  The SSN_CLOSE hook must also be executed exactly once and only after all the TXN_CLOSE
hooks of all the child transactions have been executed.  The CLOSE hooks are important for many plugins to ensure that
plugin allocated objects are appropriately reclaimed.

If objects are not cleaned up, memory leaks will occurs.  If objects are reclaimed too soon, delayed events may
cause use-after-free and other related memory corruption errors.

To ensure that sessions and transactions are correctly shutdown the following assertions are maintained.

* The Session object will not call :code:`::destroy()` on itself until all child transaction objects are fully shutdown (i.e. TXN_CLOSE hooks are called and the transaction objects have been freed).
* The Transaction object will not call :code:`::destroy()` on itself until the associated HttpSM has been shutdown.  In :code:`HttpSM::kill_this()`, the HttpSM will call :code:`ProxyTransaction::transaction_done()` on the ua_session object.  If the user agent initiates the termination, the ProxyTransaction object will send a WRITE_COMPLETE, EOS, or ERROR event on the open VIO object.  This should signal to the HttpSM object to shut itself down.

