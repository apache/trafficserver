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

.. _admin-stats-core-ssl-group:

TLS Negotiated Group
********************

The following statistics provide individual counters for the number of TLS negotiated group with client.
Some groups do not appear if linked TLS library doesn't support them.

.. ts:stat:: global proxy.process.ssl.group.user_agent.ffdhe2048 integer integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.ffdhe3072 integer integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.ffdhe4096 integer integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.ffdhe6144 integer integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.ffdhe8192 integer integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.P-224 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.P-256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.P-384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.P-521 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.X448 integer integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.X25519 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.X25519MLKEM768 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.group.user_agent.OTHER integer
   :type: counter
