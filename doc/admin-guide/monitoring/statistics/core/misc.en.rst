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

.. include:: ../../../../common.defs

.. _admin-stats-core-misc:

Miscellaneous
*************

.. ts:stat:: global proxy.process.http.misc_count_stat integer
.. ts:stat:: global proxy.process.http.misc_user_agent_bytes_stat integer

.. ts:stat:: global proxy.process.eventloop.count integer

    Number of event loops executed.

.. ts:stat:: global proxy.process.eventloop.events integer

    Number of events dispatched.

.. ts:stat:: global proxy.process.eventloop.events.min integer

    Minimum number of events dispatched in a loop.

.. ts:stat:: global proxy.process.eventloop.events.max integer

    Maximum number of events dispatched in a loop.

.. ts:stat:: global proxy.process.eventloop.wait integer

    Number of loops that did a conditional wait.

.. ts:stat:: global proxy.process.eventloop.time.min integer
    :units: nanoseconds

    Shortest time spent in a loop.

.. ts:stat:: global proxy.process.eventloop.time.max integer
    :units: nanoseconds

    Longest time spent in a loop.
