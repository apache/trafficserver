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

.. _admin-stats-core-ssl-cipher:

SSL Cipher
**********

The following statistics provide individual counters for the number of client
requests which were satisfied using the given SSL cipher.

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.AES128-GCM-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.AES128-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.AES128-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.AES256-GCM-SHA384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.AES256-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.AES256-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.CAMELLIA128-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.CAMELLIA256-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DES-CBC3-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DES-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-DSS-AES128-GCM-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-DSS-AES128-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-DSS-AES128-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-DSS-AES256-GCM-SHA384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-DSS-AES256-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-DSS-AES256-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-DSS-CAMELLIA128-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-DSS-CAMELLIA256-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-DSS-SEED-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-RSA-AES128-GCM-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-RSA-AES128-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-RSA-AES128-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-RSA-AES256-GCM-SHA384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-RSA-AES256-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-RSA-AES256-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-RSA-CAMELLIA128-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-RSA-CAMELLIA256-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.DHE-RSA-SEED-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-ECDSA-AES128-GCM-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-ECDSA-AES128-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-ECDSA-AES128-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-ECDSA-AES256-GCM-SHA384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-ECDSA-AES256-SHA384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-ECDSA-AES256-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-ECDSA-DES-CBC3-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-ECDSA-RC4-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-ECDSA-AES128-GCM-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-ECDSA-AES128-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-ECDSA-AES128-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-ECDSA-AES256-GCM-SHA384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-ECDSA-AES256-SHA384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-ECDSA-AES256-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-ECDSA-DES-CBC3-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-ECDSA-RC4-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-RSA-AES128-GCM-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-RSA-AES128-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-RSA-AES128-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-RSA-AES256-GCM-SHA384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-RSA-AES256-SHA384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-RSA-AES256-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-RSA-DES-CBC3-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDHE-RSA-RC4-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-RSA-AES128-GCM-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-RSA-AES128-SHA256 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-RSA-AES128-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-RSA-AES256-GCM-SHA384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-RSA-AES256-SHA384 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-RSA-AES256-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-RSA-DES-CBC3-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.ECDH-RSA-RC4-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.EDH-DSS-DES-CBC3-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.EDH-DSS-DES-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.EDH-RSA-DES-CBC3-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.EDH-RSA-DES-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.EXP-DES-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.EXP-EDH-DSS-DES-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.EXP-EDH-RSA-DES-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.EXP-RC2-CBC-MD5 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.EXP-RC4-MD5 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.PSK-3DES-EDE-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.PSK-AES128-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.PSK-AES256-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.PSK-RC4-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.RC4-MD5 integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.RC4-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.SEED-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.SRP-3DES-EDE-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.SRP-AES-128-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.SRP-AES-256-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.SRP-DSS-3DES-EDE-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.SRP-DSS-AES-128-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.SRP-DSS-AES-256-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.SRP-RSA-3DES-EDE-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.SRP-RSA-AES-128-CBC-SHA integer
   :type: counter

.. ts:stat:: global proxy.process.ssl.cipher.user_agent.SRP-RSA-AES-256-CBC-SHA integer
   :type: counter

