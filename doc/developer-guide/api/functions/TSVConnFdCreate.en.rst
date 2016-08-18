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

TSVConnFdCreate
***************

Create a TSVConn from a socket.

Synopsis
========

`#include <ts/ts.h>`

.. function:: TSVConn TSVConnFdCreate(int fd)

Description
===========

:func:`TSVConnFdCreate` accepts a network socket :arg:`fd` and returns a new
:type:`TSVConn` constructed from the socket. The socket descriptor must be an
already connected socket. It will be placed into non-blocking mode.

Return Values
=============

On success, the returned :type:`TSVConn` object owns the socket and the
caller must not close it. If :func:`TSVConnFdCreate` fails, :literal:`NULL`
is returned, the socket is unchanged and the caller must close it.

Examples
========

The example below is excerpted from `example/intercept/intercept.cc`
in the Traffic Server source distribution. It demonstrates how to
use :func:`TSVConnFdCreate` to construct a :type:`TSVConn` from a
connected socket.

.. literalinclude:: ../../../../example/intercept/intercept.cc
  :language: c
  :lines: 288-336

See Also
========

:manpage:`TSAPI(3ts)`
