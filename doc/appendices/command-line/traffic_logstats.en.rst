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

   Specific logfile to parse

.. option:: -o LIST, --origin_list LIST

   Only show stats for listed Origins

.. option:: -O FILE, --origin_file FILE

   File listing Origins to show

.. option:: -M COUNT, --max_origins COUNT

   Max number of Origins to show

.. option:: -u COUNT, --urls COUNT

   Produce JSON stats for URLs, argument is LRU size

.. option:: -U COUNT, --show_urls COUNT

   Only show max this number of URLs

.. option:: -A, --as_object

   Produce URL stats as a JSON object instead of array

.. option:: -C, --concise

   Eliminate metrics that can be inferred from other values

.. option:: -i, --incremental

   Incremental log parsing

.. option:: -S FILE, --statetag FILE

   Name of the state file to use

.. option:: -t, --tail

   Parse the last <sec> seconds of log

.. option:: -s, --summary

   Only produce the summary

.. option:: -j, --json

   Produce JSON formatted output

.. option:: -c, --cgi

   Produce HTTP headers suitable as a CGI

.. option:: -m, --min_hits

   Minimum total hits for an Origin

.. option:: -a, --max_age

   Max age for log entries to be considered

.. option:: -l COUNT, --line_len COUNT

   Output line length

.. option:: -T TAGS, --debug_tags TAGS

   Colon-Separated Debug Tags

.. option:: -r, --report_per_user

   Report stats per username of the authenticated client ``caun`` instead of host, see `squid log format <../../admin-guide/logging/examples.en.html#squid>`_

.. option:: -n, --no_format_check

   Don't validate the log format field names according to the `squid log format <../../admin-guide/logging/examples.en.html#squid>`_.
   This would allow squid format fields to be replaced, i.e. the username of the authenticated client ``caun`` with a random header value by using ``cqh``,
   or to remove the client's host IP address from the log for privacy reasons.

.. option:: -h, --help

   Print usage information and exit.

.. option:: -V, --version

   Print version information and exit.

