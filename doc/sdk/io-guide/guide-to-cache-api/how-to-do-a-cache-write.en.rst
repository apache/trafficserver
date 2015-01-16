How to Do a Cache Write
***********************

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

Use ``TSCacheWrite`` to write to a cache (see the :ref:`sample Protocol
plugin <about-the-sample-protocol>`). Possible
callback events include:

-  ``TS_EVENT_CACHE_WRITE_READ`` - indicates the lookup was successful.
   The data passed back along with this event is a cache vconnection
   that can be used to initiate a cache write.

-  ``TS_EVENT_CACHE_OPEN_WRITE_FAILED`` - event returned when another
   continuation is currently writing to this location in the cache. Data
   payload for this event indicates the possible reason for the write
   failing (``TSCacheError``).


