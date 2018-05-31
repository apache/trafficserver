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

.. _admin-monitoring-alarms:

Traffic Manager Alarms
**********************

|TS| signals an alarm when it detects a problem. For example, the space
allocated to event logs could be full or |TS| may not be able to write to a
configuration file.

Email Alarms
============

To configure |TS| to send an email to a specific address whenever an alarm
occurs, follow the steps below:

#. Set :ts:cv:`proxy.config.alarm_email` in :file:`records.config` to the email
   address you want to receive alarm notifications. ::

        CONFIG proxy.config.alarm_email STRING "alerts@example.com"

#. Run the command :option:`traffic_ctl config reload` to apply the configuration changes.

Using a Script File for Alarms
------------------------------

Alarm messages are built into Traffic Server and cannot be changed.
However, you can write a script file to execute certain actions when an
alarm is signaled. Traffic Server provides a sample script file named
``example_alarm_bin.sh`` in the ``bin`` directory which can serve as the
basis for your custom alarm scripts.

Viewing Statistics from Traffic Control
=======================================

You can use :program:`traffic_ctl` to view statistics
about Traffic Server performance and web traffic. In addition to viewing
statistics, you can also configure, stop, and restart the Traffic Server
system. For additional information, refer to :ref:`configure-using-traffic-ctl`
and :program:`traffic_ctl`. You can view specific information about a
Traffic Server node by specifying the variable that corresponds to the
statistic you want to see.

To view a statistic, enter the following command:::

        traffic_ctl metric get VARIABLE

where ``variable`` is the variable representing the information you
want to view. For a list of variables you can specify, refer to :ref:`traffic_ctl metrics <traffic-ctl-metric>`.

For example, the following command displays the total number of completed
HTTP requests:::

     traffic_ctl metric get proxy.process.http.completed_requests

