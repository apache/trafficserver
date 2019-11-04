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

TSHttpTxnRedoCacheLookup
*******************

Synopsis
========

.. code-block:: cpp

    #include <ts/experimental.h>

.. function:: TSReturnCode TSHttpTxnRedoCacheLookup(TSHttpTxn txnp, const char *url, int length)

Description
===========

Perform a cache lookup with a different url.
This function rewinds the state machine to perform the new cache lookup. The cache_info for the new
url must have been initialized before calling this function.

If the length argument is -1, this function will take the length from the url argument.

Notes
=====

This API may be changed in the future version since it is experimental.

See Also
========

:manpage:`TSAPI(3ts)`,
