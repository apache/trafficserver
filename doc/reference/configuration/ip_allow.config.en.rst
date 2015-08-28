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

===============
ip_allow.config
===============

.. configfile:: ip_allow.config

The :file:`ip_allow.config` file controls client access to the Traffic
Server proxy cache. You can specify ranges of IP addresses that are
allowed to use the Traffic Server as a web proxy cache. After you modify
the :file:`ip_allow.config` file, navigate to the Traffic Server bin
directory and run the :option:`traffic_ctl config reload` command to apply changes. When
you apply the changes to a node in a cluster, Traffic Server
automatically applies the changes to all other nodes in the cluster.

Format
======

Each line in the :file:`ip_allow.config` file must have the following
format::

    src_ip=<range of IP addresses> action=<action> [method=<list of methods separated by '|'>]

where src_ip is the IP address or range of IP addresses of the
client(s). The action ``ip_allow`` enables the specified client(s) to
access the Traffic Server proxy cache, and ``ip_deny`` denies the
specified client(s) to access the Traffic Server proxy cache. Multiple
method keywords can be specified (method=GET method=HEAD), or multiple
methods can be separated by an '\|' (method=GET\|HEAD). The method
keyword is optional and it is defaulted to ALL. This supports ANY string
as the HTTP method, meaning no validation is done to check wether it
is a valid HTTP method. This allows you to create filters for any method
that your origin may require, this is especially useful if you use newer
methods that aren't know to trafficserver (such as PROPFIND) or if your
origin uses an http-ish protocol.

By default, the :file:`ip_allow.config` file contains the following lines,
which allows all methods to localhost to access the Traffic Server proxy
cache and denies PUSH, PURGE and DELETE to all IPs (note this allows all
other methods to all IPs)::

    src_ip=127.0.0.1                                  action=ip_allow method=ALL
    src_ip=::1                                        action=ip_allow method=ALL
    src_ip=0.0.0.0-255.255.255.255                    action=ip_deny  method=PUSH|PURGE|DELETE
    src_ip=::-ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff action=ip_deny  method=PUSH|PURGE|DELETE

Examples
========

The following example enables all clients to access the Traffic Server
proxy cache::

    src_ip=0.0.0.0-255.255.255.255 action=ip_allow

The following example allows all clients on a specific subnet to access
the Traffic Server proxy cache::

    src_ip=123.12.3.000-123.12.3.123 action=ip_allow

The following example denies all clients on a specific subnet to access
the Traffic Server proxy cache::

    src_ip=123.45.6.0-123.45.6.123 action=ip_deny

