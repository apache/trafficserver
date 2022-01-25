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

.. include:: ../../common.defs

.. highlight:: cpp
.. default-domain:: cpp

.. _jsonrpc-handler-errors:

API Handler error codes
***********************

High level handler error codes, each particular handler can be fit into one of the following categories.
A good approach could be the following. This required coordination among all the errors, just for now, this soluction seems ok.

.. code-block:: cpp

    enum YourOwnHandlerEnum {
        FOO_ERROR = Codes::SOME_CATEGORY,
        ...
    };


.. class:: Codes

   .. enumerator:: CONFIGURATION = 1

      Errors during configuration api handling.

    .. enumerator:: METRIC = 1000

      Errors during metrics api handling.

    .. enumerator:: RECORD = 2000

      Errors during record api handling.

    .. enumerator:: SERVER = 3000

      Errors during server api handling.

    .. enumerator:: STORAGE = 4000

      Errors during storage api handling.

    .. enumerator:: PLUGIN = 4000

      Errors during plugion api handling.

    .. enumerator:: GENERIC = 30000

      Errors during generic api handling, general errors.
