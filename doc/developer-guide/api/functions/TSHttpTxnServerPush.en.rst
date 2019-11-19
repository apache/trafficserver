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

TSHttpTxnServerPush
*******************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: TSReturnCode TSHttpTxnServerPush(TSHttpTxn txnp, const char *url, int url_len)

Description
===========

Push a content to a client with Server Push mechanism.

This API works only if the protocol of a transaction supports Server Push and it
is not disabled by the client. You can call this API without checking whether
Server Push is available on the transaction and it does nothing if Server Push
is not available.

This API returns an error if the URL to push is not valid, the client has Server Push disabled,
or there is an error creating the H/2 PUSH_PROMISE frame.

See Also
========

:manpage:`TSAPI(3ts)`,
