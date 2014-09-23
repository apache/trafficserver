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


TSVConnTunnel
===========

Synopsis
--------

`#include <ts/ts.h>`

.. c:function:: TSReturnCode TSVConnTunnel(TSVConn svc)


Description
-----------

   Set the SSL connection :arg:`svc` to convert to a blind tunnel.  Can be called from the TS_VCONN_PRE_ACCEPT_HOOK or the TS_SSL_SNI_HOOK.

For this to work from the TS_SSL_SNI_HOOK, the openSSL patch must be applied which adds the ability to break out of the SSL_accept processing by returning SSL_TLSEXT_ERR_READ_AGAIN.

