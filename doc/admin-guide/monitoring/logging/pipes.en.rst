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

.. include:: ../../../common.defs

.. _admin-monitoring-logging-pipes:

ASCII Log Pipes
***************

In addition to ``ASCII`` and ``BINARY`` file modes for custom log formats, |TS|
can output log entries in ``ASCII_PIPE`` mode. This mode writes the log entries
to a UNIX named pipe (a buffer in memory). Other processes may read from this
named pipe using standard I/O functions.

The advantage of this mode is that |TS| does not need to write the entries to
disk, which frees disk space and bandwidth for other tasks. When the buffer is
full, |TS| drops log entries and issues an error message indicating how many
entries were dropped. Because |TS| only writes complete log entries to the
pipe, only full records are dropped.

