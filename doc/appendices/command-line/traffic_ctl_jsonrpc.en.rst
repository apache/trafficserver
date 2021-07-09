.. Licensed to the Apache Software Foundation (ASF) under one or more contributor license
   agreements.  See the NOTICE file distributed with this work for additional information regarding
   copyright ownership.  The ASF licenses this file to you under the Apache License, Version 2.0
   (the "License"); you may not use this file except in compliance with the License.  You may obtain
   a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software distributed under the License
   is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
   or implied.  See the License for the specific language governing permissions and limitations
   under the License.

.. include:: ../../common.defs

.. |RPC| replace:: JSONRPC 2.0

.. _JSONRPC: https://www.jsonrpc.org/specification
.. _JSON: https://www.json.org/json-en.html

.. _traffic_ctl_jsonrpc:

traffic_ctl
***********

Synopsis
========

:program:`traffic_ctl` [OPTIONS] SUBCOMMAND [OPTIONS]


.. note::

   :program:`traffic_ctl` now uses a `JSONRPC`_ endpoint instead of ``traffic_manager``. ``traffic_manager`` is not
   required. To build this version of :program:`traffic_ctl` ``--enable-jsonrpc-tc`` should be passed when configure the build.

Description
===========

:program:`traffic_ctl` is used to display,manipulate and configure
a running Traffic Server. :program:`traffic_ctl` includes a number
of subcommands that control different aspects of Traffic Server:


:program:`traffic_ctl config`
   Manipulate and display configuration records
:program:`traffic_ctl metric`
   Manipulate performance and status metrics
:program:`traffic_ctl server`
   Stop, restart and examine the server
:program:`traffic_ctl storage`
   Manipulate cache storage
:program:`traffic_ctl plugin`
   Interact with plugins.
:program:`traffic_ctl host`
   Manipulate host status.  parents for now but will be expanded to origins.
:program:`traffic_ctl rpc`
   Interact directly with the |RPC| server in |TS|




Options
=======

.. program:: traffic_ctl
.. option:: --debug

   Enable debugging output.

.. option:: -V, --version

   Print version information and exit.

.. option:: -f, --format

   Specify the output print style. `legacy` and `pretty` are availables.
   `legacy` will honour the old :program:`traffic_ctl` output messages. `pretty` <if available> will print a different output.
   Errors from the server will be display if ``pretty`` is specified.
   In case of a record request(config, metric) ``--records`` overrides this flag.

   Default: ``legacy``

.. option:: -r, --debugrpc

   Display human readable rpc messages. This will display the request and the response from the server.

.. option:: --records

   Option available only for records request.

.. option:: --run-root

   Path to the runroot file.

Subcommands
===========

.. _traffic-control-command-alarm:

traffic_ctl alarm
-----------------

.. warning::

   Option not available in the |RPC| version.

.. _traffic-control-command-config:

traffic_ctl config
------------------

.. program:: traffic_ctl config
.. option:: defaults [--records]

   Display the default values for all configuration records. The ``--records`` flag has the same
   behavior as :option:`traffic_ctl config get --records`.

.. program:: traffic_ctl config
.. option:: describe RECORD [RECORD...]

   Display all the known information about a configuration record. This includes the current and
   default values, the data type, the record class and syntax checking expression.

   Error output available if  ``--format pretty`` is specified.

.. program:: traffic_ctl config
.. option:: diff [--records]

   Display configuration records that have non-default values. The ``--records`` flag has the same
   behavior as :option:`traffic_ctl config get --records`.

.. program:: traffic_ctl config
.. option:: get [--records] RECORD [RECORD...]

   Display the current value of a configuration record.

   Error output available if ``--format pretty`` is specified.

.. program:: traffic_ctl config get
.. option:: --records

   If this flag is provided, :option:`traffic_ctl config get` will emit results in
   :file:`records.config` format.

.. program:: traffic_ctl config
.. option:: match [--records] REGEX [REGEX...]

   Display the current values of all configuration variables whose names match the given regular
   expression. The ``--records`` flag has the same behavior as :option:`traffic_ctl config get
   --records`.

