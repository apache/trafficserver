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

.. _admin-stats-core-socks:

SOCKS
*****

.. ts:stat:: global proxy.process.socks.connections_currently_open integer
   :type: gauge
   :ungathered:

   Provides the number of SOCKS proxy connections currently opened with the
   running instance of |TS|.

.. ts:stat:: global proxy.process.socks.connections_successful integer
   :type: counter
   :ungathered:

   Indicates the number of SOCKS connections to |TS| which have succeeded since
   statistics collection began.

.. ts:stat:: global proxy.process.socks.connections_unsuccessful integer
   :type: counter
   :ungathered:

   Indicates the number of attempted SOCKS connections to |TS| which have failed
   since statistics collection began.

