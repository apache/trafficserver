.. Licensed to the Apache Software Foundation (ASF) under one
   or more contributor license agreements.  See the NOTICE file
   distributed with this work for additional information
   regarding copyright ownership.  The ASF licenses this file
   to you under the Apache License, Version 2.0 (the
   "License"); you may not use this file except in compliance
   with the License.  You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

.. include:: ../../../common.defs

.. This is basically a holding pen to avoid dangling references as I can't find a better way to deal with
   non-ATS types. Some of this was handled by the "EXTERNAL_TYPES" in ext/traffic-server.py but that's
   even uglier than this.

System Types
************

Synopsis
========

This is a place for defining compiler or system provided types to avoid dangling references.

Description
===========

These types are provided by the compiler ("built-in") or from a required operating system, POSIX, or package header.

.. cpp:type:: uint24_t

.. cpp:class:: IpEndpoint

   A wrapper for :code:`sockaddr` types.

.. cpp:class:: IpAddr

   Storage for an IP address.
