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

.. _admin-plugins-ja3_fingerprint:


JA3 Fingerprint Plugin
*******************

Description
===========

``JA3 Fingerprint`` calculates JA3 fingerprints for incoming SSL traffic. "JA3 is a method for creating SSL/TLS client fingerprints" by concatenating values in ClientHello packet and MD5 hash the result to produce a 32 character fingerprint. Malwares tend to use the same encryption code/client, which makes it an effective way to detect malicious clients. More info about ja3 is available: https://github.com/salesforce/ja3.

The calculated JA3 fingerprints are then appended to upstream request (to be processed at upstream) and/or logged locally (depending on the config).

Plugin Configuration
====================
.. program:: ja3_fingerprint.so

* ``ja3_fingerprint`` can be used as a global/remap plugin and is configured via :file:`plugin.config` or :file:`remap.config`.
   .. option:: --ja3raw

   (`optional`, default:unused) - enables raw fingerprints header. With this option, the plugin will append additional header `X-JA3-Raw` to proxy request.

   .. option:: --ja3log

   (`optional`, default:unused) - enables local logging. With this option, the plugin will log JA3 info to :file:`ja3_fingerprint.log` in the standard logging directory. The format is: [time] [client IP] [JA3 string] [JA3 hash]

Requirement
=============
Won't compile against OpenSSL 1.1.0 due to API changes and opaque structures.
