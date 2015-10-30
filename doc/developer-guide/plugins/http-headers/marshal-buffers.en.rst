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

.. _developer-plugins-http-headers-marshal-buffers:

Marshal Buffers
***************

A *marshal buffer*, or ``TSMBuffer``, is a heap data structure that
stores parsed URLs, MIME headers, and HTTP headers. You can allocate new
objects out of marshal buffers and change the values within a marshal
buffer. Whenever you manipulate an object, you require the handle to the
object (``TSMLoc``) and the marshal buffer containing the object
(``TSMBuffer``).

Routines exist for manipulating the object based on these two pieces of
information. For examples, see one of the following:

-  :ref:`developer-plugins-http-headers`
-  :ref:`developer-plugins-http-headers-urls`
-  :ref:`developer-plugins-http-headers-mime-headers`

The **marshal buffer functions** enable you to create and destroy
Traffic Server's marshal buffers, which are the data structures that
hold parsed URLs, MIME headers, and HTTP headers.

.. caution::
   Any marshal buffer fetched by ``TSHttpTxn*Get`` will be used by other
   parts of the system. Be careful not to destroy these shared transaction
   marshal buffers in functions such as those below:

-  `TSHttpTxnCachedReqGet <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a889b626142157077f4f3cfe479e8b8e2>`_
-  `TSHttpTxnCachedRespGet <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#ae8f24b8dabb5008ad11620a11682ffd6>`_
-  `TSHttpTxnClientReqGet <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#acca66f22d0f87bf8f08478ed926006a5>`_
-  `TSHttpTxnClientRespGet <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a92349c8363f72b1f6dfed3ae80901fff>`_
-  `TSHttpTxnServerReqGet <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#aac2343a8b47bf9150f3ff7cd4e692d57>`_
-  `TSHttpTxnServerRespGet <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a39e8bfb199eadabb54c067ff25a9a400>`_
-  `TSHttpTxnTransformRespGet <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a20367f5469e8b7e73621c1316091d578>`_

