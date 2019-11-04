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

TSHttpConnect
*************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSVConn TSHttpConnect(sockaddr const * addr)

Description
===========

Allows the plugin to initiate an HTTP connection.

The :c:type:`TSVConn` the plugin receives as the result of successful
operates identically to one created through :c:type:`TSNetConnect`.
Aside from allowing the plugin to set the client ip and port for
logging, the functionality of :c:func:`TSHttpConnect` is identical to
connecting to localhost on the proxy port with :c:func:`TSNetConnect`.
:c:func:`TSHttpConnect` is more efficient than :c:func:`TSNetConnect`
to localhost since it avoids the overhead of passing the data through
the operating system.
