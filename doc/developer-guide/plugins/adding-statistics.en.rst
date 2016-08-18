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

This chapter describes how to add statistics to your plugins. Statistics
can be coupled or uncoupled. *Coupled* statistics are quantities that
are related and must therefore be updated together. The Traffic Server
API statistics functions add your plugin's statistics to the Traffic
Server statistics system. You can view your plugin statistics as you
would any other |TS| statistic, using :program:`traffic_ctl`.

Uncoupled Statistics
====================

A statistic is an object of type ``TSStat``. The value of the statistic
is of type ``TSStatType``. The possible ``TSStatTypes`` are:

-  ``TSSTAT_TYPE_INT64``

-  ``TSSTAT_TYPE_FLOAT``

There is *no* ``TSSTAT_TYPE_INT32``.

To add uncoupled statistics, follow the steps below:

1. Declare your statistic as a global variable in your plugin. For
   example:

   .. code-block:: c

      static TSStat my_statistic;

2. In ``TSPluginInit``, create new statistics using ``TSStatCreate``.
   When you create a new statistic, you need to give it an "external"
   name that :program:`traffic_ctl` uses to access the statistic.
   For example:

   .. code-block:: c

      my_statistic = TSStatCreate ("my.statistic", TSSTAT_TYPE_INT64);

3. Modify (increment, decrement, or other modification) your statistic
   in plugin functions.
