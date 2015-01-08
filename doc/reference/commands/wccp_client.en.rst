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

.. _wccp_client:

===========
wccp_client
===========

.. program:: wccp_client

Description
===========

Wccp_client is a front end to the wccp client library.  It is a stand
alone program that speaks the client side of the WCCP cache protocol.

It can be used instead of the built in WCCP feature in Apache traffic server.
This can be beneficial if you have multiple programs running on the same
computer that are relying on WCCP to redirect traffic from the router to 
the computer.

Since it relies on the wccp library, the wccp_client is only build if apache
traffic server is configured with --enable-wccp.

The overall Apache Traffic Server WCCP configuration documentation is
at  :ref:`WCCP Configuratoin <wccp-configuration>`

The wccp-client takes the following arguments. 

.. option:: --address IP address to bind.

.. option:: --router Booststrap IP address for routers.

.. option:: --service Path to service group definitions.

.. option:: --debug Print debugging information.

.. option:: --daemon Run as daemon.

You need to run at least with the --service arguments. 
An example service definition file, service-nogre-example.config, is included
in the tools/wccp_client directory.  In this file you define your MD5 security password
(highly recommended), and you define your service groups.  For each service
group you define how the service should be recognized (protocol and port),
the routers you are communicating with, whether you are using GRE or basic L2
routing to redirect packets.  

In addition, you can specify a proc-name, a path
to a process pid file.  If the proc-name is present, the wccp client will 
only advertise the associated service group, if the process is currently 
up and running.  So if your computer is hosting three services, and one of
them goes down, the wccp client could stop advertising the service group 
associated with the down service thus stopping the router from redirecting
that traffic, but continue to advertise and maintain the redireciton for the
other two services.

The current WCCP implementation associated with ATS only supports one cache
client per service group per router.  The cache assignment logic current
assigns the current cache client to all buckets.  
