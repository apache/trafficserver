How to Do a Cache Remove
************************

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

Use ``TSCacheRemove`` to remove items from the cache. Possible callback
events include:

-  ``TS_EVENT_CACHE_REMOVE`` - the item was removed. There is no data
   payload for this event.

-  ``TS_EVENT_CACHE_REMOVE_FAILED`` - indicates the cache was unable to
   remove the item identified by the cache key. ``TSCacheError`` data
   indicates why the remove failed.


