.. Licensed to the Apache Software Foundation (ASF) under one or more
   contributor license agreements.  See the NOTICE file distributed
   with this work for additional information regarding copyright
   ownership.  The ASF licenses this file to you under the Apache
   License, Version 2.0 (the "License"); you may not use this file
   except in compliance with the License.  You may obtain a copy of
   the License at

      http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
   implied.  See the License for the specific language governing
   permissions and limitations under the License.

.. _admin-plugins-fq-pacing:


FQ Pacing Plugin
==================

This is a remap plugin  that allows ATS to rate limit an individual TCP connection. It is based on
Linux support for the Fair Queuing qdisc. FQ and SO_MAX_PACING_RATE is available in RedHat/Centos 7.2+,
Debian 8+, and any other Linux distro with a kernel 3.18 or greater.


How it Works
------------
When activated during remap processing, this plugin calls ``setsockopt(SO_MAX_PACING_RATE)`` on the
client socket. To prevent the rate from leaking to other remap rules the client may access in future
requests, a hook is set to deactivate the pacing when the current transaction completes.


Installation
------------
First, enable the FQ qdisc by setting ``net.core.default_qdisc=fq`` in ``/etc/sysctl.conf`` and rebooting.

The `FQ Pacing` plugin is a :term:`remap plugin`.  Enable it by adding
``fq_pacing.so`` to your :file:`remap.config` file.  Provide a ``--rate=BytesPerSec`` option to set
the maxmimum rate of a TCP connection matching that remap line.

Here is an example remap.config entry:

::

  map http://reverse-fqdn.com http://origin.com @plugin=fq_pacing.so @pparam=--rate=100000

