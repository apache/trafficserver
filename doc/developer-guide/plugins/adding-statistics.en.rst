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

This chapter describes how to add statistics to your plugins.  The
|TS| statistics API functions add your plugin's statistics so you
can view your plugin statistics as you would any other |TS| statistic,
using :program:`traffic_ctl` or the c:func:`TSRecordDump` API.

A statistic is an opaque object referred to by an integral handle
returned by c:func:`TSStatCreate`. Only integer statistics are
supported, so the :arg:`type` argument to c:func:`TSStatCreate` must
be c:data:`TS_RECORDDATATYPE_INT`.

The following example shows how to add custom statistics to your
plugin. Typically, you would attempt to find the statistic by name
before creating is. This technique is useful if you want to increment
a statistic from multiple plugins. Once you have a handle to the
statistic, set the value with c:func:`TSStatIntSet`, and increment it with
c:func:`TSStatIntIncrement` or c:func:`TSStatIntDecrement`.

.. literalinclude:: ../../../example/statistic/statistic.cc
   :language: c
   :lines: 30-
