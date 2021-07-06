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

.. _developer-plugins-add-statistics:

Adding Statistics
*****************

This chapter describes how to add statistics to your plugins. The |TS| statistics API functions add
your plugin's statistics so you can view your plugin statistics as you would any other |TS|
statistic, using :program:`traffic_ctl` or the :c:func:`TSRecordDump` API.

A statistic is an opaque object referred to by an integral handle returned by
:c:func:`TSStatCreate`. Only integer statistics are supported, so the :arg:`type` argument to
:c:func:`TSStatCreate` must be :c:macro:`TS_RECORDDATATYPE_INT`.

The following example shows how to add custom statistics to your plugin. Typically, you would
attempt to find the statistic by name before creating is. This technique is useful if you want to
increment a statistic from multiple plugins. Once you have a handle to the statistic, set the value
with :c:func:`TSStatIntSet`, and increment it with :c:func:`TSStatIntIncrement` or
:c:func:`TSStatIntDecrement`.

.. literalinclude:: ../../../example/plugins/c-api/statistic/statistic.cc
   :language: cpp
   :lines: 32-

If this plugin is loaded, then the statistic can be accessed with ::

   traffic_ctl metric show plugin.statistics.now

The name of the statistic can be any string but it is best to use the convention of starting it with the literal tag "plugin" followed the plugin name followed by statistic specific tags. This avoids any name collisions and also makes finding all statistics for a plugin simple. For instance, the "redirect_1" example plugin creates a number of statistics. These can be listed with the command ::

   traffic_ctl metric match plugin.redirect_1..*

The "redirect_1" example plugin has a more realistic handling of statistics with regard to updating and is worth examining.

:c:func:`TSStatFindName` can be used to check if the statistic already exists or to provide a generic interface to statistics. In the example above you can see the code first verifies the statistic does not already exist before creating it. In general, though, this should be handled by not executing the registration code twice. If done only from plugin initialization then this will be the case. It can be the case however that statistics are based on configuration data which may be reloaded and the check must be done. This is more likely with remap plugins.
