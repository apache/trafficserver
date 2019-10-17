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
**********************

Description
===========

The JA3 fingerprint plugin calculates JA3 fingerprints for incoming SSL traffic. "JA3" is a method
for creating SSL/TLS client fingerprints by concatenating values in the `TLS Client Hello
<https://tools.ietf.org/html/rfc5246#section-7.4.1.2>`__ and hashing the result using `MD5
<https://www.openssl.org/docs/man1.1.0/man3/MD5_Init.html>`__ to produce a 32 character fingerprint.
A particular instance of malware tends to use the same encryption code/client, which makes it an
effective way to detect malicious clients even when superficial details are modified. More info about
JA3 is available `here <https://github.com/salesforce/ja3>`__.

The calculated JA3 fingerprints are then appended to upstream request in the field ``X-JA3-Sig``
(to be processed at upstream). If multiple duplicates exist for the field name, it will append to the last 
occurrence; if none exists, it will add such a field to the headers. The signatures can also be logged locally.

Plugin Configuration
====================
.. program:: ja3_fingerprint.so

``ja3_fingerprint`` can be used as a global/remap plugin and is configured via :file:`plugin.config`
or :file:`remap.config`.

.. option:: --ja3raw

   This option cause the plugin to append the field ``X-JA3-Raw`` to proxy request. The field value
   is the raw JA3 fingerprint.

   By default this is not enabled.

.. option:: --ja3log


   This option enables logging to the file ``ja3_fingerprint.log`` in the standard logging
   directory. The format is ::

      [time] [client IP] [JA3 string] [JA3 hash]

   By default this is not enabled.

Requirement
=============

This requires OpenSSL 1.0.1, 1.0.2, or OpenSSL 1.1.1 or later. OpenSSL 1.1.0 will not work due to
API changes with regard to opaque structures.

There is a potential issue with very old TLS clients which can cause a crash in the plugin. This is
due to a `bug in OpenSSL <https://github.com/openssl/openssl/pull/8756>`__ which should be fixed in
a future release.

