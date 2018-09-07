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

.. _admin-plugins-access_control:


Traffic Dump Plugin
*******************

Description
===========

``Traffic Dump`` captures session traffic and writes to a replay file :ts:git:`tests/tools/traffic-replay/replay_schema.json` for each captured session. It then can be used to replay traffic for testing purpose.

Plugin Configuration
====================
.. program:: traffic_dump.so

* ``Traffic Dump`` is a global plugin and is configured via :file:`plugin.config`.
   .. option:: --logdir <path_to_dump>
   (`required`, default:empty/unused) - specifies the directory for writing all dump files. If path is relative, it is relative to the Traffic Server directory. The plugin will use first three characters of client ip to create subdirs in an attempt to spread dumps evenly and avoid too many files in a single directory.

   .. option:: --sample <N>
   (`optional`, default:1000) - specifies the sampling ratio N. Traffic Dump will capture every one out of N sessions. This ratio can also be changed via traffic_ctl without restarting ATS.

* ``traffic_ctl`` command.
   ``traffic_ctl plugin msg traffic_dump.sample N`` - changes the sampling ratio N as mentioned above.

Replay Format
=============
This format contains traffic data including:
* Each session and transactions in the session.
* Timestamps.
* The four headers (ua request, proxy request, origin server response, proxy response).
* The protocol stack for the user agent.
* The transaction count for the outbound session.
* The content block sizes.
* See schema here: :ts:git:`tests/tools/traffic-replay/replay_schema.json`
