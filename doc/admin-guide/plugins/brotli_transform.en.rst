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

.. _admin-plugins-brotli-transform:

Brotli Transform Plugin
*********************

If brotli is supported by the clients. This plugin will use brotli algorithm to compress the data reply from the origin server.
If the data from origin server is gzip, then do ungzip first. Content-Encoding is changed to 'br' in the response.

Configuration
=============

Disable 'Proxy.config.http.normalize_ae_gzip' in records.config to maintain the "Accept-Encoding" value in the requests.

Installation
============

Add the following line to :file:`plugin.config`::

    brotli_transform.so
