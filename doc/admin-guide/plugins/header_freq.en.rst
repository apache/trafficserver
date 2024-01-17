.. _header_freq-plugin:

Header Frequency Plugin
***********************

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

The Header Frequency plugin keeps track of the number of times headers have been
seen in transactions. Two separate counteres are kept for the origin and the
client. This information is accessible via the ``log`` plugin message.  By
default the data is sent to traffic.out but it can alternatively be appended to
an arbitrary file. The following logs the stats to ``traffic.out``::

    traffic_ctl plugin msg header_freq log

The following appends the stats to ``/tmp/log.txt``. Note that this file must be
writeable by the traffic_server process's user::

    traffic_ctl plugin msg header_freq log:/tmp/log.txt


Installation
------------

Since Header Frequency plugin is an expiremental plugin, traffic_server must be configured
to build experimental plugins in order to use it::

    --enable-experimental-plugins


Once built, add the following line to :file:`plugin.config` and restart traffic_server to use it::

    header_freq.so
