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

.. c:type:: off_t

   `Reference <https://linux.die.net/include/unistd.h>`__.

.. cpp:type:: off_t

   `Reference <https://linux.die.net/include/unistd.h>`__.

.. cpp:type:: uint64_t

   `Reference <https://linux.die.net/include/stdint.h>`__.

.. cpp:type:: uint32_t

   `Reference <https://linux.die.net/include/stdint.h>`__.

.. cpp:type:: uint16_t

   `Reference <https://linux.die.net/include/stdint.h>`__.

.. cpp:type:: uint8_t

   `Reference <https://linux.die.net/include/stdint.h>`__.

.. cpp:type:: intmax_t

   The largest native signed integer type.

.. cpp:type:: size_t

   Unsigned integral type.

.. cpp:type:: ssize_t

   Signed integral type. 

.. cpp:type:: unspecified_type

   This represents a type whose name is not known to, nor needed by, the API user. Usually this is a complex template which is consumed by other elements of the API and not intended for explicit use.

.. cpp:type:: time_t

   Epoch time, in seconds.

.. cpp:type:: va_list

   Variable Argument List.
