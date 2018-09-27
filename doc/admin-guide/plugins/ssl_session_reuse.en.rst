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

.. _admin-plugins-ssl_session_reuse:

SSL Session Reuse Plugin
************************

This plugin coordinates session state data between ATS instances running in a group.  This should 
improve TLS session reuse (both ticket and ID based) for a set of machines fronted by some form of
layer 4 connection load balancer.

How It Works
============

The plugin coordinates TLS session reuse for both Session ID based resumption and ticket based resumption.
For Session ID base resumption in uses the ATS SSL Session Cache for the local store of TLS sessions.  It uses
Redis to communication new sessions with its peers.  When a new session is seen by an ATS instances it 
publishes an encrypted copy of the session state to the local Redis channel.  When a new session is received
on the Redis channel, the plugin stores that session state into its local ATS SSL session cache.  Once the
session state is in the local ATS SSL session cache it is avalible to the openssl library for future TLS 
handshakes.

For the ticket based session resumption, the plugin implements logic to decide on a Session Ticket Encryption Key (STEK)
master.  The master will periodically create a new STEK key and use the Redis channel to publish the new STEK key 
to the other ATS boxes in the group.  When the plugin starts up, it will publish a Redis message requesting the master to
resend the STEK key.  The plugin uses the TSSslTicketKeyUpdate call to update ATS with the last two STEK's it has received.

All communication over the Redis channel is encrypted with a preshared key.  All the ATS boxes participating in the session
resuse must have access to that preshared key.

Building
========

This plugin uses Redis for communication.  The hiredis client development library must be installed 
for this plugin to build.  It can be installed in the standard system location or the install location
can be specified by the --with-hiredis argument to configure.

As part of the expermental plugs, the --enable-experimental-plugins option must also be given to configure
to build this plugin.

Deploying
=========

The SSL Session Reuse plugin relies on Redis for communication.  To deploy build your own redis server or use a standard rpm
package.  It must be installed on at least one box in the ATS group.  We have it installed on two boxes in a failover 
scenario.  The SSL Session Resuse configuration file describes how to communicate with the redis servers.  

* :ts:cv:`proxy.config.ssl.session_cache` should be set to 2 to enable the ATS implementation of session cache
* :ts:cv:`proxy.config.ssl.session_cache.size` and :ts:cv:`proxy.config.ssl.session_cache.num_buckets` may need to be adjusted to ensure good hash table performance for your workload.  For example, we needed to increase the number of buckets to avoid long hash chains.
* :ts:cv:`proxy.config.ssl.server.session_ticket_enable` should be set to 1 to enable session ticket support.


Config File
===========

SSL Session Reuse is a global plugin.  Its configuration file is given as a argument to the plugin. 

* redis.RedisEndpoints - This is a comma separated list of Redis servers to connect to.  The description of the redis server may include a port
* redis.RedisConnectTimeout - Timeout on the redis connect attempt in milliseconds.
* redis.RedisRetryDelay - Timeout on retrying redis operations in miliseconds.
* pubconfig.PubNumWorkers - Number of worker threads.  Must be at least as many as the number of redis servers.
* pubconfig.PubRedisPublishTries - Number of times to attempt publishing data
* pubconfig.PubRedisConnectTries - Number of times to retry a redis connection attempt
* pubconfig.PubMaxQueuedMessages - Maximum number of undelivered messages to leave in the queue
* ssl_session.ClusterName - Name associated with the group of machines.  Used to form basis of the redis channel name, e.g. Pool1
* ssl_session.KeyUpdateInterval - How often to update the STEK key in seconds.
* ssl_session.STEKMaster - If set to 1, the machine will assume it is the STEK master on startup
* ssl_session.redis_auth_key_file - The location of the file containing the redis preshared secret.
* subconfig.SubColoChannel - The redis channels to subscribe to, e.g. Pool1.*



Example Config File
===================

.. literalinclude:: ../../../plugins/experimental/ssl_session_reuse/example_config.config

