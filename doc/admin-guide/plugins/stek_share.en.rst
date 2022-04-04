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

.. _admin-plugins-stek_share:


STEK Share Plugin
*****************

This plugin coordinates STEK (Session Ticket Encryption Key) between ATS instances running in a group.
As the ID based session resumption rate continue to decrease, this new plugin will replace the
:ref:`admin-plugins-ssl_session_reuse` plugin.


How It Works
============

This plugin implements the `Raft consensus algorithm <https://raft.github.io/>` to decide on a leader. The leader will
periodically create a new STEK key and share it with all other ATS boxes in the group. When the plugin starts up, it
will automatically join the cluster of all other ATS boxes in the group, which will also automatically elect a leader.
The plugin uses the `TSSslTicketKeyUpdate` call to update ATS with the latest two STEK's it has received.

All communication are encrypted. All the ATS boxes participating in the STEK sharing must have access to the cert/key pair.

Note that since the this plugin only updates STEK every few hours, all Raft related stuff are kept in memory, and some code is
borrowed from the examples from `NuRaft library <https://github.com/eBay/NuRaft>` that is used in this plugin.


Building
========

This plugin uses `NuRaft library <https://github.com/eBay/NuRaft>` for leader election and communication.
The NuRaft library must be installed for this plugin to build. It can be specified by the `--with-nuraft` argument to configure.

This plugin also uses `YAML-CPP library <https://github.com/jbeder/yaml-cpp>` for reading the configuration file.
The YAML-CPP library must be installed for this plugin to build. It can be specified by the `--with-yaml-cpp` argument to configure.

As part of the experimental plugs, the `--enable-experimental-plugins` option must also be given to configure to build this plugin.


Config File
===========

STEK Share is a global plugin. Its configuration file uses YAML, and is given as an argument to the plugin in :file:`plugin.config`.

::
  stek_share.so etc/trafficserver/example_server_conf.yaml

Available options:

* server_id - An unique ID for the server.
* address - Hostname or IP address of the server.
* port - Port number for communication.
* asio_thread_pool_size - [Optional] Thread pool size for `ASIO library <http://think-async.com/Asio/>`. Default size is 4.
* heart_beat_interval - [Optional] Heart beat interval of Raft leader, must be less than "election_timeout_lower_bound". Default value is 100 ms.
* election_timeout_lower_bound - [Optional] Lower bound of Raft leader election timeout. Default value is 200 ms.
* election_timeout_upper_bound - [Optional] Upper bound of Raft leader election timeout. Default value is 400 ms.
* reserved_log_items - [Optional] The maximum number of logs preserved ahead the last snapshot. Default value is 5.
* snapshot_distance - [Optional] The number of log appends for each snapshot. Default value is 5.
* client_req_timeout - [Optional] Client request timeout. Default value is 3000 ms.
* key_update_interval - The interval between STEK update.
* server_list_file - Path to a file containing information of all the servers that's supposed to be in the Raft cluster.
* root_cert_file - Path to the root ca file.
* server_cert_file - Path to the cert file.
* server_key_file - Path to the key file.
* cert_verify_str - SSL verification string, for example "/C=US/ST=IL/O=Yahoo/OU=Edge/CN=localhost"


Example Config File
===================

.. literalinclude:: ../../../plugins/experimental/stek_share/example_server_conf.yaml


Server List File
================

Server list file as mentioned above, also in YAML.

* server_id - ID of the server.
* address - Hostname or IP address of the server.
* port - Port number of the server.


Example Server List File
========================

.. literalinclude:: ../../../plugins/experimental/stek_share/example_server_list.yaml
