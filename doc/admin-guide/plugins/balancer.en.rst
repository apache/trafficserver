.. _admin-plugins-balancer:

Balancer Plugin
***************

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


The ``balancer`` balances requests across multiple origin servers.
To use this plugin, configure it in a :file:`remap.config` rule, specifying
a balancing policy and a set of origin servers. For example::

   map http://foo.com http://foo.com \
      @plugin=balancer.so @pparam=--policy=hash,url @pparam=one.bar.com @pparam=two.bar.com

The ``replacement`` URL in the mapping rule is not used. The argument
to the ``--policy`` option is a comma-separated list of keywords.
The first keyword is the name of a balancing policy. The subsequent
keywords are used to refine the requested policy.

The remaining plugin arguments are balancer targets. Typically,
these will be the host names of origin servers that requests should
be balanced across. The target name may contain a colon-separated
port number.

Hash Balancing Policy
---------------------

The ``hash`` balancing policy performs a consistent hash across the
set of origins. This minimizes the number of hash entries that must
be moved when the set of origin servers changes. An optional list
of hash fields follows the ``hash`` keyword. Each specified hash
field is hashed to select an outbound origin server.

The following fields can be supplied to the hash:

key
  The request cache key. Note that the cache key will only be
  set if you have already chained a plugin that sets a custom
  cache key.

url
  The request URL. This is the default hash field that is used if
  no other fields are specified.

srcaddr
  The source IP address of the request.

dstaddr
  The destination IP address of the request.

Round Robin Balancing Policy
----------------------------

The ``roundrobin`` balancing policy simply allocates requests to
origin servers in order. Over time, the number of requests received
by each origin should be approximately the same.

Health Checking
---------------

The ``balancer`` plugin does not check the health of the origin
servers, however the plugin is fully reloadable so health checking
is usualy simple to implement. Most production environments already
have mechanisms to check service health. It is recommended that you
write a simple script to monitor this information and rewrite
:file:`remap.config` when appropriate. Running :option:`traffic_ctl config reload`
will reload the ``balancer`` plugin with the new set of origin servers.