.. program:: traffic_ctl config
.. option:: reload

   Initiate a Traffic Server configuration reload. Use this command to update the running
   configuration after any configuration file modification. If no configuration files have been
   modified since the previous configuration load, this command is a no-op.

   The timestamp of the last reconfiguration event (in seconds since epoch) is published in the
   `proxy.node.config.reconfigure_time` metric.

.. program:: traffic_ctl config
.. option:: set RECORD VALUE

   Set the named configuration record to the specified value. Refer to the :file:`records.config`
   documentation for a list of the configuration variables you can specify. Note that this is not a
   synchronous operation.

.. program:: traffic_ctl config
.. option:: status

   Display detailed status about the Traffic Server configuration system. This includes version
   information, whether the internal configuration store is current and whether any daemon processes
   should be restarted.

.. _traffic-control-command-metric:

traffic_ctl metric
------------------
.. program:: traffic_ctl metric
.. option:: get METRIC [METRIC...]

   Display the current value of the specified statistics.

   Error output available if ``--format pretty`` is specified.

.. program:: traffic_ctl metric
.. option:: match REGEX [REGEX...]

   Display the current values of all statistics whose names match
   the given regular expression.

.. program:: traffic_ctl metric
.. option:: zero METRIC [METRIC...]

   Reset the named statistics to zero.

.. program:: traffic_ctl metric
.. option:: describe RECORD [RECORD...]

   Display all the known information about a metric record.

   Error output available if ``--format pretty`` is specified.

.. _traffic-control-command-server:

traffic_ctl server
------------------
.. program:: traffic_ctl server
.. option:: restart

   Option not yet available

.. program:: traffic_ctl server restart
.. option:: --drain

   This option modifies the behavior of :option:`traffic_ctl server restart` such that
   :program:`traffic_server` is not shut down until the number of active client connections drops to
   the number given by the :ts:cv:`proxy.config.restart.active_client_threshold` configuration
   variable.

.. program:: traffic_ctl server
.. option:: start

   Option not yet available

.. program:: traffic_ctl server
.. option:: status

   Option not yet available

.. program:: traffic_ctl server
.. option:: stop

   Option not yet available

.. program:: traffic_ctl server
.. option:: backtrace

   Option not yet available

.. _traffic-control-command-storage:

traffic_ctl storage
-------------------
.. program:: traffic_ctl storage
.. option:: offline PATH [PATH ...]

   Mark a cache storage device as offline. The storage is identified by :arg:`PATH` which must match
   exactly a path specified in :file:`storage.config`. This removes the storage from the cache and
   redirects requests that would have used this storage to other storage. This has exactly the same
   effect as a disk failure for that storage. This does not persist across restarts of the
   :program:`traffic_server` process.

.. _traffic-control-command-plugin:

traffic_ctl plugin
-------------------
.. program:: traffic_ctl plugin
.. option:: msg TAG DATA

   Send a message to plugins. All plugins that have hooked the
   ``TSLifecycleHookID::TS_LIFECYCLE_MSG_HOOK`` will receive a callback for that hook.
   The :arg:`TAG` and :arg:`DATA` will be available to the plugin hook processing. It is expected
   that plugins will use :arg:`TAG` to select relevant messages and determine the format of the
   :arg:`DATA`.

.. _traffic-control-command-host:

traffic_ctl host
----------------
.. program:: traffic_ctl host

A stat to track status is created for each host. The name is the host fqdn with a prefix of
"proxy.process.host_status". The value of the stat is a string which is the serialized
representation of the status. This contains the overall status and the status for each reason.  The
stats may be viewed using the :program:`traffic_ctl metric` command or through the `stats_over_http`
endpoint.

.. option:: --time count

   Set the duration of an operation to ``count`` seconds. A value of ``0`` means no duration, the
   condition persists until explicitly changed. The default is ``0`` if an operation requires a time
   and none is provided by this option.

