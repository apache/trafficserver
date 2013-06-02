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

-  ```TSDebug`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#afccd91047cc46eb35478a751ec65c78d>`__
   prints out a formatted statement if you are running Traffic Server in
   debug mode.

-  ```TSIsDebugTagSet`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a2d3ceac855c1cde83eff5484bc952288>`__
   checks to see if a debug tag is set. If the debug tag is set, then
   Traffic Server prints out all debug statements associated with the
   tag.

-  ```TSError`` <http://people.apache.org/~amc/ats/doc/html/InkAPI_8cc.html#a19ff77fecfc3e331b03da6e358907787>`__
   prints error messages to Traffic Server's error log

-  ```TSAssert`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#ad94eb4fb1f08082ea1634f169cc49c68>`__
   enables the use of assertion in a plugin.

-  ```TSReleaseAssert`` <http://people.apache.org/~amc/ats/doc/html/ts_8h.html#a5e751769785de91c52bd503bcbc28b0a>`__
   enables the use of assertion in a plugin.


