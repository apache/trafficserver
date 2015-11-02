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

.. include:: ../../../common.defs

.. _developer-plugins-hooks-intercepting:

Intercepting HTTP Transactions
******************************

The intercepting HTTP transaction functions enable plugins to intercept
transactions either after the request is received or upon contact with
the origin server. The plugin then acts as the origin server using the
``TSVConn`` interface. The intercepting HTTP transaction function allow
for reading ``POST`` bodies in plugins as well as using alternative
transports to the origin server.The intercepting HTTP transaction
functions are:

-  :c:func:`TSHttpTxnIntercept`

