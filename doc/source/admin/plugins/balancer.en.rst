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


This is a plugin for Traffic Server, that allows you to configure
mapping rules.

To use this plugin, configure a remap.config rule like

::
   map http://foo.com http://bar.com @plugin=balancer.so @pparam=rotation:news

The "To-Url" in the remap.config rule is generally not used, unless the
lookup completely fails (i.e. this is a backup URL for extreme error
cases).

This is a list of all available options (set via @pparam):

::
    rotation      The name of the rotation (e.g. news) [to-host in remap]
    hash      What to hash on, url, path, cookie, ip, header (primary)
    hash2     Optional, secondary hash, to hash within a multi-host bucket
    bucketw   Width of each hash bucket [1]

The rotation parameter specifies which rotation to do the lookup on. If
not specified, we will default to the same name as used in the To URL in
the remap rule.

The bucket width specifies how many hosts a particular hash bucket
should contain, for example:

::
    @pparam=bucketw:2

The hash parameter can be used zero or more times, without it, no
hashing is done at all. If you have more than one hash keys, they are
concatenated in the order specified. For example:

::
    @pparam=hash:ip @pparam=hash:cookie/B

The "header" hash key takes a required extra value, for example:

::
    @pparam=hash:header/Host

For "cookie" hash keys, you can optionally specify an identifier for
which cookie to use (without it, the entire cookie header is used). For
example:

::
    @pparam=hash:cookie/B

The secondary hash ("hash2") is used to provide "stickiness" within a
bucket that's larger than one host (i.e. bucketw > 1). This allows you
to (for example) have a primary hash on the URL, where each URL is
served by some number of servers. A secondary hash on B-cookie would
then provide user stickiness, so that for a particular URL, a particular
user will always hit the same server.

If the hashes you've requested (either "hash" or "hash2") can not be
generated, we default to using the URL instead for the primary hash. For
the secondary hash, if set, we'll default to the src-IP. If these
defaults are not desirable, make sure that you have at least one hash
key that is guaranteed to exist (e.g. @pparam=hash:ip).

If no "hash" parameters are specified, no hashing is done. This is the
default behavior, obviously. In this cash, the "hash2" directive has no
effect as well.

Finally, a couple of "flag" options (parameters) are available, to
control some of the lookup mechanisms:

-  @pparam=hostip will use the IP returned by the lookup