.. option:: --reason active | local | manual

   Sets the reason for the operation.

   ``active``
      Set the active health check reason.

   ``local``
      Set the local health check reason.

   ``manual``
      Set the administrative reason. This is the default reason if a reason is needed and not
      provided by this option.

   Internally the reason can be ``self_detect`` if
   :ts:cv:`proxy.config.http.parent_proxy.self_detect` is set to the value 2 (the default). This is
   used to prevent parent selection from creating a loop by selecting itself as the upstream by
   marking this reason as "down" in that case.

   .. note::

      The up / down status values are independent, and a host is consider available if and only if
      all of the statuses are "up".

.. option:: status HOSTNAME [HOSTNAME ...]

   Get the current status of the specified hosts with respect to their use as targets for parent
   selection. This returns the same information as the per host stat.

.. option:: down HOSTNAME [HOSTNAME ...]

   Marks the listed hosts as down so that they will not be chosen as a next hop parent. If
   :option:`--time` is included the host is marked down for the specified number of seconds after
   which the host will automatically be marked up. A host is not marked up until all reason codes
   are cleared by marking up the host for the specified reason code.

   Supports :option:`--time`, :option:`--reason`.

.. option:: up HOSTNAME [HOSTNAME ...]

   Marks the listed hosts as up so that they will be available for use as a next hop parent. Use
   :option:`--reason` to mark the host reason code. The 'self_detect' is an internal reason code
   used by parent selection to mark down a parent when it is identified as itself and


   Supports :option:`--reason`.

.. _traffic_ctl_rpc:

traffic_ctl rpc
---------------
.. program:: traffic_ctl rpc

A mechanism to interact directly with the |TS| |RPC| endpoint. This means that this is not tied to any particular API
but rather to the rpc endpoint, so you can directly send requests and receive responses from the server.

.. option:: file

   Reads a file or a set of files from the disc, use the content of the files as message(s) to the |RPC| endpoint. All jsonrpc messages
   will be validated before sending them. If the file contains invalid  json|yaml format the message will not be send, in
   case of a set of files, if a particular file is not a proper json/yaml format then that particular file will be skipped.

   Example:

   .. code-block:: bash

      traffic_ctl rpc file jsonrpc_cmd1.json jsonrpc_cmd2.yaml

.. option:: get-api

   Request the entire admin api. This will retrieve all the registered methods and notifications on the server side.

   Example:

   .. code-block:: bash

      $ traffic_ctl rpc get-api
      Methods:
      - admin_host_set_status
      - admin_server_stop_drain
      - admin_server_start_drain
      - admin_clear_metrics_records
      - admin_clear_all_metrics_records
      - admin_plugin_send_basic_msg
      - admin_lookup_records
      - admin_config_set_records
      - admin_storage_get_device_status
      - admin_storage_set_device_offline
      - admin_config_reload
      - show_registered_handlers
      Notifications:
      - some_registered_notification_handler


.. option:: input

   Input mode, traffic_ctl will provide a control input from a stream buffer. Once the content is written the terminal :program:`traffic_ctl`
   will wait for the user to press Control-D to send the request to the rpc endpoint.
   This feature allows you to directly interact with the jsonrpc endpoint and test your API easily and without the need to know the low level
   implementation details of the transport layer.
   :program:`traffic_ctl` will validate the input format, not the message content. The message content will be validated by the server.
   See example `input_example_2`_.

   .. option:: --raw, -r

      No json/yaml parse validation will take place, the input content will be directly send to the server.

   Example:

   .. code-block::

      $ traffic_ctl rpc input
      >> Ctrl-D to fire the request
      {
         "id":"86e59b43-185b-4a0b-b1c1-babb1a3d5401",
         "jsonrpc":"2.0",
         "method":"admin_lookup_records",
         "params":[
            {
               "record_name":"proxy.config.diags.debug.tags",
               "rec_types":[
                  "1",
                  "16"
               ]
            }
         ]
      }
      <pressed Ctrl-D>

      <-- Server's response.
      {
         "jsonrpc":"2.0",
         "result":{
            "recordList":[
               {
                  "record":{
                     "record_name":"proxy.config.diags.debug.tags",
                     "record_type":"3",
                     "version":"0",
                     "raw_stat_block":"0",
                     "order":"423",
                     "config_meta":{
                        "access_type":"0",
                        "update_status":"0",
                        "update_type":"1",
                        "checktype":"0",
                        "source":"3",
                        "check_expr":"null"
                     },
                     "record_class":"1",
                     "overridable":"false",
                     "data_type":"STRING",
                     "current_value":"rpc",
                     "default_value":"http|dns"
                  }
               }
            ]
         },
         "id":"86e59b43-185b-4a0b-b1c1-babb1a3d5401"
      }


