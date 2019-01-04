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

TSVConnReenable
***************

Synopsis
========

`#include <ts/ts.h>`

.. function:: void TSVConnReenable(TSVConn svc)

Description
===========

Reenable the SSL connection :arg:`svc`. If a plugin hook is called, ATS
processing on that connection will not resume until this is invoked for that
connection.

If the server is running OpenSSL 1.0.2, the plugin writer can pause SSL handshake
processing at the certificate callback  by not reenabling the connection.
Running an OpenSSL versions older than 1.0.2, the handshake processing in
``SSL_accept`` will not be stopped even if the SNI callback does not reenable
the connection.

Additional processing could reenable the virtual connection causing the
``SSL_accept`` to be called again to complete the handshake exchange.  In the
case of a blind tunnel conversion, the SSL handshake will never be completed by
Traffic Server.

This call does appropriate locking and scheduling, so it is safe to call from
another thread.

TSVConnReenableEx
*****************

Synopsis
========

`#include <ts/ts.h>`

.. function:: void TSVConnReenableEx(TSVConn svc, TSEvent event)

Description
===========

An extended verion of TSVConnEnable that allows the plugin to return a status to
the core logic.  If all goes well this is TS_EVENT_CONTINUE.  However, if
the plugin wants to stop the processing it can set the event to TS_EVENT_ERROR.

For example, in the case of the TS_SSL_VERIFY_SERVER_HOOK, the plugin make decide the 
origin certificate is bad.  By calling TSVonnReenable with TS_EVENT_ERROR, the 
certificate check will error and the TLS handshake will fail.


