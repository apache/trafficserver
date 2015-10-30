.. _configuring-traffic-server:

Configuring Traffic Server
**************************

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

Traffic Server provides several options for configuring the system.

.. toctree::
   :maxdepth: 2

.. _configure-using-traffic-line:

Configure Traffic Server Using Traffic Line
===========================================

Traffic Line enables you to quickly and easily change your Traffic
Server configuration via command-line interface.

View Configuration Options in Traffic Line
------------------------------------------

To view a configuration setting, enter the following command::

    traffic_ctl config get VARIABLE

where *var* is the variable associated with the configuration
option. For a list of variables, refer to :ref:`configuration-variables`.

Change Configuration Options in Traffic Line
--------------------------------------------

To change the value of a configuration setting, enter the following
command::

    traffic_config set VARIABLE VALUE

where *var* is the variable associated with the configuration option
and *value* is the value you want to use. For a list of the
variables, see :ref:`configuration-variables`.

Configure Traffic Server Using Configuration Files
==================================================

As an alternative to using Traffic Line, you can change
Traffic Server configuration options by manually editing specific
variables in :file:`records.config`.

Traffic Server must reread the configuration files for any changes to take effect.
This is done with :option:`traffic_ctl config reload`. Some configuration changes require a
full restart of Traffic Server.

The following is a sample portion of :file:`records.config`:

.. figure:: ../static/images/admin/records.jpg
   :align: center
   :alt: Sample records.config file

   Sample records.config file

In addition to :file:`records.config`,
Traffic Server provides other configuration files that are used to
configure specific features. You can manually edit all configuration
files as described in :ref:`admin-configuration-files`.
