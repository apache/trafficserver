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

.. _traffic_logcat:

traffic_logcat
**************

Synopsis
========

:program:`traffic_logcat` [-o output-file | -a] [-CEhSVw2] [input-file ...]

Description
===========

To analyze a binary log file using standard tools, you must first convert
it to ASCII. :program:`traffic_logcat` does exactly that.

Options
=======

.. program:: traffic_logcat

.. option:: -o PATH, --output_file PATH

Specifies where the command output is directed.

.. option:: -a, --auto_filename

Automatically generates the output filename based on the input
filename. If the input is from stdin, then this option is ignored.
For example::

     traffic_logcat -a squid-1.blog squid-2.blog squid-3.blog

generates::

     squid-1.log squid-2.log squid-3.log

.. option:: -f, --follow

Follows the file, like :manpage:`tail(1)` ``-f``

.. option:: -C, --clf

Attempts to transform the input to Netscape Common format, if possible.

.. option:: -E, --elf

Attempts to transform the input to Netscape Extended format, if possible.

.. option:: -S, --squid

Attempts to transform the input to Squid format, if possible.

.. option:: -2, --elf2

Attempt to transform the input to Netscape Extended-2 format, if possible.

.. option:: -T, --debug_tags

.. option:: -w, --overwrite_output

.. option:: -h, --help

   Print usage information and exit.

.. option:: -V, --version

   Print version information and exit.


.. note:: Use only one of the following options at any given time: ``-S``, ``-C``, ``-E``, or ``-2``.

If no input files are specified, then :program:`traffic_logcat` reads from the
standard input (``stdin``). If you do not specify an output file, then
:program:`traffic_logcat` writes to the standard output (``stdout``).

For example, to convert a binary log file to an ASCII file, you can use
the :program:`traffic_logcat` command with either of the following options
below::

    traffic_logcat binary_file > ascii_file
    traffic_logcat -o ascii_file binary_file

The binary log file is not modified by this command.

See Also
========

:manpage:`tail(1)`
