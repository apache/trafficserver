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

.. _developer-plugins-getting-started-naming:

Naming Conventions
******************

The Traffic Server API adheres to the following naming conventions:

-  The ``TS`` prefix is used for all function and variable names defined
   in the Traffic Server API. **Examples**:
   ``TS_EVENT_NONE``,\ ``TSMutex``, and ``TSContCreate``

-  Enumerated values are always written in all uppercase letters.
   **Examples**: ``TS_EVENT_NONE`` and ``TS_VC_CLOSE_ABORT``

-  Constant values are all uppercase; enumerated values can be seen as a
   subset of constants. **Examples**: ``TS_URL_SCHEME_FILE`` and
   ``TS_MIME_FIELD_ACCEPT``

-  The names of defined types are mixed-case. **Examples**:
   ``TSHttpSsn`` and ``TSHttpTxn``

-  Function names are mixed-case. **Examples**: ``TSUrlCreate`` and
   ``TSContDestroy``

-  Function names use the following subject-verb naming style:
   ``TS-<subject>-<verb>``, where ``<subject>`` goes from general to
   specific. This makes it easier to determine what a function does by
   reading its name. **For** **example**: the function to retrieve the
   password field (the specific subject) from a URL (the general
   subject) is ``TSUrlPasswordGet``.

-  Common verbs like ``Create``, ``Destroy``, ``Get``, ``Set``,
   ``Copy``, ``Find``, ``Retrieve``, ``Insert``, ``Remove``, and
   ``Delete`` are used only when appropriate.


