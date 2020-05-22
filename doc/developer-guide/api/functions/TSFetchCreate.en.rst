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

.. default-domain:: c

TSFetchCreate
*************

Traffic Server asynchronous Fetch API.

Synopsis
========

.. code-block:: cpp

    #include <ts/ts.h>

.. function:: void TSFetchPages(TSFetchUrlParams_t *)
.. function:: TSFetchSM TSFetchUrl(const char *, int, sockaddr const *, TSCont, TSFetchWakeUpOptions, TSFetchEvent)
.. function:: void TSFetchFlagSet(TSFetchSM, int)
.. function:: TSFetchSM TSFetchCreate(TSCont, const char *, const char *, const char *, struct sockaddr const *, int)
.. function:: void TSFetchHeaderAdd(TSFetchSM, const char *, int, const char *, int)
.. function:: void TSFetchWriteData(TSFetchSM, const void *, size_t)
.. function:: ssize_t TSFetchReadData(TSFetchSM, void *, size_t)
.. function:: void TSFetchLaunch(TSFetchSM)
.. function:: void TSFetchDestroy(TSFetchSM)
.. function:: void TSFetchUserDataSet(TSFetchSM, void *)
.. function:: void* TSFetchUserDataGet(TSFetchSM)
.. function:: TSMBuffer TSFetchRespHdrMBufGet(TSFetchSM)
.. function:: TSMLoc TSFetchRespHdrMLocGet(TSFetchSM)

Description
===========

Traffic Server provides a number of routines for fetching resources asynchronously.
These API are useful to support a number of use cases that may involve sideways
calls, while handling the client request. Some typical examples include centralized
rate limiting framework, database lookups for login/authentication, refreshing configs
in the background asynchronously, ESI etc.
