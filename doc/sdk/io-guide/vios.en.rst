VIOs
****

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

A **VIO**, or **virtual IO**, is a description of an IO operation that's
currently in progress. The VIO data structure is used by vconnection
users to determine how much progress has been made on a particular IO
operation and to re-enable an IO operation when it stalls due to buffer
space issues. VIOs are used by vconnection implementors to determine the
buffer for an IO operation, how much work to do on the IO operation, and
which continuation to call back when progress on the IO operation is
made.

The ``TSVIO`` data structure itself is opaque, but it could be defined
as follows:

::

        ::::c
    typedef struct {
        TSCont continuation;
        TSVConn vconnection;
        TSIOBufferReader reader;
        TSMutex mutex;
        int nbytes;
        int ndone;
    } *TSVIO;

The VIO functions below access and modify various parts of the data
structure.

-  ```TSVIOBufferGet`` <http://people.apache.org/~amc/ats/doc/html/InkIOCoreAPI_8cc.html#a55df75b6ba6e9152292a01e0b4e21963>`__
-  ```TSVIOVConnGet`` <http://people.apache.org/~amc/ats/doc/html/InkIOCoreAPI_8cc.html#a32b9eaaadf2145f98ceb4d64b7c06c2f>`__
-  ```TSVIOContGet`` <http://people.apache.org/~amc/ats/doc/html/InkIOCoreAPI_8cc.html#a071f12b307885c02aceebc41601bbdcf>`__
-  ```TSVIOMutexGet`` <http://people.apache.org/~amc/ats/doc/html/InkIOCoreAPI_8cc.html#ab4e8c755cf230918a14a4411af8b3e63>`__
-  ```TSVIONBytesGet`` <http://people.apache.org/~amc/ats/doc/html/InkIOCoreAPI_8cc.html#af6fc57adc7308864b343b6b7fd30c5ff>`__
-  ```TSVIONBytesSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a27594723f14891ac43da3e1368328d0e>`__
-  ```TSVIONDoneGet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ad71156f68a119c00502ff1fd598824ab>`__
-  ```TSVIONDoneSet`` <http://people.apache.org/~amc/ats/doc/html/InkIOCoreAPI_8cc.html#af4590966899039571d874e0c090042ad>`__
-  ```TSVIONTodoGet`` <http://people.apache.org/~amc/ats/doc/html/InkIOCoreAPI_8cc.html#a1dd145ddd60822a5f892becf7b8e8f84>`__
-  ```TSVIOReaderGet`` <http://people.apache.org/~amc/ats/doc/html/InkIOCoreAPI_8cc.html#a471ee1fde01fbeabce6c39944dfe9da6>`__
-  ```TSVIOReenable`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a792ef9d6962193badad2877a81d8bcff>`__

