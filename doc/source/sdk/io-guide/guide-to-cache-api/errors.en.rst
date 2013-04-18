Errors
******

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

Errors pertaining to the failure of various cache operations are
indicated by ``TSCacheError`` (enumeration). They are as follows:

-  ``TS_CACHE_ERROR_NO_DOC`` - the key does not match a cached resource

-  ``TS_CACHE_ERROR_DOC_BUSY`` - e.g, another continuation could be
   writing to the cache location

-  ``TS_CACHE_ERROR_NOT_READY`` - the cache is not ready