.. _input_example_2:

   Example 2:

   You can see a valid  json ``{}`` but an invalid |RPC| message. In this case the server is responding.

   .. code-block::

      $ traffic_ctl rpc input
      >> Ctrl-D to fire the request
      {}
      <pressed Ctrl-D>
      < -- Server's response
      {
         "jsonrpc":"2.0",
         "error":{
            "code":-32600,
            "message":"Invalid Request"
         }
      }

Examples
========

Mark down a host with `traffic_ctl` and view the associated host stats::

   .. code-block:: bash

      # traffic_ctl host down cdn-cache-02.foo.com --reason manual

      # traffic_ctl metric match host_status
      proxy.process.host_status.cdn-cache-01.foo.com HOST_STATUS_DOWN,ACTIVE:UP:0:0,LOCAL:UP:0:0,MANUAL:DOWN:1556896844:0,SELF_DETECT:UP:0
      proxy.process.host_status.cdn-cache-02.foo.com HOST_STATUS_UP,ACTIVE:UP:0:0,LOCAL:UP:0:0,MANUAL:UP:0:0,SELF_DETECT:UP:0
      proxy.process.host_status.cdn-cache-origin-01.foo.com HOST_STATUS_UP,ACTIVE:UP:0:0,LOCAL:UP:0:0,MANUAL:UP:0:0,SELF_DETECT:UP:0

In the example above, 'cdn-cache-01.foo.com' is unavailable, `HOST_STATUS_DOWN` and was marked down
for the `manual` reason, `MANUAL:DOWN:1556896844:0`, at the time indicated by the UNIX time stamp
`1556896844`.  To make the host available, one would have to clear the `manual` reason using:

   .. code-block:: bash

      # traffic_ctl host up cdn-cache-01.foo.com --reason manual

Configure Traffic Server to insert ``Via`` header in the response to the client

   .. code-block:: bash

      # traffic_ctl config set proxy.config.http.insert_response_via_str 1
      # traffic_ctl config reload

Autest
======

If you want to interact with |TS| under a unit test, then a few things need to be considered.

- Runroot needs to be configured in order to  let `traffic_ctl` knows where to find the socket.

   There are currently two ways to do this:

   1. Using `run-root` param.

      1. Let `Test.MakeATSProcess` to create the runroot file under the |TS| config directory. This can be done by passing `dump_runroot=True` to the above function:

       .. code-block:: python

         ts = Test.MakeATSProcess(..., dump_runroot=True)


      `dump_runroot` will write out some of the keys inside the runroot file, in this case the `runtimedir`.

      2. Then you should specify the :option:`traffic_ctl --run-root` when invoking the command:

         .. code-block:: python

            tr.Processes.Default.Command = f'traffic_ctl config reload --run-root {ts.Disk.runroot_yaml.Name}'

   2. Setting up the `TS_RUNROOT` environment variable.
      This is very similar to `1` but, instead of passing the `--run-root` param to `traffic_ctl`, you just need to specify the
      `TS_RUNROOT` environment variable. To do that, just do as 1.1 shows and then:

      .. code-block:: python

         ts.SetRunRootEnv()

      The above call will set the variable, please be aware that this variable will also be read by TS.

See also
========

:manpage:`records.config(5)`,
:manpage:`storage.config(5)`,
:ref:`admnin-jsonrpc-configuration`,
:ref:`jsonrpc-protocol`
