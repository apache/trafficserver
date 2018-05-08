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

.. highlight:: cpp
.. default-domain:: cpp

.. _developer-doc-rpc:

.. |TServer| replace:: :program:`traffic_server`
.. |TManager| replace:: :program:`traffic_manager`
.. |TCtl| replace:: :program:`traffic_ctl`
.. |LM| replace:: :class:`LocalManager`
.. |PM| replace:: :class:`ProcessManager`

RPC
***

========
Overview
========

In order for programs in different address spaces (remote clients and |TManager|) to communicate with |TServer|, there is a RPC mechanism in :ts:git:`mgmt/`. 

This is a simple serialization style RPC which runs over unix domain sockets. The following sections will detail the runtime structure, serialization mechanisms and how messages are passed from remote clients to |TServer|.


=================
Runtime Structure
=================

.. uml::
   :align: center

   hide empty members

   node "traffic_manager"
   node "traffic_server"

   [traffic_ctl] <-d-> [traffic_manager] : Remote RPC
   [other remote clients] <-u-> [traffic_manager] : Remote RPC
   [traffic_manager] <-r-> [traffic_server] : Local RPC
   [traffic_server] <-r-> [plugin] : Hook

|TManager| opens a unix domain socket to recieve commands from remote clients. |TManager| also has a unix domain socket connection with |TServer| to communicate.

===============
Message Passing
===============

Sequence diagram for a command sent from |TCtl| to when it is recieved by a plugin. 

.. image:: ../../uml/images/RPC-sequence-diagram.svg
    
.. note::

    Currently a fire and forget model. traffic_manager sends out an asynchoronous signal without any acknowledgement. It then proceeds to send a response itself. 

=======================
Remote RPC vs Local RPC 
=======================

The RPC API for remote clients, such as |TCtl|, etc, is different from the RPC API used between |TManager| and |TServer|. 

|TManager| acts like a bridge for remote clients to interact with |TServer|. Thus, it is currently impossible to do things like have |TCtl| directly send messages to |TServer|. Classes suffixed with "Remote", ie. :ts:git:`CoreAPIRemote.cc`, and classes suffixed with "Local", ie. :ts:git:`NetworkUtilsLocal.cc` are for remote and local clients, respectively. The following sections will note which set of RPC's are relevant.

=======================
Serialization Mechanism
=======================

.. class:: MgmtMarshall

   This is the class used to marshall data objects. It provides functions to marshall and unmarshall data. Each data object is associated with a field. Fields are of :type:`MgmtMarshallType`:

    - **MGMT_MARSHALL_INT** : 4 bytes.
    - **MGMT_MARSHALL_LONG** : 8 bytes.
    - **MGMT_MARSHALL_STRING** : 4 bytes to indicate the string size in bytes, followed by the entire string and NULL terminator. 
    - **MGMT_MARSHALL_DATA** : 4 byt es to indicate data size in bytes, followed by the entire data object. 

When data is actually sent over a connection it must first be seralized. This is the general serialization mechanism for RPC communcation. 

Generally, for remote clients sending messages to |TServer|, the message is serialized using remote RPC APIs. The serialized message is sent to |TManager| and |TManager| then simply relays the message to |TServer|. |TServer| eventually unserializes the message. 

Details for how |TManager| and |TServer| communicate are documented in the next section.

Marshalling:
============

   .. function:: ssize_t mgmt_message_marshall(void *buf, size_t remain, const MgmtMarshallType *fields, unsigned count, ...)

      Variable argument wrapper for ``mgmt_message_marshall_v``. Allows for different datatypes to be marshalled together as long as a field is specified for each data object. Arguments should be references to objects to be marshalled. 

   .. function:: ssize_t mgmt_message_marshall_v(void *buf, size_t remain, const MgmtMarshallType *fields, unsigned count, va_list ap)

      This function goes through all the data objects and serializes them together into a buffer. Based on the field, the number of bytes is determined and if there is enough space, it is written into the buffer. 

Unmarshalling:
==============

   .. function:: ssize_t mgmt_message_parse(const void *buf, size_t len, const MgmtMarshallType *fields, unsigned count, ...)

      Variable argument wrapper for ``mgmt_message_parse_v``. Reference to data object to store unmarshalled message needed for variable arguements. 

   .. function:: ssize_t mgmt_message_parse_v(const void *buf, size_t len, const MgmtMarshallType *fields, unsigned count, va_list ap)
   
      This function parses all the serialized. Based on the field, the number of bytes to be read is determined and copied into a ``MgmtMarshallAnyPtr``.

===================
Local Serialization
===================

A RPC message is sent as a :class:`MgmtMessageHdr` followed by the serialized data in bytes. Serialization is very simple as the inteface for communication between |TManager| and |TServer| only allows for :class:`MgmtMessageHdr`, which is a fixed size, and raw data in the form of :code:`char*` to be sent. A header specifies a :arg:`msg_id` and the :arg:`data_len`. On a read, the header is *always* first read. Based on the length of the data payload, the correct number of bytes is then read from the socket. On a write, the header is first populated and sent on the socket, followed by the raw data.

.. class:: MgmtMessageHdr

   .. member:: int msg_id 

      ID for the event or signal to be sent.

   .. member:: int data_len

      Length in bytes of the message. 

