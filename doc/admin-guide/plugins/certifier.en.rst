.. _admin-plugins-certifier:

Certifier Plugin
***************

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

Description
===========

The ``certifier`` performs two basic tasks:

#. Load SSL certificates from file storage on demand. The total number of loaded certificates kept in memory can be configured.
#. Generates SSL certificates on demand. Generated certificates will be written to file storage for later retrieval.

Implementation
==============
Certificates management is done by `SslLRUList` (a Least-Recently-Used (LRU) list with a lookup map). The structure holding all info related to a given SNI is `SslData`. On lookup/insertion, the lookup map will be accessed and the corresponding `SslData` pointer will be moved to the head in the list. If on lookup/insertion the internal count exceeds the given limit on number of files, the tail node will be removed from both the list and the map.

When the plugin sees an incoming HTTPS request, it does:

#. Look up the SNI in `SslLRUList` and set up the context if a valid context exists. Otherwise, it will schedule a thread to retrieve such context from disk (or generate). If such a thread is already scheduled, it will put this SSL connection onto the queue.
#. The retriever thread will first try to load the cert from disk. If no such certs exist and dynamic generation is enabled, the thread will generate a Certificate Signing Request (CSR) with provided SNI and sign it with root CA passed in from config options. The signed certificate is then written to the disk and all queued up SSL connections are set up with correct context and enabled.

Setup
=====
* Cert and key:
   For example, ``openssl req -newkey rsa:2048 -nodes -keyout ca.key -x509 -days 365 -out ca.cert``. Note that the cert/key should be generated without a challenge password.
* Serial number:
   A text file containing a valid integer with a trailing new line.

Plugin Configuration
====================
.. program:: certifier.so
* Specify certificate generation related files. If any of the following parameters is missing, the dynamic generation will be disabled.
   .. option:: --sign-cert <path_to_certificate>
   (`optional`, default:empty/unused) - specifies the path to the root CA certficate. In most cases, this would be a self-signed certificate that is configured to be trusted by all potential clients. Path should be the path and file name of the cert. If it is relative, it is relative to the Traffic Server configuration directory.

   .. option:: --sign-key <path_to_key>
   (`optional`, default:empty/unused) - specifies the path to the root CA private key. In most cases, this would be generated alongside the self-signed root CA certificate.

   .. option:: --sign-serial <path_to_serial>
   (`optional`, default:empty/unused) - specifies the path to the serial number file. This will be used to assign serial numbers to certificates and keep all generated ones in sync. Serial file should be a number with a trailing newline.
* Specify the certificates management related settings.
   .. option:: --store <path_to_certs_dir>
   (`required`, default:empty) - specifies the directory to use as the root of file system certificates storage.

   .. option:: --max <N>
   (`required`, default:empty) - specifies the upper limit on number of files kept in memory.


Example Usage
=============
To use this plugin, enable it in a :file:`plugin.config` rule, specifying certificates storage path, max number of certificates in memory, and signing cert+key+serial. For example:

   certifier.so --store=/home/zeyuan/certifier/certs --max=1000 --sign-cert=/home/zeyuan/certifier/root-ca.crt --sign-key=/home/zeyuan/certifier/root-ca.key --sign-serial=/home/zeyuan/certifier/ca-serial.txt

One use case would be routing incoming CONNECT request to another port on traffic server. With the certifier generating a trusted certificate, other plugins can act with a similar behavior to Man-In-The-Middle (logging interesting data for example).

.. uml::
   :align: center
   actor "User"
   participant "Traffic_Server"
   participant "Origin_Server"
   [User] -> [Traffic_Server]: CONNECT request
   [Traffic_Server] -> [Traffic_Server]: Route CONNECT\nback to self
   [User] -->> [Traffic_Server]: Client Hello
   [Traffic_Server] -->> [User]: Server Hello with fake certs from certifier
   [User] -->> [Traffic_Server]: ClientKeyExchange [ChangeCipherSpec]
   [Traffic_Server] -->> [User]: ChangeCipherSpec
   [User] <-> [Traffic_Server]: Tunnel established
   [User] -> [Traffic_Server]: User request via tunnel
   [Traffic_Server] -> [Origin_Server]: Request
   [Origin_Server] --> [Traffic_Server]: Response
   [Traffic_Server] --> [User]: TS response via tunnel
@enduml
