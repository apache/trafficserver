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

=============
Glossary
=============

.. glossary::

   continuation
      A callable object that contains state. These are are mechanism used by Traffic Server to implement callbacks and
      continued computations. Continued computations are critical to efficient processing of traffic because by avoiding
      any blocking operations that wait on external events. In any such case a continuation is used so that other
      processing can continue until the external event occurs. At that point the continuation is invoked to continue the
      suspended processing. This can be considered similar to co-routines.

   session
      A single connection from a client to Traffic Server, covering all requests and responses on that connection.

   transaction
      A client request and response, either from the origin server or from the cache.