.. uml::

   |Write|
   start
   :populate msg_id;
   :populate data_len;
   |#LightGreen|Socket|
   :send MgmtMessageHdr;
   |Write|
   :get the raw data; 
   note left : casts from\nchar* to void*
   |Socket|
   :send raw data;
   |Read|
   :different address space;
   : ...\nsome time later;
   |Socket|
   :read MgmtMessageHdr;
   |Read|
   :get data_len;
   |Socket|
   :read data_len bytes;
   |Read|
   :choose callback based on msg_id\nand send raw data;
   stop

==========================
RPC API for remote clients
==========================

:ts:git:`NetworkMessage.cc` provides a set of APIs for remote clients to communicate with |TManager|. 

|TManager| will then send a response with the return value. The return value only indicates if the request was successfully propogated to |TServer|. Thus, there is no way currently for |TServer| to send information back to remote clients.

.. function:: TSMgmtError send_mgmt_request(const mgmt_message_sender &snd, OpType optype, ...)
.. function:: TSMgmtError send_mgmt_request(int fd, OpType optype, ...)

      Send a request from a remote client to |TManager|.

.. function:: TSMgmtError send_mgmt_response(int fd, OpType optype, ...)

      Send a response from |TManager| to remote client.

.. function:: TSMgmtError send_mgmt_error(int fd, OpType optype, TSMgmtError error)

      Send err from |TManager| to remote client.

.. function:: TSMgmtError recv_mgmt_request(void *buf, size_t buflen, OpType optype, ...)

      Read request from remote client. Used by |TManager|.

.. function:: TSMgmtError recv_mgmt_response(void *buf, size_t buflen, OpType optype, ...)
  
      Read response from |TManager|. Used by remote clients.

Using Remote RPC API Example
============================

This details how a remote client may use the :ts:git:`NetworkMessage.cc` interface to send messages to |TManager|. Leveraging :func:`send_mgmt_request` with a :class:`mgmt_message_sender` which specifies how *send* a message, a remote client can implement its own way to send messages to |TManager| to allow for things such as preprocessing messages. 

.. class:: mgmt_message_sender 

.. class:: mgmtapi_sender : public mgmt_message_sender

   .. function:: TSMgmtError send(void *msg, size_t msglen) const override

   Specifies how the message is to be sent. Can do things such as serialization or preprocessing based on parameters.

.. code-block:: cpp   

   #define MGMTAPI_SEND_MESSAGE(fd, optype, ...) send_mgmt_request(mgmtapi_sender(fd), (optype), __VA_ARGS__)

Now, using this macro, messages can easily be sent. For example:

.. code-block:: cpp  

  TSMgmtError ret;
  MgmtMarshallData reply    = {nullptr, 0};

  // create and send request
  ret = MGMTAPI_SEND_MESSAGE(main_socket_fd, op, &optype);
  if (ret != TS_ERR_OKAY) {
    goto done;
  }

  // get a response
  ret = recv_mgmt_message(main_socket_fd, reply);
  if (ret != TS_ERR_OKAY) {
    goto done;
  }

====================================
RPC API for |TServer| and |TManager|
====================================

.. image:: ../../uml/images/RPC-states.svg
   :align: center

|LM| and |PM| follow similar workflows. A manager will poll the socket for any messages. If it is able to read a message, it will handle it based on the :arg:`msg_id` from the :class:`MgmtMessageHdr` and select a callback to run asynchoronously. The async callback will add a response, if any, to an outgoing event queue within the class. A manager will continue to poll and read on the socket as long as there are messages avaliable. Two things can stop a manager from polling. 

1. there are no longer any messages on the socket for a *timeout* time period. 
   
#. an external event, for example, a |TCtl| command, triggers an ``eventfd`` to tell the manager to stop polling. In the case of an external event, the manager will finish reading anything on the socket, but it will not wait for any timeout period. So, if there are three pending events on the socket, all three will be processed and then immediately after, the manager will stop polling. 

Once a manager is done polling, it will begin to send out messages from it's outgoing event queue. It continues to do this until there are no more messages left. Note that as a manager is polling, it is unable to write anything on the socket itself so the number of messages read will always be exhaustive. 

.. class:: BaseManager 

LocalManager
============

.. class:: LocalManager :  public BaseManager

   This class is used by |TManager| to communicate with |TServer|

   .. function:: void pollMgmtProcessServer()

      This function watches the socket and handles incoming messages from processes. Used in the main event loop of |TManager|.

   .. function:: void sendMgmtMsgToProcesses(MgmtMessageHdr *mh)
              
      This function is used by |TManager| to process the messages based on :arg:`msg_id`. It then sends the message to |TServer| over sockets.

ProcessManager
==============

.. class:: ProcessManager : public BaseManager

   This class is used by |TServer| to communicate with |TManager|

   .. function:: int pollLMConnection()

      This function periodically polls the socket to see if there are any messages from the |LM|.

   .. function:: void signalManager (int msg_id, const char *data_raw, int data_len)

      This function sends messages to the |LM| using sockets.

.. note::

   1. Both :func:`LocalManager::pollMgmtProcessServer` and :func:`ProcessManager::pollLMConnection` actually use ``select``, not ``poll`` or ``epoll``, underneath.
