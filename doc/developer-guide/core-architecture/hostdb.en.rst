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

.. _developer-doc-hostdb:

HostDB
******

HostDB is a cache of DNS results. It is used to increase performance by aggregating address
resolution across transactions.

Runtime Structure
=================

DNS results are stored in a global hash table as instances of ``HostDBRecord``. Each instance
stores the results of a single query. These records are not updated with new DNS results. Instead
a new record instance is created and replaces the previous instance in the table. Some specific
dynamic data is migrated from the old record to the new one, such as the failure status of the
upstreams in the record.


