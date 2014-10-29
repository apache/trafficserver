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

============
traffic_via
============

Synopsis
========


:program:`traffic_via` [options]

.. _traffic-via-commands:

Description
===========

:program:`traffic_via` is used to decode via header. It can be used as follows.
Pipe output of any command to via_decoder. This supports via header within [] only


Options
=======

.. program:: traffic_via

.. option:: -h, --help

    Print usage information and exit.

.. option:: -V, --version

    Print version.


Examples
========

Decode via header::
    
    $ ./traffic_via -V
        Apache Traffic Server - traffic_via - 5.0.0 - (build # 81915 on Sep 19 2014 at 15:02:08)
    
    $ ./traffic_via -h
        Traffic via decoder usage:
        Pipe output of any command to traffic_via. This supports via header within [] only
            echo [viaheader] 2>&1| traffic_via

        switch__________________type__default___description
        -V, --version           tog   false     Print Version Id
        
    $ echo [uScMsEf p eC:t cCMi p sF] 2>&1| ./traffic_via
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
        ICP status                                             :no icp
        Parent proxy connection status                         :no parent proxy or unknown
        Origin server connection status                        :connection open failed