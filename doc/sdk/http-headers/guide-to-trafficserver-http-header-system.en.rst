Guide to Traffic Server HTTP Header System

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


No Null-Terminated Strings
~~~~~~~~~~~~~~~~~~~~~~~~~~

It's not safe to assume that string data contained in marshal buffers
(such as URLs and MIME fields) is stored in null-terminated string
copies. Therefore, your plugins should always use the length parameter
when retrieving or manipulating these strings. You **cannot** pass in
``NULL`` for string-length return values; string values returned from
marshall buffers are not null-terminated. If you need a null-terminated
value, then use ``TSstrndup`` to automatically null-terminate a string.
The strings that come back and are not null-terminated **cannot** be
passed into the common ``str*()`` routines

.. note::
   Values returned from a marshall buffer can be ``NULL``, which means the
   field or object requested does not exist.

For example (from the ``blacklist-1`` sample)

.. code-block:: c

   char *host_string;
   int host_length;
   host_string = TSUrlHostGet (bufp, url_loc, &host_length);
   for (i = 0; i < nsites; i++) {
   if (strncmp (host_string, sites[i], host_length) == 0) {
      // ...
   }

See the sample plugins for additional examples.

.. toctree::
   :maxdepth: 2

   guide-to-trafficserver-http-header-system/duplicate-mime-fields-are-not-coalesced.en
   guide-to-trafficserver-http-header-system/mime-fields-always-belong-to-an-associated-mime-header.en
   guide-to-trafficserver-http-header-system/release-marshal-buffer-handles.en

