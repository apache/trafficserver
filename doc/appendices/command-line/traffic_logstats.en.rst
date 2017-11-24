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

.. _traffic_logstats:

traffic_logstats
****************

Synopsis
========

:program:`traffic_logstats` [options]

Description
===========

:program:`traffic_logstats` is a log parsing utility, that is intended to
produce metrics for total and per origin requests. Currently, this utility
only supports parsing and processing the Squid binary log format, or a custom
format that is compatible with the initial log fields of the Squid format.

Output can either be a human readable text file, or a JSON format. Parsing can
be done incrementally, and :program:`traffic_logstats` supports restarting
where it left off previously (state is stored in an external file). This is
useful when collecting metrics periodically into a stats processing system,
and also supports the case where a log file is rotated.

The per-URL metrics (*-u*) requires that you specify a size of the LRU used
for keeping the counters. This is to assure that :program:`traffic_logstats`
does not consume an exorbitant amount of memory.

Options
=======

.. program:: traffic_logstats

.. option:: -f FILE, --log_file FILE

.. option:: -o LIST, --origin_list LIST

.. option:: -O FILE, --origin_file FILE

.. option:: -M COUNT, --max_origins COUNT

.. option:: -u COUNT, --urls COUNT

.. option:: -U COUNT, --show_urls COUNT

.. option:: -A, --as_object

.. option:: -C, --concise

.. option:: -i, --incremental

.. option:: -S FILE, --statetag FILE

.. option:: -t, --tail

.. option:: -s, --summary

.. option:: -j, --json

.. option:: -c, --cgi

.. option:: -m, --min_hits

.. option:: -a, --max_age

.. option:: -l COUNT, --line_len COUNT

.. option:: -T TAGS, --debug_tags TAGS

.. option:: -h, --help

   Print usage information and exit.

.. option:: -V, --version

   Print version information and exit.

