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

.. _admin-plugins-traffic_dump:


Traffic Dump Plugin
*******************

Description
===========

``Traffic Dump`` captures traffic for a set of sampled sessions and records this traffic to replay files according to the :ts:git:`tests/tools/traffic-replay/replay_schema.json` schema. These replay files can be used to replay traffic for testing purposes. Further, since the traffic is written decrypted, they can be used to conveniently analyze HTTP traffic going through ATS.

Plugin Configuration
====================
.. program:: traffic_dump.so

``traffic_dump.so``
  ``Traffic Dump`` is a global plugin and is configured via :file:`plugin.config`.

   .. option:: --logdir <path_to_dump>

   (`required`) - Specifies the directory for writing all dump files. If the path is relative it is considered relative to the Traffic Server directory. The plugin will use the first three characters of the client's IP to create subdirs in an attempt to spread dumps evenly and avoid too many files in a single directory.

   .. option:: --sample <N>

   (`required`) - Specifies the sampling ratio N. Traffic Dump will capture every one out of N sessions. This ratio can also be changed via ``traffic_ctl`` without restarting ATS.

   .. option:: --limit <N>

   (`optional`) - Specifies the maximum disk usage to N bytes (approximate). Traffic Dump will stop capturing new sessions once disk usage exceeds this limit. If this option is not used then no disk utilization limit will be enforced.

   .. option:: --sensitive-fields <field1,field2,...,fieldn>

   (`optional`) - A comma separated list of HTTP case-insensitive field names whose values are considered sensitive information. Traffic Dump will not dump the incoming field values for any of these fields but will instead dump a generic value for them of the same length as the original. If this option is not used, a default list of "Cookie,Set-Cookie" is used. Providing this option overwrites that default list with whatever values the user provides. Pass a quoted empty string as the argument to specify that no fields are sensitive,

   .. option:: --sni-filter <SNI_Name>

   (`optional`) - An SNI with which to filter sessions. Only HTTPS sessions with the provided SNI will be dumped. The sample option will apply a sampling rate to these filtered sessions. Thus, with a sample value of 2, 1/2 of all sessions with the specified SNI will be dumped.

``traffic_ctl`` <command>
  ``Traffic Dump`` can be dynamically configured via ``traffic_ctl``.

   * ``traffic_ctl plugin msg traffic_dump.sample N`` - changes the sampling ratio N as mentioned above.
   * ``traffic_ctl plugin msg traffic_dump.reset`` - resets the disk usage counter.
   * ``traffic_ctl plugin msg traffic_dump.limit N`` - changes the max disk usage to N bytes as mentioned above.
   * ``traffic_ctl plugin msg traffic_dump.unlimit`` - configure Traffic Dump to not enforce a disk usage limit.

Replay Format
=============
This replay files contain the following information:

* Each sampled session and all the transactions for those sessions.
* Timestamps of each transaction.
* The four sets of HTTP message headers (user agent request, proxy request, origin server response, proxy response).
* The protocol stacks for the user agent and the origin server connections.
* The number of body bytes for each message.

For details, see the schema: :ts:git:`tests/tools/lib/replay_schema.json`

Post Processing
===============
Traffic Dump comes with a post processing script located at :ts:git:`plugins/experimental/traffic_dump/post_process.py`. This filters out incomplete sessions and transactions and merges a specifiable amount of sessions from a client into single replay files. It also optionally formats the single-line JSON files into a human readable format. It takes the following arguments:

* The first positional argument is the input directory containing the replay files captured by traffic_dump as described above.
* The second positional argument is the output directory containing the processed replay files.
* ``-n`` is an optional argument specifying how many sessions will be attempted to be merged into single replay files. The default is 10.

There are other options. Use ``--help`` for a description of these.
