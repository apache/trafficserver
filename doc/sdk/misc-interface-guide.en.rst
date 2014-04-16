Miscellaneous Interface Guide
*****************************

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

Most of the functions in the Traffic Server API provide an interface to
specific code modules within Traffic Server. The miscellaneous functions
described in this chapter provide some useful general capabilities. They
are categorized as follows:

.. toctree::
   :maxdepth: 2

   misc-interface-guide/tsfopen-family.en
   misc-interface-guide/memory-allocation.en
   misc-interface-guide/thread-functions.en


The C library already provides functions such as ``printf``, ``malloc``,
and ``fopen`` to perform these tasks. The Traffic Server API versions,
however, overcome various C library limitations (such as portability to
all Traffic Server-support platforms).

Debugging Functions
-------------------

-  :c:func:`TSDebug`
   prints out a formatted statement if you are running Traffic Server in
   debug mode.

-  :c:func:`TSIsDebugTagSet`
   checks to see if a debug tag is set. If the debug tag is set, then
   Traffic Server prints out all debug statements associated with the
   tag.

-  :c:func:`TSError`
   prints error messages to Traffic Server's error log

-  :c:func:`TSAssert`
   enables the use of assertion in a plugin.

-  :c:func:`TSReleaseAssert`
   enables the use of assertion in a plugin.


