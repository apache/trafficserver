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
.. default-domain:: cpp

TSTxnType
************

Synopsis
========

.. code-block:: cpp

.. enum:: TSTxnType

   Specify the type of a transaction argument

   .. enumerator:: TS_TXN_TYPE_UNKNOWN

      Invalid value. This is used to indicate a failure or for initialization.

   .. enumerator:: TS_TXN_TYPE_HTTP

      A HTTP transaction. This includes CONNECT method requests which will create a tunnel.

   .. enumerator:: TS_TXN_TYPE_EXPLICIT_TUNNEL

      A blind tunnel transaction created based on a configuration file or an API call.

   .. enumerator:: TS_TXN_TYPE_TR_PASS_TUNNEL

      A blind tunnel created based on a parse error for a server port with tr-pass set.

Description
===========

Specify the type of a transaction.  Plugins can use this to determine if they are interacting with tunnels
or parsed HTTP requests.
