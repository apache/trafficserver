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

TSContDestroy
*************

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void TSContDestroy(TSCont contp)

Description
===========

Destroys a continuation created with :func:`TSContCreate`.  If :func:`TSContDestroy` is called
in the continuation's handler function, and the contination has a mutex, it must be unlocked just
before destroying the continuation.  Otherwise, |TS| will abort.  Example:

.. code-block:: cpp

    int handler(TSCont contp, TSEvent, void *)
    {
      TSMutex mtx = TSContMutexGet(contp);
      if (mtx) {
        TSMutexUnlock(mtx);
      }
      TSContDestroy(contp);
      return 0;
    }
