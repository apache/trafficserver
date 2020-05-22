.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. include:: ../../../common.defs

.. default-domain:: c

TSAcceptor
**********

Traffic Server API's related to Accept objects

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSAcceptor TSAcceptorGet(TSVConn sslp)
.. function:: TSAcceptor TSAcceptorGetbyID(int id)
.. function:: int TSAcceptorIDGet(TSAcceptor acceptor)
.. function:: int TSAcceptorCount()


Description
===========

Traffic Server allows plugins to get information from an accept object that created a certain TSVConn object using the above mentioned APIs.
An acceptor thread listens for incoming connections and creates the virtual connection (:type:`TSVConn`) for each accepted connection.

:func:`TSAcceptorGet` returns :type:`TSAcceptor` object that created :arg:`sslp`.

:func:`TSAcceptorGetbyID` returns the :type:`TSAcceptor` object identified by :arg:`id`. :type:`TSAcceptor` represents the acceptor object created by the core
traffic server.

:func:`TSAcceptorIDGet` returns the Integer number that identifies :arg:`acceptor`. All the cloned :type:`TSAcceptor` objects will have the same identifying number.

:func:`TSAcceptorCount` returns the number of :type:`TSAcceptor` objects created by the server.
