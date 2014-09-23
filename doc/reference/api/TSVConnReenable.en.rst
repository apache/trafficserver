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


TSVConnReenable
===============

Synopsis
--------

`#include <ts/ts.h>`

.. c:function:: void TSSslVConnReenable(TSVConn svc)

Description
-----------

   Reenable the SSL connection :arg:`svc`. If a plugin hook is called, ATS processing on that connnection will not resume until this is invoked for that connection.

If the server running Traffic Server has the appropriate openSSL patch installed, the SNI callback can return ``SSL_TLSEXT_ERR_READ_AGAIN`` to stop the SSL handshake processing.  This results in ``SSL_accept`` returning ``SSL_ERROR_WANT_SNI_RESOLVE`` before completing the SSL handshake (only the client hello message will have been received).  Additional processing could reenable the virtual connection causing the ``SSL_accept`` to be called again to complete the handshake exchange.  In the case of a blind tunnel conversion, the SSL handshake will never be completed by Traffic Server.

The plugin callbacks can halt the SSL handshake processing by not reenabling the connection (i.e., by not calling :c:func:`TSSslVConnReenable`).  If a plugin SNI callback does not reenable the connection, the global callback will return ``SSL_TLSEXT_ERR_READ_AGAIN``.

Without the openSSL patch, the handshake processing in ``SSL_accept`` will not be stopped even if the SNI callback does not reenable the connection.


