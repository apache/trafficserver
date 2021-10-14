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


TSHttpTxnCntl
=============

Synopsis
--------

.. code-block:: cpp

    #include <ts/ts.h>

.. c:function:: bool TSHttpTxnCntlGet(TSHttpTxn txnp, TSHttpCntlType ctrl)
.. c:function:: TSReturnCode TSHttpTxnCntlSet(TSHttpTxn txnp, TSHttpCntlType cntl, bool data)

Description
-----------
Set or Get the status of various control mechanisms within the HTTP transaction. The control
type must be one of the values are identified by the enumeration :c:enum:`TSHttpCntlType`. The
values are boolean values, ``true`` and ``false``. A ``true`` values turns on the transaction
feature, and the ``false`` value turns it off.
