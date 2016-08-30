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

.. _traffic_via:

traffic_via
***********

Synopsis
========

:program:`traffic_via` [OPTIONS] [VIA ...]

Description
===========

:program:`traffic_via` decodes the Traffic Server Via header codes.
Via header strings are passed as command-line options. The enclosing
square brackets are optional.

If the argument is '-', :program:`traffic_via` will filter standard
input for Via headers. This modes supports only supports Via headers
that are enclosed by square brackets.

Options
=======

.. program:: traffic_via

.. option:: -h, --help

    Print usage information and exit.

.. option:: -V, --version

    Print version.

Examples
========

Decode the Via header from command-line arguments::

    $ traffic_via "[uScMsEf p eC:t cCMi p sF]"
    Via header is uScMsEf p eC:t cCMi p sF, Length is 24
    Via Header Details:
    Request headers received from client                   :simple request (not conditional)
    Result of Traffic Server cache lookup for URL          :miss (a cache "MISS")
    Response information received from origin server       :error in response
    Result of document write-to-cache:                     :no cache write performed
    Proxy operation result                                 :unknown
    Error codes (if any)                                   :connection to server failed
    Tunnel info                                            :no tunneling
    Cache Type                                             :cache
    Cache Lookup Result                                    :cache miss (url not in cache)
    Parent proxy connection status                         :no parent proxy or unknown
    Origin server connection status                        :connection open failed

Decode the Via header from a curl request, using the :ref:`X-Debug <admin-plugins-xdebug>` plugin::

    $ curl -H  "X-Debug: Via" -I http://test.example.com | traffic_via -
    Via header is uScMsSf pSeN:t cCMi p sS, Length is 24
    Via Header Details:
    Request headers received from client                   :simple request (not conditional)
    Result of Traffic Server cache lookup for URL          :miss (a cache "MISS")
    Response information received from origin server       :connection opened successfully
    Result of document write-to-cache:                     :no cache write performed
    Proxy operation result                                 :served or connection opened successfully
    Error codes (if any)                                   :no error
    Tunnel info                                            :no tunneling
    Cache Type                                             :cache
    Cache Lookup Result                                    :cache miss (url not in cache)
    Parent proxy connection status                         :no parent proxy or unknown
    Origin server connection status                        :connection opened successfully
