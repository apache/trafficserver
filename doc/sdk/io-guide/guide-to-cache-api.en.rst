Guide to the Cache API
**********************

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

.. toctree::
   :maxdepth: 2

   guide-to-cache-api/how-to-do-a-cache-write.en
   guide-to-cache-api/how-to-do-a-cache-remove.en
   guide-to-cache-api/errors.en
   guide-to-cache-api/example.en


The cache API enables plugins to read, write, and remove objects in the
Traffic Server cache. All cache APIs are keyed by an object called an
``TSCacheKey``; cache keys are created via ``TSCacheKeyCreate``; keys
are destroyed via ``TSCacheKeyDestroy``. Use ``TSCacheKeyDigestSet`` to
set the hash of the cache key.

Note that the cache APIs differentiate between HTTP data and plugin
data. The cache APIs do not allow you to write HTTP docs in the cache;
you can only write plugin-specific data (a specific type of data that
differs from the HTTP type).

**Example:**

.. code-block:: c

        const unsigned char *key_name = "example key name";

        TSCacheKey key;
        TSCacheKeyCreate (&key);
        TSCacheKeyDigestSet (key, (unsigned char *) key_name , strlen(key_name));
        TSCacheKeyDestroy (key);

How to Do a Cache Read
~~~~~~~~~~~~~~~~~~~~~~

``TSCacheRead`` does not really read - it is used for lookups (see the
sample Protocol plugin). Possible callback events include:

-  ``TS_EVENT_CACHE_OPEN_READ`` - indicates the lookup was successful.
   The data passed back along with this event is a cache vconnection
   that can be used to initiate a read on this keyed data.

-  ``TS_EVENT_CACHE_OPEN_READ_FAILED`` - indicates the lookup was
   unsuccessful. Reasons for this event could be that another
   continuation is writing to that cache location, or the cache key
   doesn't refer to a cached resource. Data payload for this event
   indicates the possible reason the read failed (``TSCacheError``).


